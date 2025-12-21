#include "modem.h"

#include <Wire.h>

// XPowers
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

// TinyGSM
#define TINY_GSM_MODEM_SIM7080
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>

// -------- Board wiring (T-SIM7080G-S3) --------
static constexpr int PMU_I2C_SDA = 15;   // per your PMIC document
static constexpr int PMU_I2C_SCL = 7;

static constexpr int MODEM_RXD   = 4;    // modem RXD -> ESP32 TX
static constexpr int MODEM_TXD   = 5;    // modem TXD -> ESP32 RX
static constexpr int MODEM_PWR   = 41;   // modem PWRKEY

static constexpr uint32_t MODEM_BAUD = 115200;

// ---------------------------------------------
static XPowersPMU PMU;
static TinyGsm modem(Serial1);

static bool is_plausible_year(int year)
{
    return (year >= 2020 && year <= 2099);
}

static void pwrkey_pulse()
{
    // Manufacturer example style pulse (safe fallback)
    pinMode(MODEM_PWR, OUTPUT);
    digitalWrite(MODEM_PWR, LOW);
    delay(100);
    digitalWrite(MODEM_PWR, HIGH);
    delay(1000);
    digitalWrite(MODEM_PWR, LOW);
}

static bool pmu_enable_modem_rails()
{
    Wire.begin(PMU_I2C_SDA, PMU_I2C_SCL);
    Wire.setClock(400000);

    // NOTE: XPowers begin signature needs (Wire, addr, sda, scl)
    if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_I2C_SDA, PMU_I2C_SCL))
    {
        Serial.println("❌ PMU init failed");
        return false;
    }

    // Modem main power rail
    PMU.setDC3Voltage(3000);
    PMU.enableDC3();

    // Modem / GNSS IO / antenna rail
    PMU.setBLDO2Voltage(3300);
    PMU.enableBLDO2();

    // Do NOT touch camera rails here (per your request)

    // Recommended by vendor examples
    PMU.disableTSPinMeasure();

    // Rail stabilization (your doc mentions timing)
    delay(100);
    return true;
}

static bool wait_for_at_ready(uint32_t timeout_ms)
{
    uint32_t start = millis();
    int retry = 0;

    // First try: modem might already be up (auto-boot via rails)
    while (millis() - start < timeout_ms)
    {
        if (modem.testAT(1000))
            return true;

        Serial.print(".");
        delay(200);

        // If not responding, occasionally try a PWRKEY pulse (safe fallback)
        if (++retry > 15)
        {
            Serial.println("\n⚠ AT not ready → PWRKEY pulse retry");
            pwrkey_pulse();
            retry = 0;
        }
    }
    return false;
}

static bool is_registered_line(const String &line)
{
    // Accept ,1 (home) or ,5 (roaming)
    // Example: +CEREG: 0,1
    // Example: +CREG: 0,5
    int comma = line.lastIndexOf(',');
    if (comma < 0) return false;

    String stat = line.substring(comma + 1);
    stat.trim();

    return (stat == "1" || stat == "5");
}

static bool wait_for_network_registration(uint32_t timeout_ms)
{
    uint32_t start = millis();

    while (millis() - start < timeout_ms)
    {
        // LTE registration
        modem.sendAT("+CEREG?");
        if (modem.waitResponse(2000, "+CEREG:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();
            if (is_registered_line(line))
                return true;
        }
        modem.waitResponse(500);

        // CS registration fallback
        modem.sendAT("+CREG?");
        if (modem.waitResponse(2000, "+CREG:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();
            if (is_registered_line(line))
                return true;
        }
        modem.waitResponse(500);

        delay(1000);
    }
    return false;
}

bool modem_init_early()
{
    Serial.println("📡 Modem early init (PMU + AT)");

    if (!pmu_enable_modem_rails())
        return false;

    // Bring up modem UART (HardwareSerial owns the UART driver for UART1)
    Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RXD, MODEM_TXD);

    // Wait for AT
    Serial.print("📡 Probing AT");
    if (!wait_for_at_ready(30000))
    {
        Serial.println("\n❌ Modem not responding to AT");
        return false;
    }
    Serial.println("\n✅ Modem AT ready");

    // Enable network time + timezone reporting (needed for meaningful +CCLK)
    modem.sendAT("+CLTS=1");
    modem.waitResponse(2000);

    modem.sendAT("+CTZR=1");
    modem.waitResponse(2000);

    return true;
}

bool modem_get_timestamp(char *out, size_t out_len)
{
    if (!out || out_len < 16)
        return false;

    // Default fallback first (so caller always gets something)
    snprintf(out, out_len, "UPT%08lu", (unsigned long)(millis() / 1000UL));

    Serial.println("🕒 Waiting for network registration (for valid time)...");

    // If registration never happens, we keep fallback timestamp
    if (!wait_for_network_registration(60000))
    {
        Serial.println("⚠ Network registration timeout → timestamp fallback");
        return false;
    }

    // Query CCLK a few times; sometimes it’s valid only after reg
    for (int attempt = 0; attempt < 10; attempt++)
    {
        modem.sendAT("+CCLK?");
        if (modem.waitResponse(3000, "+CCLK:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();

            int q1 = line.indexOf('\"');
            int q2 = line.indexOf('\"', q1 + 1);
            if (q1 < 0 || q2 < 0)
            {
                modem.waitResponse(500);
                delay(500);
                continue;
            }

            String dt = line.substring(q1 + 1, q2); // "YY/MM/DD,HH:MM:SS+ZZ"
            // Parse minimal fields safely
            if (dt.length() < 17)
            {
                modem.waitResponse(500);
                delay(500);
                continue;
            }

            String yy = dt.substring(0, 2);
            String MM = dt.substring(3, 5);
            String dd = dt.substring(6, 8);
            String hh = dt.substring(9, 11);
            String mm = dt.substring(12, 14);
            String ss = dt.substring(15, 17);

            int year = 2000 + yy.toInt();
            if (!is_plausible_year(year))
            {
                Serial.printf("⚠ Ignoring implausible modem time: %s\n", dt.c_str());
                modem.waitResponse(500);
                delay(1000);
                continue;
            }

            snprintf(out, out_len, "%04d%s%s_%s%s%s",
                     year,
                     MM.c_str(),
                     dd.c_str(),
                     hh.c_str(),
                     mm.c_str(),
                     ss.c_str());

            Serial.printf("🕒 Modem timestamp: %s\n", out);
            return true;
        }

        modem.waitResponse(500);
        delay(1000);
    }

    Serial.println("⚠ Could not read +CCLK with plausible year → timestamp fallback");
    return false;
}

#include "modem.h"

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#define TINY_GSM_MODEM_SIM7080
#include <TinyGsmClient.h>

/* =============================
   MODEM PINS (T-SIM7080G-S3)
   ============================= */
#define MODEM_RX_PIN   4
#define MODEM_TX_PIN   5
#define MODEM_PWR_PIN  41

#define PMU_I2C_SDA    15
#define PMU_I2C_SCL    7

#define MODEM_BAUD     115200

/* =============================
   GLOBALS
   ============================= */
static HardwareSerial ModemSerial(1);
static TinyGsm modem(ModemSerial);
static XPowersPMU PMU;

/* =============================
   INTERNAL HELPERS
   ============================= */
static bool year_plausible(int year)
{
    return year >= 2022 && year <= 2099;
}

static bool wait_for_network(uint32_t timeout_ms)
{
    uint32_t start = millis();

    while (millis() - start < timeout_ms)
    {
        modem.sendAT("+CEREG?");
        if (modem.waitResponse(2000, ",1") == 1 ||
            modem.waitResponse(2000, ",5") == 1)
            return true;

        modem.sendAT("+CREG?");
        if (modem.waitResponse(2000, ",1") == 1 ||
            modem.waitResponse(2000, ",5") == 1)
            return true;

        delay(1000);
    }

    return false;
}

/* =============================
   PUBLIC API
   ============================= */

bool modem_init_early()
{
    Serial.println("📡 Modem early init (PMU + AT)");

    Wire.begin(PMU_I2C_SDA, PMU_I2C_SCL);

    if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_I2C_SDA, PMU_I2C_SCL))
    {
        Serial.println("❌ PMU init failed");
        return false;
    }

    /* ---- Modem power rails ONLY ---- */
    PMU.setDC3Voltage(3000);
    PMU.enableDC3();

    PMU.setBLDO2Voltage(3300);
    PMU.enableBLDO2();

    PMU.disableTSPinMeasure();

    delay(100);

    /* ---- UART ---- */
    ModemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    /* ---- Auto-boot modem ---- */
    pinMode(MODEM_PWR_PIN, OUTPUT);
    digitalWrite(MODEM_PWR_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWR_PIN, HIGH);
    delay(1000);
    digitalWrite(MODEM_PWR_PIN, LOW);

    /* ---- AT readiness ---- */
    uint32_t start = millis();
    while (!modem.testAT(1000))
    {
        if (millis() - start > 30000)
        {
            Serial.println("❌ Modem AT timeout");
            return false;
        }
        delay(500);
    }

    Serial.println("✅ Modem AT ready");

    modem.sendAT("+CLTS=1");
    modem.waitResponse();

    modem.sendAT("+CTZR=1");
    modem.waitResponse();

    return true;
}

bool modem_get_timestamp(char *out, size_t out_len)
{
    Serial.println("🕒 Waiting for network registration (for valid time)...");

    if (!wait_for_network(60000))
    {
        Serial.println("⚠ Network registration timeout → timestamp fallback");
        snprintf(out, out_len, "UPT_%lu", millis() / 1000);
        return false;
    }

    for (int attempt = 0; attempt < 10; attempt++)
    {
        modem.sendAT("+CCLK?");
        if (modem.waitResponse(5000, "+CCLK:") != 1)
        {
            delay(1000);
            continue;
        }

        String line = ModemSerial.readStringUntil('\n');
        line.trim();

        int q1 = line.indexOf('"');
        int q2 = line.indexOf('"', q1 + 1);
        if (q1 < 0 || q2 < 0)
            continue;

        String body = line.substring(q1 + 1, q2);
        if (body.length() < 17)
            continue;

        int year = 2000 + body.substring(0, 2).toInt();
        if (!year_plausible(year))
        {
            Serial.printf("⚠ Implausible modem year %d\n", year);
            delay(1000);
            continue;
        }

        snprintf(
            out,
            out_len,
            "%04d%s%s_%s%s%s",
            year,
            body.substring(3,5).c_str(),
            body.substring(6,8).c_str(),
            body.substring(9,11).c_str(),
            body.substring(12,14).c_str(),
            body.substring(15,17).c_str()
        );

        Serial.printf("🕒 Modem timestamp: %s\n", out);
        return true;
    }

    Serial.println("⚠ Failed to obtain valid modem time → fallback");
    snprintf(out, out_len, "UPT_%lu", millis() / 1000);
    return false;
}

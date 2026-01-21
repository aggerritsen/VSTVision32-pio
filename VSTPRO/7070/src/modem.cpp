// src/modem.cpp (RFC) — dual-board modem init
//
// 7070 (VST_BOARD_7070):
//   - NO PMU usage
//   - Serial1 + TinyGSM AT + network time via +CCLK?
//
// 7080 (VST_BOARD_7080):
//   - PMU rails via XPowers (AXP2101)
//   - Then Serial1 + TinyGSM AT + network time via +CCLK?
//
// IMPORTANT RULES (per your requirements):
//   - No structural/public API changes
//   - No function renames
//   - Modem model macro must come from build_flags (platformio.ini)
//
// Pins always come from config.h

#include "modem.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>

#ifndef TINY_GSM_RX_BUFFER
#define TINY_GSM_RX_BUFFER 1024
#endif
#include <TinyGsmClient.h>

#if defined(VST_BOARD_7080)
#include <XPowersLib.h>
static XPowersPMU PMU;
#endif

static TinyGsm modem(Serial1);

// -----------------------------
static bool is_plausible_year(int year)
{
    return (year >= 2020 && year <= 2099);
}

// Many LilyGO boards use "inverted" PWRKEY level-shift logic.
// Your working behavior for 7070/7080 is:
//   drive HIGH for ~1s then LOW (as you used in examples).
static void pwrkey_pulse()
{
    pinMode(MODEM_PWR, OUTPUT);

    // Start: HIGH then LOW (matches your example / earlier working behavior)
    digitalWrite(MODEM_PWR, HIGH);
    delay(50);

    // Required pulse width for SIM70xx/7080 is typically ~1s
    digitalWrite(MODEM_PWR, LOW);
    delay(1100);

    // Release back HIGH (or leave HIGH)
    digitalWrite(MODEM_PWR, HIGH);
    delay(50);
}

static bool wait_for_at_ready(uint32_t timeout_ms)
{
    uint32_t start = millis();
    int retry = 0;

    while (millis() - start < timeout_ms)
    {
        if (modem.testAT(1000))
            return true;

        delay(200);

        if (++retry > 15)
        {
            Serial.println("⚠ AT not ready → PWRKEY pulse");
            pwrkey_pulse();
            retry = 0;
        }
    }
    return false;
}

static bool is_registered_line(const String &line)
{
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
        modem.sendAT("+CEREG?");
        if (modem.waitResponse(2000, "+CEREG:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();
            if (is_registered_line(line))
                return true;
        }

        modem.sendAT("+CREG?");
        if (modem.waitResponse(2000, "+CREG:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();
            if (is_registered_line(line))
                return true;
        }

        delay(1000);
    }
    return false;
}

#if defined(VST_BOARD_7080)
static bool pmu_enable_modem_rails_7080()
{
    // Uses config.h pins you confirmed worked:
    // PMU_I2C_SDA=15, PMU_I2C_SCL=7, PMU_I2C_HZ=400000
    Wire.begin(PMU_I2C_SDA, PMU_I2C_SCL);
    Wire.setClock(PMU_I2C_HZ);

    Serial.println("⚡ PMU.begin(...)");
    if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_I2C_SDA, PMU_I2C_SCL))
    {
        Serial.println("❌ PMU init failed");
        return false;
    }

    // Your previous working rails
    Serial.println("⚡ Enabling rails: DC3=3.0V, BLDO2=3.3V");
    PMU.setDC3Voltage(3000);
    PMU.enableDC3();

    PMU.setBLDO2Voltage(3300);
    PMU.enableBLDO2();

    PMU.disableTSPinMeasure();
    delay(100);

    Serial.println("✅ PMU rails OK");
    return true;
}
#endif

// -----------------------------
// Public API (modem.h)
// -----------------------------
bool modem_init_early()
{
    static bool done = false;
    if (done) return true;
    done = true;

#if defined(VST_BOARD_7070)
    Serial.println("📡 modem_init_early (7070, no PMU)...");
#elif defined(VST_BOARD_7080)
    Serial.println("📡 modem_init_early (7080, PMU rails)...");
#else
    Serial.println("📡 modem_init_early (unknown board)...");
#endif

#if defined(VST_BOARD_7080)
    // Restore the previously working 7080 PMU logic
    if (!pmu_enable_modem_rails_7080())
        return false;
#endif

    // UART bring-up
    Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
    delay(50);

    Serial.print("📡 Probing AT");
    if (!wait_for_at_ready(30000))
    {
        Serial.println("\n❌ Modem not responding to AT");
        return false;
    }
    Serial.println("\n✅ Modem AT ready");

    // Enable network time sync hints (best-effort)
    modem.sendAT("+CLTS=1");
    modem.waitResponse(2000);

    modem.sendAT("+CTZR=1");
    modem.waitResponse(2000);

    return true;
}

bool modem_get_timestamp(char *out, size_t out_len)
{
    if (!out || out_len < 17)
        return false;

    if (!wait_for_network_registration(60000))
        return false;

    for (int attempt = 0; attempt < 10; attempt++)
    {
        modem.sendAT("+CCLK?");
        if (modem.waitResponse(3000, "+CCLK:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();

            int q1 = line.indexOf('\"');
            int q2 = line.indexOf('\"', q1 + 1);
            if (q1 < 0 || q2 < 0) { delay(1000); continue; }

            String dt = line.substring(q1 + 1, q2);
            if (dt.length() < 17) { delay(1000); continue; }

            int year = 2000 + dt.substring(0, 2).toInt();
            if (!is_plausible_year(year)) { delay(1000); continue; }

            snprintf(out, out_len,
                     "%04d%02d%02d_%02d%02d%02d",
                     year,
                     dt.substring(3,5).toInt(),
                     dt.substring(6,8).toInt(),
                     dt.substring(9,11).toInt(),
                     dt.substring(12,14).toInt(),
                     dt.substring(15,17).toInt());

            return true;
        }
        delay(1000);
    }

    return false;
}

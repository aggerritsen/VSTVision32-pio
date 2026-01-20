// src/modem.cpp (RFC) — 7070 (ESP32 / SIM7070-family) WITHOUT PMU
//
// Goals:
//  - No PMU / no XPowersLib usage at all on 7070
//  - No hardcoded TINY_GSM_MODEM_* here (MUST come from platformio.ini build_flags)
//  - Pins come from config.h
//  - Bring up modem early, acquire network time, and provide timestamp for filenames
//  - Avoid WDT hangs: bounded waits + yields
//
// Expected 7070 wiring (matches your example):
//  - MODEM_PWR  = GPIO4  (power-key control / inverted logic on some boards)
//  - MODEM_RXD  = GPIO26
//  - MODEM_TXD  = GPIO27
//
// Notes:
//  - Some boards need PWRKEY pulse HIGH->LOW with >= 1s HIGH, due to level shifting.
//  - We do BOTH: a "power-on sequence" (HIGH 1s -> LOW) plus a fallback "pulse" on retry.

#include "modem.h"
#include "config.h"

#include <Arduino.h>
#include <time.h>

#ifndef TINY_GSM_RX_BUFFER
#define TINY_GSM_RX_BUFFER 1024
#endif
#include <TinyGsmClient.h>

static TinyGsm modem(Serial1);

// ---------------------------------------------
// Small helpers
// ---------------------------------------------
static bool is_plausible_year(int year)
{
    return (year >= 2020 && year <= 2099);
}

static void yield_brief()
{
    // Helps keep watchdog happy during long loops
    delay(5);
    yield();
}

// The board example:
//   pinMode(PWR_PIN, OUTPUT);
//   digitalWrite(PWR_PIN, HIGH);
//   delay(1000);
//   digitalWrite(PWR_PIN, LOW);
//
// Comment said: "Starting the machine requires at least 1 second of low level,
// and with a level conversion, the levels are opposite"
// In practice on these LilyGO SIM7xxx ESP32 boards, many use HIGH->LOW like above.
// We'll implement that as the primary "power-on sequence".
static void modem_poweron_sequence()
{
    pinMode(MODEM_PWR, OUTPUT);
    // Often safe to start LOW
    digitalWrite(MODEM_PWR, LOW);
    delay(50);

    // Power key sequence (example)
    digitalWrite(MODEM_PWR, HIGH);
    delay(1100);
    digitalWrite(MODEM_PWR, LOW);
    delay(200);
}

// Fallback pulse used during AT retries
static void modem_pwrkey_pulse()
{
    pinMode(MODEM_PWR, OUTPUT);

    // Quick pulse (some boards want a longer "HIGH" press)
    digitalWrite(MODEM_PWR, HIGH);
    delay(1100);
    digitalWrite(MODEM_PWR, LOW);
    delay(300);
}

static bool wait_for_at_ready(uint32_t timeout_ms)
{
    uint32_t start = millis();
    int retry = 0;

    while (millis() - start < timeout_ms)
    {
        if (modem.testAT(1000))
            return true;

        yield_brief();

        // Every ~15 tries, re-pulse PWRKEY
        if (++retry >= 15)
        {
            Serial.println("⚠ AT not ready → PWRKEY pulse");
            modem_pwrkey_pulse();
            retry = 0;
        }
    }
    return false;
}

static bool is_registered_line(const String &line)
{
    // Handles responses like:
    //   +CEREG: 0,1
    //   +CEREG: 0,5
    //   +CREG: 0,1
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
        modem.waitResponse(100); // drain

        modem.sendAT("+CREG?");
        if (modem.waitResponse(2000, "+CREG:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();
            if (is_registered_line(line))
                return true;
        }
        modem.waitResponse(100); // drain

        yield_brief();
        delay(250);
    }
    return false;
}

// Parse SIMCOM +CCLK response:
//   +CCLK: "yy/MM/dd,hh:mm:ss±zz"
// We will extract yy/MM/dd,hh:mm:ss and format to:
//   YYYYMMDD_HHMMSS
static bool parse_cclk_to_timestamp(const String &line, char *out, size_t out_len)
{
    if (!out || out_len < 17) return false;

    int q1 = line.indexOf('\"');
    int q2 = line.indexOf('\"', q1 + 1);
    if (q1 < 0 || q2 < 0) return false;

    String dt = line.substring(q1 + 1, q2);
    // expect at least "yy/MM/dd,hh:mm:ss"
    if (dt.length() < 17) return false;

    int yy = dt.substring(0, 2).toInt();
    int year = 2000 + yy;
    if (!is_plausible_year(year)) return false;

    int mon  = dt.substring(3, 5).toInt();
    int day  = dt.substring(6, 8).toInt();
    int hour = dt.substring(9, 11).toInt();
    int min  = dt.substring(12, 14).toInt();
    int sec  = dt.substring(15, 17).toInt();

    snprintf(out, out_len, "%04d%02d%02d_%02d%02d%02d",
             year, mon, day, hour, min, sec);

    return true;
}

static bool set_system_time_from_timestamp(const char *ts)
{
    // ts format: YYYYMMDD_HHMMSS
    if (!ts) return false;

    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    if (sscanf(ts, "%4d%2d%2d_%2d%2d%2d", &Y, &M, &D, &h, &m, &s) != 6)
        return false;

    struct tm t {};
    t.tm_year = Y - 1900;
    t.tm_mon  = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min  = m;
    t.tm_sec  = s;

    time_t epoch = mktime(&t);
    if (epoch < 1577836800) // 2020-01-01
        return false;

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    return true;
}

// ---------------------------------------------
// Public API (modem.h)
// ---------------------------------------------
bool modem_init_early()
{
    static bool started = false;
    if (started) return true;
    started = true;

    // Start UART for modem
    Serial1.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
    delay(50);

    // Ensure modem is actually on
    Serial.println("📡 modem_init_early (7070, no PMU)...");
    modem_poweron_sequence();

    Serial.print("📡 Probing AT");
    if (!wait_for_at_ready(30000))
    {
        Serial.println("\n❌ Modem not responding to AT");
        return false;
    }
    Serial.println("\n✅ Modem AT ready");

    // Enable network time updates (SIMCOM)
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

    // Wait for registration first (network time typically requires it)
    if (!wait_for_network_registration(60000))
        return false;

    // Try a few times to get +CCLK with plausible year
    for (int attempt = 0; attempt < 10; attempt++)
    {
        modem.sendAT("+CCLK?");
        if (modem.waitResponse(3000, "+CCLK:") == 1)
        {
            String line = modem.stream.readStringUntil('\n');
            line.trim();

            if (parse_cclk_to_timestamp(line, out, out_len))
            {
                // Also set system time here so everything uses it thereafter.
                if (set_system_time_from_timestamp(out))
                {
                    // Log in main.cpp typically, but safe to log here too
                    // (keep it short)
                }
                return true;
            }
        }

        yield_brief();
        delay(1000);
    }

    return false;
}

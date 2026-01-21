// src/main.cpp (RFC)
// - Always initialize MODEM/TIME first, then SD, then VisionAI
// - If VisionAI is missing, DO NOT stall/reset; keep running and retry VisionAI init periodically
// - SD filenames use SYSTEM TIME (set from modem timestamp at boot)

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "modem.h"
#include "sdcard.h"
#include "VisionAI.h"

/* =========================================================
   ACTUATORS (preferred outputs)
   ========================================================= */
static uint32_t led1_until = 0;
static uint32_t led2_until = 0;
static uint32_t led3_until = 0;

static void leds_service()
{
    uint32_t now = millis();
    if (led1_until && now > led1_until) { digitalWrite(LED_PIN_1, LOW); led1_until = 0; }
    if (led2_until && now > led2_until) { digitalWrite(LED_PIN_2, LOW); led2_until = 0; }
    if (led3_until && now > led3_until) { digitalWrite(LED_PIN_3, LOW); led3_until = 0; }
}

static void leds_pulse_for_target(uint8_t target)
{
    uint32_t now = millis();
    // mapping: target 3/2/1 -> LED 1/2/3
    if (target == 3) { digitalWrite(LED_PIN_1, HIGH); led1_until = now + LED_ON_MS; }
    else if (target == 2) { digitalWrite(LED_PIN_2, HIGH); led2_until = now + LED_ON_MS; }
    else if (target == 1) { digitalWrite(LED_PIN_3, HIGH); led3_until = now + LED_ON_MS; }
}

/* =========================================================
   SYSTEM TIME SET (from modem timestamp "YYYYMMDD_HHMMSS")
   ========================================================= */
static bool is_digit(char c) { return (c >= '0' && c <= '9'); }

static bool parse2(const char *p, int &out)
{
    if (!is_digit(p[0]) || !is_digit(p[1])) return false;
    out = (p[0] - '0') * 10 + (p[1] - '0');
    return true;
}

static bool parse4(const char *p, int &out)
{
    if (!is_digit(p[0]) || !is_digit(p[1]) || !is_digit(p[2]) || !is_digit(p[3])) return false;
    out = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
    return true;
}

static bool set_system_time_from_timestamp(const char *ts)
{
    if (!ts || strlen(ts) != 15 || ts[8] != '_') return false;

    int year, mon, day, hour, min, sec;
    if (!parse4(ts + 0, year) ||
        !parse2(ts + 4, mon)  ||
        !parse2(ts + 6, day)  ||
        !parse2(ts + 9, hour) ||
        !parse2(ts + 11, min) ||
        !parse2(ts + 13, sec))
        return false;

    struct tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;
    tm.tm_isdst = -1;

    time_t t = mktime(&tm);
    if (t < 0) return false;

    struct timeval tv{};
    tv.tv_sec = t;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    Serial.print("🕒 SYSTEM TIME SET: ");
    Serial.println(buf);

    return true;
}

static void log_memory()
{
    Serial.printf(
        "heap_free=%u heap_min=%u psram=%s\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        psramFound() ? "YES" : "NO");
}

static char g_timestamp[32] = {0};

static void obtain_modem_timestamp_and_set_time_blocking()
{
    while (true)
    {
        Serial.println("📡 modem_init_early...");
        if (!modem_init_early())
        {
            Serial.println("⚠️ modem_init_early failed; retrying in 5s");
            delay(5000);
            continue;
        }

        Serial.println("🕒 Obtaining modem timestamp (network time)...");
        if (modem_get_timestamp(g_timestamp, sizeof(g_timestamp)))
        {
            Serial.printf("🕒 Modem timestamp: %s\n", g_timestamp);

            if (!set_system_time_from_timestamp(g_timestamp))
            {
                Serial.println("⚠️ Failed to set system time from modem timestamp; retrying in 5s");
                delay(5000);
                continue;
            }

            sdcard_set_time_valid(true);
            Serial.println("[PHASE 1] DONE");
            return;
        }

        Serial.println("⚠️ No modem timestamp yet; retrying in 5s");
        delay(5000);
    }
}

/* =========================================================
   VisionAI init retry (non-fatal)
   ========================================================= */
static bool g_vision_ok = false;
static uint32_t g_next_vision_retry_ms = 0;
static constexpr uint32_t VISION_RETRY_MS = 3000;

static void try_visionai_begin_now()
{
    Serial.println("[PHASE 3] VisionAI INIT");
    if (VisionAI::begin())
    {
        Serial.println("[PHASE 3] DONE");
        Serial.println("✅ VisionAI ready");
        g_vision_ok = true;
    }
    else
    {
        Serial.println("⚠️ VisionAI not detected/ready (continuing without it)");
        g_vision_ok = false;
        g_next_vision_retry_ms = millis() + VISION_RETRY_MS;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000); // give monitor time to attach
    Serial.println();
    Serial.println("=======================================");
    Serial.println(" VSTPRO BOOT");
    Serial.println("=======================================");

    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);
    digitalWrite(LED_PIN_1, LOW);
    digitalWrite(LED_PIN_2, LOW);
    digitalWrite(LED_PIN_3, LOW);

    // IMPORTANT ORDER:
    // 1) MODEM/TIME
    Serial.println("[PHASE 1] MODEM + TIME");
    obtain_modem_timestamp_and_set_time_blocking();

    // 2) SD
    Serial.println("[PHASE 2] SD INIT");
    bool sd_ok = sdcard_init();
    Serial.printf("[PHASE 2] DONE (sd_ok=%s)\n", sd_ok ? "true" : "false");

    // 3) VisionAI (non-fatal if missing)
    try_visionai_begin_now();

    Serial.println("✅ SETUP COMPLETE -> entering loop()");
    log_memory();
}

void loop()
{
    leds_service();

    // If VisionAI is not ready, keep the system alive and retry periodically.
    if (!g_vision_ok)
    {
        uint32_t now = millis();
        if (g_next_vision_retry_ms && (int32_t)(now - g_next_vision_retry_ms) >= 0)
        {
            Serial.println("♻️ Retrying VisionAI::begin...");
            try_visionai_begin_now();
        }
        delay(50);
        return;
    }

    VisionAI::LoopResult r = VisionAI::loop_once();

    if (r.ok)
    {
        // pulse LEDs for detected targets
        for (size_t i = 0; i < r.box_count; i++)
        {
            leds_pulse_for_target(r.targets[i]);
        }

        // Save JPEG (kept enabled for debugging)
        if (sdcard_available() && r.jpeg && r.jpeg_len)
        {
            (void)sdcard_save_jpeg(r.frame_id, r.jpeg, r.jpeg_len);
        }
    }
    else
    {
        // SSCMA stalled / error -> best-effort reinit
        (void)VisionAI::reinit();
        delay(250);
    }

    if (r.jpeg) free(r.jpeg);

    delay(10);
}

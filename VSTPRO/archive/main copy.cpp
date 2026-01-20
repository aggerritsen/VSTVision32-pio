#include <Arduino.h>
#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include <esp_heap_caps.h>
#include "esp_crc.h"

SSCMA AI;

/* ================================
   ACTUATORS
   ================================ */
static constexpr int LED_PIN_1 = 1;   // GPIO1
static constexpr int LED_PIN_2 = 2;   // GPIO2
static constexpr int LED_PIN_3 = 3;   // GPIO3

static constexpr uint32_t LED_ON_MS = 1000;
static uint32_t led1_until = 0;
static uint32_t led2_until = 0;
static uint32_t led3_until = 0;

/* ================================
   I2C (locked to your wiring)
   ================================ */
static constexpr int I2C_SDA = 3;   // GPIO03 (P1.7)
static constexpr int I2C_SCL = 8;   // GPIO08 (P1.6)
static constexpr uint32_t I2C_HZ = 400000;

/* ================================
   OUTPUT OPTIONS
   ================================ */
static constexpr bool PRINT_IMAGE_HEX_PREVIEW = true;
static constexpr size_t IMAGE_HEX_PREVIEW_BYTES = 64;

/* ================================
   INVOKE / BACKOFF (production-like)
   ================================ */
// Observed: rc=3 behaves like BUSY / NOT READY
static constexpr int RC_BUSY = 3;

// How long we are willing to wait for a frame before declaring a stall (ms)
static constexpr uint32_t INVOKE_DEADLINE_MS = 25000;

// Backoff behavior when busy
static constexpr uint32_t BACKOFF_START_MS = 30;
static constexpr uint32_t BACKOFF_MAX_MS   = 1200;
static constexpr uint8_t  BACKOFF_MULT_NUM = 3;   // multiply by 1.5 (3/2)
static constexpr uint8_t  BACKOFF_MULT_DEN = 2;

// When we get a successful frame, we reset backoff to this small delay
static constexpr uint32_t BACKOFF_RESET_MS = 30;

// Minimum idle between successful frames (prevents hammering & reduces jitter)
static constexpr uint32_t POST_SUCCESS_IDLE_MS = 10;

/* Recovery policy */
static constexpr uint32_t STALL_REINIT_COOLDOWN_MS = 1500;

/* ================================
   STATE
   ================================ */
static uint32_t frame_id = 0;

static String   cached_inf;
static String   cached_image;
static size_t   cached_image_len = 0;
static uint32_t cached_image_crc = 0;

/* Timing */
static uint32_t last_frame_ms = 0;

/* Backoff state */
static uint32_t backoff_ms = BACKOFF_RESET_MS;

/* ================================
   UTIL
   ================================ */
static void log_memory()
{
    Serial.printf(
        "heap_free=%u heap_min=%u psram=%s\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        psramFound() ? "YES" : "NO"
    );
}

static void print_hex_preview(const uint8_t *buf, size_t len, size_t max_bytes)
{
    size_t n = (len < max_bytes) ? len : max_bytes;
    for (size_t i = 0; i < n; i++)
    {
        if (i && (i % 16 == 0)) Serial.println();
        Serial.printf("%02X ", buf[i]);
    }
    if (len > n) Serial.print("...");
    Serial.println();
}

static bool looks_like_base64_jpeg(const String &s)
{
    // Common base64 JPEG prefix is "/9j/"
    if (s.length() < 4) return false;
    return (s[0] == '/' && s[1] == '9' && s[2] == 'j' && s[3] == '/');
}

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

    // Keep behavior deterministic (your earlier sketch had a duplicated target==3 branch)
    if (target == 3)
    {
        digitalWrite(LED_PIN_1, HIGH);
        led1_until = now + LED_ON_MS;
    }
    else if (target == 2)
    {
        digitalWrite(LED_PIN_2, HIGH);
        led2_until = now + LED_ON_MS;
    }
    else if (target == 1)
    {
        digitalWrite(LED_PIN_3, HIGH);
        led3_until = now + LED_ON_MS;
    }
}

/* ================================
   RECOVERY
   ================================ */
static bool reinit_sscma()
{
    Serial.println("♻️ Re-initializing SSCMA over I2C...");
    delay(STALL_REINIT_COOLDOWN_MS);

    // Re-run begin on the existing Wire instance
    if (!AI.begin(&Wire))
    {
        Serial.println("❌ SSCMA re-init failed");
        return false;
    }

    Serial.println("✅ SSCMA re-initialized");
    backoff_ms = BACKOFF_RESET_MS;
    return true;
}

/* ================================
   INVOKE WITH BACKOFF + DEADLINE
   ================================ */
static bool invoke_with_backoff()
{
    uint32_t start = millis();
    uint32_t last_busy_log_ms = 0;
    uint32_t busy_count = 0;

    while (true)
    {
        int rc = AI.invoke(1, false, false);
        if (rc == CMD_OK)
        {
            // Success: reset backoff quickly for best throughput
            backoff_ms = BACKOFF_RESET_MS;
            return true;
        }

        if (rc == RC_BUSY)
        {
            busy_count++;

            // Log busy occasionally (not every loop)
            uint32_t now = millis();
            if (now - last_busy_log_ms > 2000)
            {
                Serial.printf("⏳ BUSY (rc=%d) x%lu, backoff=%lums\n",
                              rc, (unsigned long)busy_count, (unsigned long)backoff_ms);
                last_busy_log_ms = now;
            }

            // Service LEDs during waits (keeps loop responsive)
            leds_service();

            delay(backoff_ms);

            // Exponential-ish backoff: backoff *= 1.5 up to max
            uint32_t next = (backoff_ms * BACKOFF_MULT_NUM) / BACKOFF_MULT_DEN;
            backoff_ms = (next > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : next;

            // Deadline check
            if ((millis() - start) > INVOKE_DEADLINE_MS)
            {
                Serial.printf("⚠️ Invoke deadline exceeded (%lums). Busy loops=%lu\n",
                              (unsigned long)INVOKE_DEADLINE_MS,
                              (unsigned long)busy_count);
                return false;
            }

            continue;
        }

        // Real error: log and do a short delay before retrying until deadline
        Serial.printf("❌ AI.invoke failed rc=%d (backoff=%lums)\n", rc, (unsigned long)backoff_ms);
        leds_service();
        delay(backoff_ms);

        uint32_t next = (backoff_ms * BACKOFF_MULT_NUM) / BACKOFF_MULT_DEN;
        backoff_ms = (next > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : next;

        if ((millis() - start) > INVOKE_DEADLINE_MS)
        {
            Serial.printf("⚠️ Invoke deadline exceeded after rc=%d\n", rc);
            return false;
        }
    }
}

/* ================================
   CAPTURE + PRINT
   ================================ */
static bool capture_and_print()
{
    if (!invoke_with_backoff())
        return false;

    uint32_t now_ms = millis();
    uint32_t dt_ms = (last_frame_ms == 0) ? 0 : (now_ms - last_frame_ms);
    last_frame_ms = now_ms;

    frame_id++;

    Serial.println("=======================================");
    Serial.printf("🧠 FRAME %lu", (unsigned long)frame_id);
    if (dt_ms) Serial.printf(" (dt=%lums)", (unsigned long)dt_ms);
    Serial.println();

    Serial.printf("boxes: %u\n", (unsigned)AI.boxes().size());
    Serial.printf(
        "perf: preprocess=%u inference=%u postprocess=%u\n",
        (unsigned)AI.perf().prepocess,
        (unsigned)AI.perf().inference,
        (unsigned)AI.perf().postprocess
    );

    for (size_t i = 0; i < AI.boxes().size(); i++)
    {
        auto &b = AI.boxes()[i];
        Serial.printf(
            "  [%u] target=%u score=%u x=%u y=%u w=%u h=%u\n",
            (unsigned)i,
            b.target,
            b.score,
            b.x,
            b.y,
            b.w,
            b.h
        );

        leds_pulse_for_target(b.target);
    }

    // Compact JSON-ish inference
    cached_inf = "";
    cached_inf += "{\"frame\":";
    cached_inf += frame_id;
    cached_inf += ",\"dt_ms\":";
    cached_inf += dt_ms;
    cached_inf += ",\"perf\":{";
    cached_inf += "\"preprocess\":";
    cached_inf += AI.perf().prepocess;
    cached_inf += ",\"inference\":";
    cached_inf += AI.perf().inference;
    cached_inf += ",\"postprocess\":";
    cached_inf += AI.perf().postprocess;
    cached_inf += "},\"boxes\":[";
    for (size_t i = 0; i < AI.boxes().size(); i++)
    {
        auto &b = AI.boxes()[i];
        if (i) cached_inf += ",";
        cached_inf += "{\"target\":";
        cached_inf += b.target;
        cached_inf += ",\"score\":";
        cached_inf += b.score;
        cached_inf += ",\"x\":";
        cached_inf += b.x;
        cached_inf += ",\"y\":";
        cached_inf += b.y;
        cached_inf += ",\"w\":";
        cached_inf += b.w;
        cached_inf += ",\"h\":";
        cached_inf += b.h;
        cached_inf += "}";
    }
    cached_inf += "]}";

    Serial.println("INF_JSON:");
    Serial.println(cached_inf);

    // Image (base64 string from module)
    cached_image = AI.last_image();
    cached_image_len = cached_image.length();

    cached_image_crc = esp_crc32_le(
        0,
        (const uint8_t*)cached_image.c_str(),
        cached_image_len
    );

    Serial.printf("📷 image: bytes=%u crc=%08lx", (unsigned)cached_image_len, (unsigned long)cached_image_crc);
    if (looks_like_base64_jpeg(cached_image)) Serial.print(" (base64 JPEG)");
    Serial.println();

    if (PRINT_IMAGE_HEX_PREVIEW && cached_image_len > 0)
    {
        Serial.printf("📷 image hex preview (first %u bytes):\n", (unsigned)IMAGE_HEX_PREVIEW_BYTES);
        print_hex_preview(
            (const uint8_t*)cached_image.c_str(),
            cached_image_len,
            IMAGE_HEX_PREVIEW_BYTES
        );
    }

    log_memory();

    // Tiny idle to avoid hammering the bus at maximum tight-loop speed
    if (POST_SUCCESS_IDLE_MS) delay(POST_SUCCESS_IDLE_MS);

    return true;
}

/* ================================
   SETUP
   ================================ */
void setup()
{
    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);

    uint32_t now = millis();
    digitalWrite(LED_PIN_1, HIGH); led1_until = now + LED_ON_MS;
    digitalWrite(LED_PIN_2, HIGH); led2_until = now + LED_ON_MS;
    digitalWrite(LED_PIN_3, HIGH); led3_until = now + LED_ON_MS;

    Serial.begin(115200);
    delay(500);

    Serial.println("=======================================");
    Serial.println(" T-SIM7080G-S3 | SSCMA I2C INFERENCE ");
    Serial.println("=======================================");

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_HZ);

    if (!AI.begin(&Wire))
    {
        Serial.println("❌ SSCMA init failed");
        while (1) delay(100);
    }

    Serial.println("✅ SSCMA initialized over I2C");
    log_memory();
}

/* ================================
   LOOP
   ================================ */
void loop()
{
    leds_service();

    if (!capture_and_print())
    {
        // If we consistently stall, attempt a re-init once per stall
        (void)reinit_sscma();

        // Avoid a tight failure loop if the device is absent
        delay(250);
    }
}

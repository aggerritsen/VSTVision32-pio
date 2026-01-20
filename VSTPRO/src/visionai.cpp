#include "VisionAI.h"

#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include <esp_heap_caps.h>
#include "esp_crc.h"
#include "mbedtls/base64.h"

#include "config.h"

/* =========================================================
   SSCMA I2C (Wire1) — DO NOT TOUCH global Wire (PMU uses it)
   ========================================================= */
static TwoWire WireAI(1);
static SSCMA AI;

/* =========================================================
   INVOKE / BACKOFF
   ========================================================= */
static constexpr int RC_BUSY = 3;
static constexpr uint32_t INVOKE_DEADLINE_MS = 25000;

static constexpr uint32_t BACKOFF_MAX_MS   = 1200;
static constexpr uint8_t  BACKOFF_MULT_NUM = 3;   // *1.5
static constexpr uint8_t  BACKOFF_MULT_DEN = 2;
static constexpr uint32_t BACKOFF_RESET_MS = 30;

static constexpr uint32_t POST_SUCCESS_IDLE_MS = 10;
static constexpr uint32_t STALL_REINIT_COOLDOWN_MS = 1500;

/* =========================================================
   OUTPUT OPTIONS
   ========================================================= */
static constexpr bool PRINT_IMAGE_HEX_PREVIEW = false;
static constexpr size_t IMAGE_HEX_PREVIEW_BYTES = 64;

/* =========================================================
   STATE
   ========================================================= */
static uint32_t frame_id = 0;
static uint32_t last_frame_ms = 0;
static uint32_t backoff_ms = BACKOFF_RESET_MS;

/* =========================================================
   UTIL
   ========================================================= */
static void log_memory()
{
    Serial.printf(
        "heap_free=%u heap_min=%u psram=%s\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        psramFound() ? "YES" : "NO");
}

/* =========================================================
   JPEG sanity + BASE64 decode
   ========================================================= */
static bool jpeg_sanity_check(const uint8_t *buf, size_t len)
{
    if (!buf || len < 4) return false;
    if (buf[0] != 0xFF || buf[1] != 0xD8) return false;

    bool found_sos = false;
    bool found_eoi = false;

    for (size_t i = 2; i + 1 < len; i++) {
        if (buf[i] != 0xFF) continue;
        uint8_t marker = buf[i + 1];
        if (marker == 0x00) continue;
        if (marker == 0xDA) found_sos = true;
        if (marker == 0xD9) { found_eoi = true; break; }
    }
    return found_sos && found_eoi;
}

static bool decode_base64_to_jpeg(const String &b64, uint8_t **out_buf, size_t *out_len)
{
    if (!out_buf || !out_len) return false;
    *out_buf = nullptr;
    *out_len = 0;

    size_t decoded_len = 0;
    int rc = mbedtls_base64_decode(
        nullptr, 0, &decoded_len,
        (const unsigned char*)b64.c_str(),
        b64.length()
    );

    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decoded_len == 0)
        return false;

    uint8_t *buf = (uint8_t*)heap_caps_malloc(decoded_len, MALLOC_CAP_8BIT);
    if (!buf) return false;

    rc = mbedtls_base64_decode(
        buf, decoded_len, out_len,
        (const unsigned char*)b64.c_str(),
        b64.length()
    );

    if (rc != 0 || *out_len == 0) {
        free(buf);
        return false;
    }

    *out_buf = buf;
    return true;
}

/* =========================================================
   INVOKE with backoff
   ========================================================= */
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
            backoff_ms = BACKOFF_RESET_MS;
            return true;
        }

        if (rc == RC_BUSY)
        {
            busy_count++;

            uint32_t now = millis();
            if (now - last_busy_log_ms > 2000)
            {
                Serial.printf("⏳ BUSY (rc=%d) x%lu, backoff=%lums\n",
                              rc, (unsigned long)busy_count, (unsigned long)backoff_ms);
                last_busy_log_ms = now;
            }

            delay(backoff_ms);

            uint32_t next = (backoff_ms * BACKOFF_MULT_NUM) / BACKOFF_MULT_DEN;
            backoff_ms = (next > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : next;

            if ((millis() - start) > INVOKE_DEADLINE_MS)
            {
                Serial.printf("⚠️ Invoke deadline exceeded (%lums). Busy loops=%lu\n",
                              (unsigned long)INVOKE_DEADLINE_MS,
                              (unsigned long)busy_count);
                return false;
            }
            continue;
        }

        Serial.printf("❌ AI.invoke failed rc=%d (backoff=%lums)\n", rc, (unsigned long)backoff_ms);
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

namespace VisionAI {

bool begin()
{
    // Important: do NOT call Wire.begin() here — PMU/modem owns Wire.
    WireAI.begin(AI_I2C_SDA, AI_I2C_SCL);
    WireAI.setClock(AI_I2C_HZ);

    if (!AI.begin(&WireAI))
    {
        Serial.println("❌ SSCMA init failed on Wire1");
        return false;
    }

    Serial.println("✅ SSCMA initialized over Wire1");
    backoff_ms = BACKOFF_RESET_MS;
    frame_id = 0;
    last_frame_ms = 0;
    return true;
}

bool reinit()
{
    Serial.println("♻️ Re-initializing SSCMA over Wire1...");
    delay(STALL_REINIT_COOLDOWN_MS);

    WireAI.end();
    delay(25);

    WireAI.begin(AI_I2C_SDA, AI_I2C_SCL);
    WireAI.setClock(AI_I2C_HZ);

    if (!AI.begin(&WireAI))
    {
        Serial.println("❌ SSCMA re-init failed");
        return false;
    }

    Serial.println("✅ SSCMA re-initialized");
    backoff_ms = BACKOFF_RESET_MS;
    return true;
}

LoopResult loop_once()
{
    LoopResult out;

    if (!invoke_with_backoff())
    {
        out.ok = false;
        return out;
    }

    uint32_t now_ms = millis();
    uint32_t dt_ms = (last_frame_ms == 0) ? 0 : (now_ms - last_frame_ms);
    last_frame_ms = now_ms;

    frame_id++;
    out.frame_id = frame_id;

    Serial.println("=======================================");
    Serial.printf("🧠 FRAME %lu", (unsigned long)frame_id);
    if (dt_ms) Serial.printf(" (dt=%lums)", (unsigned long)dt_ms);
    Serial.println();

    Serial.printf("boxes: %u\n", (unsigned)AI.boxes().size());
    Serial.printf("perf: preprocess=%u inference=%u postprocess=%u\n",
                  (unsigned)AI.perf().prepocess,
                  (unsigned)AI.perf().inference,
                  (unsigned)AI.perf().postprocess);

    out.box_count = 0;
    for (size_t i = 0; i < AI.boxes().size() && i < 16; i++)
    {
        auto &b = AI.boxes()[i];
        Serial.printf("  [%u] target=%u score=%u x=%u y=%u w=%u h=%u\n",
                      (unsigned)i, b.target, b.score, b.x, b.y, b.w, b.h);

        out.targets[out.box_count++] = (uint8_t)b.target;
    }

    // Base64 image
    String b64 = AI.last_image();
    size_t b64_len = b64.length();
    uint32_t b64_crc = esp_crc32_le(0, (const uint8_t*)b64.c_str(), b64_len);

    Serial.printf("📷 image: bytes=%u crc=%08lx\n", (unsigned)b64_len, (unsigned long)b64_crc);

    if (PRINT_IMAGE_HEX_PREVIEW && b64_len > 0)
    {
        Serial.printf("📷 image hex preview (first %u bytes):\n", (unsigned)IMAGE_HEX_PREVIEW_BYTES);
        size_t n = (b64_len < IMAGE_HEX_PREVIEW_BYTES) ? b64_len : IMAGE_HEX_PREVIEW_BYTES;
        for (size_t i = 0; i < n; i++)
        {
            if (i && (i % 16 == 0)) Serial.println();
            Serial.printf("%02X ", (uint8_t)b64[i]);
        }
        if (b64_len > n) Serial.print("...");
        Serial.println();
    }

    // Convert base64 -> jpeg (malloc) (caller frees)
    if (b64_len > 0)
    {
        uint8_t *jpeg = nullptr;
        size_t jpeg_len = 0;

        if (decode_base64_to_jpeg(b64, &jpeg, &jpeg_len) &&
            jpeg_sanity_check(jpeg, jpeg_len))
        {
            out.jpeg = jpeg;
            out.jpeg_len = jpeg_len;
        }
        else
        {
            if (jpeg) free(jpeg);
        }
    }

    log_memory();
    if (POST_SUCCESS_IDLE_MS) delay(POST_SUCCESS_IDLE_MS);

    out.ok = true;
    return out;
}

} // namespace VisionAI

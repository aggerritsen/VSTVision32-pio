#include "VisionAI.h"

#include <Seeed_Arduino_SSCMA.h>
#include <esp_heap_caps.h>
#include "esp_crc.h"

VisionAI::VisionAI() {}

bool VisionAI::begin(TwoWire &wire, const VisionAIConfig &cfg)
{
    cfg_ = cfg;
    wire_ = &wire;

    if (!ai_)
        ai_ = new SSCMA();

    wire_->begin(cfg_.sda, cfg_.scl);
    wire_->setClock(cfg_.i2c_hz);

    backoff_ms_ = cfg_.backoff_reset_ms;
    frame_id_ = 0;
    last_frame_ms_ = 0;

    return ai_->begin(wire_);
}

bool VisionAI::reinit()
{
    if (!ai_ || !wire_) return false;

    Serial.println("♻️ Re-initializing SSCMA over I2C...");
    delay(cfg_.reinit_cooldown_ms);

    if (!ai_->begin(wire_))
    {
        Serial.println("❌ SSCMA re-init failed");
        return false;
    }

    Serial.println("✅ SSCMA re-initialized");
    backoff_ms_ = cfg_.backoff_reset_ms;
    return true;
}

void VisionAI::enableLeds(int led1_pin, int led2_pin, int led3_pin, uint32_t led_on_ms)
{
    leds_enabled_ = true;
    led1_pin_ = led1_pin;
    led2_pin_ = led2_pin;
    led3_pin_ = led3_pin;
    led_on_ms_ = led_on_ms;

    pinMode(led1_pin_, OUTPUT);
    pinMode(led2_pin_, OUTPUT);
    pinMode(led3_pin_, OUTPUT);

    uint32_t now = millis();
    digitalWrite(led1_pin_, HIGH); led1_until_ = now + led_on_ms_;
    digitalWrite(led2_pin_, HIGH); led2_until_ = now + led_on_ms_;
    digitalWrite(led3_pin_, HIGH); led3_until_ = now + led_on_ms_;
}

void VisionAI::serviceLeds()
{
    if (!leds_enabled_) return;

    uint32_t now = millis();
    if (led1_until_ && now > led1_until_) { digitalWrite(led1_pin_, LOW); led1_until_ = 0; }
    if (led2_until_ && now > led2_until_) { digitalWrite(led2_pin_, LOW); led2_until_ = 0; }
    if (led3_until_ && now > led3_until_) { digitalWrite(led3_pin_, LOW); led3_until_ = 0; }
}

bool VisionAI::invokeWithBackoff()
{
    if (!ai_) return false;

    uint32_t start = millis();
    uint32_t last_busy_log_ms = 0;
    uint32_t busy_count = 0;

    // ensure sane start
    if (backoff_ms_ < cfg_.backoff_start_ms)
        backoff_ms_ = cfg_.backoff_start_ms;

    while (true)
    {
        int rc = ai_->invoke(cfg_.task_id, cfg_.invoke_arg2, cfg_.invoke_arg3);
        if (rc == CMD_OK)
        {
            backoff_ms_ = cfg_.backoff_reset_ms;
            return true;
        }

        if (rc == cfg_.rc_busy)
        {
            busy_count++;

            uint32_t now = millis();
            if (cfg_.busy_log_every_ms > 0 && (now - last_busy_log_ms) > cfg_.busy_log_every_ms)
            {
                Serial.printf("⏳ BUSY (rc=%d) x%lu, backoff=%lums\n",
                              rc, (unsigned long)busy_count, (unsigned long)backoff_ms_);
                last_busy_log_ms = now;
            }

            serviceLeds();
            delay(backoff_ms_);

            uint32_t next = (backoff_ms_ * cfg_.backoff_mult_num) / cfg_.backoff_mult_den;
            backoff_ms_ = (next > cfg_.backoff_max_ms) ? cfg_.backoff_max_ms : next;

            if ((millis() - start) > cfg_.invoke_deadline_ms)
            {
                Serial.printf("⚠️ Invoke deadline exceeded (%lums). Busy loops=%lu\n",
                              (unsigned long)cfg_.invoke_deadline_ms,
                              (unsigned long)busy_count);
                return false;
            }
            continue;
        }

        // non-busy error
        Serial.printf("❌ AI.invoke failed rc=%d (backoff=%lums)\n", rc, (unsigned long)backoff_ms_);
        serviceLeds();
        delay(backoff_ms_);

        uint32_t next = (backoff_ms_ * cfg_.backoff_mult_num) / cfg_.backoff_mult_den;
        backoff_ms_ = (next > cfg_.backoff_max_ms) ? cfg_.backoff_max_ms : next;

        if ((millis() - start) > cfg_.invoke_deadline_ms)
        {
            Serial.printf("⚠️ Invoke deadline exceeded after rc=%d\n", rc);
            return false;
        }
    }
}

bool VisionAI::capture(VisionFrame &out)
{
    serviceLeds();

    if (!invokeWithBackoff())
        return false;

    uint32_t now_ms = millis();
    out.dt_ms = (last_frame_ms_ == 0) ? 0 : (now_ms - last_frame_ms_);
    last_frame_ms_ = now_ms;

    frame_id_++;
    out.frame_id = frame_id_;

    out.perf.preprocess  = (uint16_t)ai_->perf().prepocess;
    out.perf.inference   = (uint16_t)ai_->perf().inference;
    out.perf.postprocess = (uint16_t)ai_->perf().postprocess;

    fillBoxesAndActuate(out);

    out.image_b64 = ai_->last_image();
    out.image_len = out.image_b64.length();
    out.image_is_base64_jpeg = looksLikeBase64Jpeg(out.image_b64);

    out.image_crc32 = esp_crc32_le(
        0,
        (const uint8_t*)out.image_b64.c_str(),
        out.image_len
    );

    buildInfJson(out);

    if (cfg_.post_success_idle_ms)
        delay(cfg_.post_success_idle_ms);

    return true;
}

void VisionAI::fillBoxesAndActuate(VisionFrame &out)
{
    out.box_count = 0;

    auto &boxes = ai_->boxes();
    size_t n = boxes.size();
    if (n > VisionFrame::MAX_BOXES) n = VisionFrame::MAX_BOXES;

    uint32_t now = millis();

    for (size_t i = 0; i < n; i++)
    {
        const auto &b = boxes[i];

        out.boxes[i].target = b.target;
        out.boxes[i].score  = b.score;
        out.boxes[i].x      = b.x;
        out.boxes[i].y      = b.y;
        out.boxes[i].w      = b.w;
        out.boxes[i].h      = b.h;
        out.box_count++;

        if (leds_enabled_)
        {
            if (b.target == 3)
            {
                digitalWrite(led1_pin_, HIGH);
                led1_until_ = now + led_on_ms_;
            }
            else if (b.target == 2)
            {
                digitalWrite(led2_pin_, HIGH);
                led2_until_ = now + led_on_ms_;
            }
            else if (b.target == 1)
            {
                digitalWrite(led3_pin_, HIGH);
                led3_until_ = now + led_on_ms_;
            }
        }
    }
}

void VisionAI::buildInfJson(VisionFrame &out) const
{
    out.inf_json = "";
    out.inf_json += "{\"frame\":";
    out.inf_json += out.frame_id;
    out.inf_json += ",\"dt_ms\":";
    out.inf_json += out.dt_ms;
    out.inf_json += ",\"perf\":{";
    out.inf_json += "\"preprocess\":";
    out.inf_json += out.perf.preprocess;
    out.inf_json += ",\"inference\":";
    out.inf_json += out.perf.inference;
    out.inf_json += ",\"postprocess\":";
    out.inf_json += out.perf.postprocess;
    out.inf_json += "},\"boxes\":[";

    for (size_t i = 0; i < out.box_count; i++)
    {
        const auto &b = out.boxes[i];
        if (i) out.inf_json += ",";
        out.inf_json += "{\"target\":";
        out.inf_json += b.target;
        out.inf_json += ",\"score\":";
        out.inf_json += b.score;
        out.inf_json += ",\"x\":";
        out.inf_json += b.x;
        out.inf_json += ",\"y\":";
        out.inf_json += b.y;
        out.inf_json += ",\"w\":";
        out.inf_json += b.w;
        out.inf_json += ",\"h\":";
        out.inf_json += b.h;
        out.inf_json += "}";
    }
    out.inf_json += "]}";
}

void VisionAI::printFrame(const VisionFrame &f) const
{
    Serial.println("=======================================");
    Serial.printf("🧠 FRAME %lu", (unsigned long)f.frame_id);
    if (f.dt_ms) Serial.printf(" (dt=%lums)", (unsigned long)f.dt_ms);
    Serial.println();

    Serial.printf("boxes: %u\n", (unsigned)f.box_count);
    Serial.printf("perf: preprocess=%u inference=%u postprocess=%u\n",
                  (unsigned)f.perf.preprocess,
                  (unsigned)f.perf.inference,
                  (unsigned)f.perf.postprocess);

    for (size_t i = 0; i < f.box_count; i++)
    {
        const auto &b = f.boxes[i];
        Serial.printf("  [%u] target=%u score=%u x=%u y=%u w=%u h=%u\n",
                      (unsigned)i,
                      b.target, b.score, b.x, b.y, b.w, b.h);
    }

    Serial.println("INF_JSON:");
    Serial.println(f.inf_json);

    Serial.printf("📷 image: bytes=%u crc=%08lx", (unsigned)f.image_len, (unsigned long)f.image_crc32);
    if (f.image_is_base64_jpeg) Serial.print(" (base64 JPEG)");
    Serial.println();

    if (cfg_.print_image_hex_preview && f.image_len > 0)
    {
        Serial.printf("📷 image hex preview (first %u bytes):\n", (unsigned)cfg_.image_hex_preview_bytes);
        printHexPreview(
            (const uint8_t*)f.image_b64.c_str(),
            f.image_len,
            cfg_.image_hex_preview_bytes
        );
    }

    Serial.printf("heap_free=%u heap_min=%u psram=%s\n",
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  psramFound() ? "YES" : "NO");
}

/* static */ bool VisionAI::looksLikeBase64Jpeg(const String &s)
{
    if (s.length() < 4) return false;
    return (s[0] == '/' && s[1] == '9' && s[2] == 'j' && s[3] == '/');
}

/* static */ void VisionAI::printHexPreview(const uint8_t *buf, size_t len, size_t max_bytes)
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

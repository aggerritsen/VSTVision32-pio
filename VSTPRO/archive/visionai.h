#pragma once
#include <Arduino.h>
#include <Wire.h>

class SSCMA;

struct VisionBox
{
    uint8_t  target;
    uint8_t  score;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
};

struct VisionPerf
{
    uint16_t preprocess;
    uint16_t inference;
    uint16_t postprocess;
};

struct VisionFrame
{
    uint32_t frame_id = 0;
    uint32_t dt_ms = 0;

    VisionPerf perf{};

    static constexpr size_t MAX_BOXES = 16;
    VisionBox boxes[MAX_BOXES]{};
    size_t box_count = 0;

    String image_b64;
    size_t image_len = 0;
    uint32_t image_crc32 = 0;
    bool image_is_base64_jpeg = false;

    String inf_json;
};

struct VisionAIConfig
{
    // I2C
    int sda = 3;
    int scl = 8;
    uint32_t i2c_hz = 400000;

    // invoke(task_id, arg2, arg3) kept compatible with your current usage
    int task_id = 1;
    bool invoke_arg2 = false;
    bool invoke_arg3 = false;

    // Busy/backoff
    int rc_busy = 3;
    uint32_t invoke_deadline_ms = 25000;

    uint32_t backoff_start_ms = 30;
    uint32_t backoff_reset_ms = 30;
    uint32_t backoff_max_ms   = 1200;
    uint8_t  backoff_mult_num = 3;   // *= 1.5 (3/2)
    uint8_t  backoff_mult_den = 2;

    // Recovery
    uint32_t reinit_cooldown_ms = 1500;

    // Logging
    bool   print_image_hex_preview = true;
    size_t image_hex_preview_bytes = 64;
    uint32_t post_success_idle_ms = 10;
    uint32_t busy_log_every_ms = 2000;
};

class VisionAI
{
public:
    VisionAI();

    bool begin(TwoWire &wire, const VisionAIConfig &cfg);
    bool capture(VisionFrame &out);
    bool reinit();

    void printFrame(const VisionFrame &f) const;

    // Optional LEDs
    void enableLeds(int led1_pin, int led2_pin, int led3_pin, uint32_t led_on_ms = 1000);
    void serviceLeds();

private:
    bool invokeWithBackoff();
    void fillBoxesAndActuate(VisionFrame &out);   // <-- NOT const (it may update LED timing)
    void buildInfJson(VisionFrame &out) const;

    static bool looksLikeBase64Jpeg(const String &s);
    static void printHexPreview(const uint8_t *buf, size_t len, size_t max_bytes);

private:
    TwoWire *wire_ = nullptr;
    VisionAIConfig cfg_{};

    SSCMA *ai_ = nullptr;

    uint32_t frame_id_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t backoff_ms_ = 0;

    // LEDs (optional)
    bool leds_enabled_ = false;
    int led1_pin_ = -1, led2_pin_ = -1, led3_pin_ = -1;
    uint32_t led_on_ms_ = 1000;
    uint32_t led1_until_ = 0, led2_until_ = 0, led3_until_ = 0;
};

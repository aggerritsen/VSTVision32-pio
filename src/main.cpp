#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "driver/uart.h"
#include "esp_crc.h"
#include "mbedtls/base64.h"

#include "sdcard.h"
#include "modem.h"

/* =========================================================
   BROKER UART CONFIG (XIAO ↔ T-SIM)
   UART2 is used exclusively for broker traffic
   ========================================================= */
static constexpr uart_port_t BROKER_UART = UART_NUM_2;
static constexpr int BROKER_RX_PIN = 18;
static constexpr int BROKER_TX_PIN = 17;
static constexpr int BROKER_BAUD   = 921600;
static constexpr int BROKER_BUF_SZ = 4096;

/* =========================================================
   RX STATE MACHINE
   ========================================================= */
enum RxState {
    WAIT_JSON,
    WAIT_IMAGE_HEADER,
    READ_IMAGE,
    WAIT_END
};

static RxState rx_state = WAIT_JSON;

/* =========================================================
   FRAME DATA
   ========================================================= */
static String   json_buffer;
static String   image_base64;
static size_t   image_expected_len = 0;
static uint32_t image_expected_crc = 0;
static uint32_t frame_id = 0;

/* Timestamp buffer */
static char g_timestamp[32] = {0};

/* =========================================================
   UTIL
   ========================================================= */
static void reset_frame()
{
    json_buffer = "";
    image_base64 = "";
    image_expected_len = 0;
    image_expected_crc = 0;
    rx_state = WAIT_JSON;
}

/* =========================================================
   SYSTEM TIME SET (robust & minimal)
   Expected format: YYYYMMDD_HHMMSS
   ========================================================= */
static bool set_system_time_from_timestamp(const char *ts)
{
    if (!ts || strlen(ts) < 15) {
        Serial.println("❌ Invalid timestamp string");
        return false;
    }

    struct tm tm {};
    tm.tm_year = atoi(ts + 0) - 1900;
    tm.tm_mon  = atoi(ts + 4) - 1;
    tm.tm_mday = atoi(ts + 6);
    tm.tm_hour = atoi(ts + 9);
    tm.tm_min  = atoi(ts + 11);
    tm.tm_sec  = atoi(ts + 13);
    tm.tm_isdst = 0;

    time_t t = mktime(&tm);
    if (t <= 0) {
        Serial.println("❌ mktime() failed, system time not set");
        return false;
    }

    struct timeval tv {
        .tv_sec = t,
        .tv_usec = 0
    };
    settimeofday(&tv, nullptr);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    Serial.print("🕒 SYSTEM TIME SET: ");
    Serial.println(buf);

    return true;
}

/* =========================================================
   JPEG SANITY CHECK
   ========================================================= */
static bool jpeg_sanity_check(const uint8_t *buf, size_t len)
{
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        Serial.println("❌ JPEG sanity: missing SOI");
        return false;
    }

    bool found_sos = false;
    bool found_eoi = false;

    for (size_t i = 2; i + 1 < len; i++) {
        if (buf[i] != 0xFF) continue;

        uint8_t marker = buf[i + 1];
        if (marker == 0x00) continue;

        if (marker == 0xDA) found_sos = true;
        if (marker == 0xD9) { found_eoi = true; break; }
    }

    if (!found_sos || !found_eoi) {
        Serial.println("❌ JPEG sanity: invalid marker structure");
        return false;
    }

    Serial.println("✅ JPEG sanity: marker structure OK");
    return true;
}

/* =========================================================
   BASE64 → JPEG DECODE
   ========================================================= */
static bool decode_base64_to_jpeg(
    const String &b64,
    uint8_t **out_buf,
    size_t *out_len
)
{
    size_t decoded_len = 0;

    int rc = mbedtls_base64_decode(
        nullptr, 0, &decoded_len,
        (const unsigned char*)b64.c_str(),
        b64.length()
    );

    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return false;

    uint8_t *buf = (uint8_t*)heap_caps_malloc(decoded_len, MALLOC_CAP_8BIT);
    if (!buf) return false;

    rc = mbedtls_base64_decode(
        buf, decoded_len, out_len,
        (const unsigned char*)b64.c_str(),
        b64.length()
    );

    if (rc != 0) {
        free(buf);
        return false;
    }

    *out_buf = buf;
    return true;
}

/* =========================================================
   BROKER UART INIT
   ========================================================= */
static void broker_uart_init()
{
    uart_config_t cfg {
        .baud_rate = BROKER_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_driver_install(BROKER_UART, BROKER_BUF_SZ, BROKER_BUF_SZ, 0, nullptr, 0);
    uart_param_config(BROKER_UART, &cfg);
    uart_set_pin(BROKER_UART, BROKER_TX_PIN, BROKER_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.printf(
        "UART2 broker configured RX=%d TX=%d BAUD=%d\n",
        BROKER_RX_PIN, BROKER_TX_PIN, BROKER_BAUD
    );
}

/* =========================================================
   SETUP
   ========================================================= */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println("=======================================");
    Serial.println(" T-SIM7080G-S3 | SSCMA UART RECEIVER ");
    Serial.println("=======================================");

    /* 1️⃣ MODEM + TIME (FIRST, BLOCKING, BEST EFFORT) */
    Serial.println("📡 Modem early init (PMU + AT)");
    if (modem_init_early()) {
        if (modem_get_timestamp(g_timestamp, sizeof(g_timestamp))) {
            Serial.printf("🕒 Modem timestamp: %s\n", g_timestamp);
            set_system_time_from_timestamp(g_timestamp);
        } else {
            Serial.println("⚠ Modem time unavailable");
        }
    } else {
        Serial.println("⚠ Modem init failed");
    }

    /* 2️⃣ BROKER UART */
    broker_uart_init();

    /* 3️⃣ SD CARD */
    sdcard_init();
}

/* =========================================================
   LOOP
   ========================================================= */
void loop()
{
    uint8_t c;
    if (uart_read_bytes(BROKER_UART, &c, 1, 20 / portTICK_PERIOD_MS) <= 0)
        return;

    static String line;

    if (rx_state == READ_IMAGE) {
        image_base64 += (char)c;
        if (image_base64.length() >= image_expected_len)
            rx_state = WAIT_END;
        return;
    }

    if (c != '\n') {
        line += (char)c;
        return;
    }

    line.trim();

    if (rx_state == WAIT_JSON && line.startsWith("JSON ")) {
        json_buffer = line.substring(5);
        int idx = json_buffer.indexOf("\"frame\":");
        if (idx >= 0) frame_id = json_buffer.substring(idx + 8).toInt();

        Serial.println("🧠 INFERENCE");
        Serial.printf("Frame      : %lu\n", frame_id);
        Serial.println(json_buffer);

        rx_state = WAIT_IMAGE_HEADER;
    }
    else if (rx_state == WAIT_IMAGE_HEADER && line.startsWith("IMAGE ")) {
        sscanf(line.c_str(), "IMAGE %zu %lx",
               &image_expected_len, &image_expected_crc);

        image_base64.reserve(image_expected_len);
        rx_state = READ_IMAGE;
    }
    else if (rx_state == WAIT_END && line == "END") {
        uint32_t crc = esp_crc32_le(
            0,
            (const uint8_t*)image_base64.c_str(),
            image_base64.length()
        );

        if (crc == image_expected_crc) {
            uint8_t *jpeg = nullptr;
            size_t jpeg_len = 0;

            if (decode_base64_to_jpeg(image_base64, &jpeg, &jpeg_len) &&
                jpeg_sanity_check(jpeg, jpeg_len) &&
                sdcard_available())
            {
                sdcard_save_jpeg(frame_id, jpeg, jpeg_len);
            }

            free(jpeg);
            String ack = "ACK " + String(frame_id) + "\n";
            uart_write_bytes(BROKER_UART, ack.c_str(), ack.length());
        }

        reset_frame();
    }

    line = "";
}

#include <Arduino.h>
#include "driver/uart.h"
#include "esp_crc.h"
#include "sdcard.h"

/* =============================
   UART CONFIG
   ============================= */
static constexpr uart_port_t UART_PORT = UART_NUM_1;
static constexpr int UART_RX_PIN = 18;
static constexpr int UART_TX_PIN = 17;
static constexpr int UART_BAUD   = 921600;
static constexpr int UART_BUF_SZ = 4096;

/* =============================
   RX STATE
   ============================= */
enum RxState {
    WAIT_JSON,
    WAIT_IMAGE_HEADER,
    READ_IMAGE,
    WAIT_END
};

static RxState rx_state = WAIT_JSON;

/* =============================
   FRAME DATA
   ============================= */
static String json_buffer;
static String image_base64;

static size_t   image_expected_len = 0;
static uint32_t image_expected_crc = 0;
static uint32_t frame_id = 0;

/* =============================
   UTIL
   ============================= */
void reset_frame()
{
    json_buffer = "";
    image_base64 = "";
    image_expected_len = 0;
    image_expected_crc = 0;
    rx_state = WAIT_JSON;
}

/* =============================
   SETUP
   ============================= */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println("=======================================");
    Serial.println(" T-SIM7080G-S3 | SSCMA UART RECEIVER ");
    Serial.println("=======================================");

    /* ---------- UART (IDF DRIVER) ---------- */
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_driver_install(UART_PORT, UART_BUF_SZ, UART_BUF_SZ, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(
        UART_PORT,
        UART_TX_PIN,
        UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    Serial.println("UART1 configured (IDF driver)");

    /* ---------- SD CARD (ABSTRACTION ONLY) ---------- */
    if (!sdcard_init())
    {
        Serial.println("⚠ SD card not available — continuing without storage");
    }
    else
    {
        Serial.printf(
            "📊 SD usage: %llu / %llu bytes\n",
            sdcard_used_bytes(),
            sdcard_total_bytes()
        );
    }
}

/* =============================
   LOOP
   ============================= */
void loop()
{
    uint8_t c;
    int len = uart_read_bytes(
        UART_PORT,
        &c,
        1,
        20 / portTICK_PERIOD_MS
    );

    if (len <= 0)
        return;

    static String line;

    /* -------- IMAGE BODY -------- */
    if (rx_state == READ_IMAGE)
    {
        image_base64 += (char)c;

        if (image_base64.length() >= image_expected_len)
        {
            Serial.printf(
                "🖼 Image received (%u bytes)\n",
                image_base64.length()
            );
            rx_state = WAIT_END;
        }
        return;
    }

    /* -------- LINE MODE -------- */
    if (c == '\n')
    {
        line.trim();

        if (rx_state == WAIT_JSON && line.startsWith("JSON "))
        {
            json_buffer = line.substring(5);

            int idx = json_buffer.indexOf("\"frame\":");
            if (idx >= 0)
                frame_id = json_buffer.substring(idx + 8).toInt();

            Serial.printf("📦 JSON received (frame %lu)\n", frame_id);
            rx_state = WAIT_IMAGE_HEADER;
        }
        else if (rx_state == WAIT_IMAGE_HEADER && line.startsWith("IMAGE "))
        {
            sscanf(
                line.c_str(),
                "IMAGE %zu %lx",
                &image_expected_len,
                &image_expected_crc
            );

            image_base64.reserve(image_expected_len);

            Serial.printf(
                "📸 IMAGE header: len=%u crc=%08lx\n",
                image_expected_len,
                image_expected_crc
            );

            rx_state = READ_IMAGE;
        }
        else if (rx_state == WAIT_END && line == "END")
        {
            uint32_t crc = esp_crc32_le(
                0,
                (const uint8_t*)image_base64.c_str(),
                image_base64.length()
            );

            Serial.println("=================================");
            Serial.printf("FRAME %lu COMPLETE\n", frame_id);
            Serial.printf("CRC expected: %08lx\n", image_expected_crc);
            Serial.printf("CRC computed: %08lx\n", crc);

            if (crc == image_expected_crc)
            {
                Serial.println("✅ CRC OK → ACK sent");
                String ack = "ACK " + String(frame_id) + "\n";
                uart_write_bytes(UART_PORT, ack.c_str(), ack.length());
            }
            else
            {
                Serial.println("❌ CRC FAIL → NACK sent");
                String nack = "NACK " + String(frame_id) + "\n";
                uart_write_bytes(UART_PORT, nack.c_str(), nack.length());
            }

            reset_frame();
        }

        line = "";
    }
    else
    {
        line += (char)c;
    }
}

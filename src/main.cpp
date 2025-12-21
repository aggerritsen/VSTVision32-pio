#include <Arduino.h>
#include "driver/uart.h"
#include "esp_crc.h"
#include "mbedtls/base64.h"
#include "sdcard.h"
#include "modem.h"


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
static String   json_buffer;
static String   image_base64;
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

bool jpeg_sanity_check(const uint8_t *buf, size_t len)
{
    if (len < 4)
        return false;

    // SOI
    if (buf[0] != 0xFF || buf[1] != 0xD8)
    {
        Serial.println("❌ JPEG sanity: missing SOI");
        return false;
    }

    bool found_sos = false;
    bool found_eoi = false;

    size_t i = 2;

    while (i + 1 < len)
    {
        if (buf[i] != 0xFF)
        {
            i++;
            continue;
        }

        uint8_t marker = buf[i + 1];

        // Stuffed byte (FF 00)
        if (marker == 0x00)
        {
            i += 2;
            continue;
        }

        // EOI
        if (marker == 0xD9)
        {
            found_eoi = true;
            break;
        }

        // SOS (start of scan)
        if (marker == 0xDA)
        {
            found_sos = true;
            i += 2;

            // Scan entropy data until EOI
            while (i + 1 < len)
            {
                if (buf[i] == 0xFF && buf[i + 1] == 0xD9)
                {
                    found_eoi = true;
                    break;
                }
                i++;
            }
            break;
        }

        // Markers with length field
        if (i + 3 >= len)
        {
            Serial.println("❌ JPEG sanity: truncated marker");
            return false;
        }

        uint16_t seg_len = (buf[i + 2] << 8) | buf[i + 3];
        if (seg_len < 2)
        {
            Serial.println("❌ JPEG sanity: invalid segment length");
            return false;
        }

        i += 2 + seg_len;
    }

    if (!found_sos)
    {
        Serial.println("❌ JPEG sanity: missing SOS");
        return false;
    }

    if (!found_eoi)
    {
        Serial.println("❌ JPEG sanity: missing EOI");
        return false;
    }

    Serial.println("✅ JPEG sanity: marker structure OK");
    return true;
}


/* =============================
   BASE64 → JPEG DECODE
   ============================= */
bool decode_base64_to_jpeg(
    const String &b64,
    uint8_t **out_buf,
    size_t *out_len
)
{
    size_t decoded_len = 0;

    int rc = mbedtls_base64_decode(
        nullptr,
        0,
        &decoded_len,
        (const unsigned char *)b64.c_str(),
        b64.length()
    );

    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        Serial.printf("❌ Base64 size calc failed (%d)\n", rc);
        return false;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(
        decoded_len,
        MALLOC_CAP_8BIT
    );

    if (!buf)
    {
        Serial.println("❌ JPEG buffer alloc failed");
        return false;
    }

    rc = mbedtls_base64_decode(
        buf,
        decoded_len,
        out_len,
        (const unsigned char *)b64.c_str(),
        b64.length()
    );

    if (rc != 0)
    {
        Serial.printf("❌ Base64 decode failed (%d)\n", rc);
        free(buf);
        return false;
    }

    *out_buf = buf;
    return true;
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

    char ts[32];

    if (!modem_init_early()) {
        Serial.println("⚠ Modem init failed");
    }

    if (modem_get_timestamp(ts, sizeof(ts))) {
        Serial.print("🕒 Timestamp: ");
        Serial.println(ts);
    }


    // --- existing UART config (UNCHANGED) ---
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
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

    // --- SD init stays as-is ---
    sdcard_init();
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
            Serial.printf("🖼 Image received (%u bytes)\n", image_base64.length());
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
                Serial.println("✅ CRC OK");

                uint8_t *jpeg_buf = nullptr;
                size_t jpeg_len = 0;

                if (decode_base64_to_jpeg(image_base64, &jpeg_buf, &jpeg_len))
                {
                    Serial.printf("🧩 JPEG decoded: %u bytes\n", jpeg_len);

                    if (jpeg_len >= 3)
                    {
                        Serial.printf(
                            "🧪 JPEG magic: %02X %02X %02X\n",
                            jpeg_buf[0],
                            jpeg_buf[1],
                            jpeg_buf[2]
                        );
                    }

                    jpeg_sanity_check(jpeg_buf, jpeg_len);
                    
                    if (sdcard_available())
                        sdcard_save_jpeg(frame_id, jpeg_buf, jpeg_len);

                    free(jpeg_buf);
                }

                String ack = "ACK " + String(frame_id) + "\n";
                uart_write_bytes(UART_PORT, ack.c_str(), ack.length());
            }
            else
            {
                Serial.println("❌ CRC FAIL");
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

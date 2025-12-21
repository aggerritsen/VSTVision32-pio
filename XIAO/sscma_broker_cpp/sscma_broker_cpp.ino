#include <Arduino.h>
#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include <esp_heap_caps.h>
#include "esp_crc.h"

SSCMA AI;

/* ================================
   UART CONFIG (XIAO → T-SIM)
   ================================ */
static constexpr uint32_t UART_BAUD = 921600;
static constexpr int UART_TX_PIN = 43;
static constexpr int UART_RX_PIN = 44;

HardwareSerial BrokerUART(1);

/* ================================
   TRANSPORT
   ================================ */
static constexpr uint32_t ACK_TIMEOUT_MS = 5000;

/* ================================
   STATE
   ================================ */
static uint32_t frame_id = 0;
static bool     awaiting_ack = false;
static uint32_t last_send_ms = 0;

/* Cached frame (for resend) */
static String cached_json;
static String cached_inf;          // ✅ NEW
static String cached_image;
static size_t cached_image_len = 0;
static uint32_t cached_image_crc = 0;

/* ================================
   UTIL
   ================================ */
void log_memory()
{
    Serial.printf(
        "heap_free=%u heap_min=%u psram=%s\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        psramFound() ? "YES" : "NO"
    );
}

/* ================================
   SEND FRAME (CACHED)
   ================================ */
void send_cached_frame()
{
    /* JSON (unchanged, backward compatible) */
    BrokerUART.print("JSON ");
    BrokerUART.println(cached_json);

    /* INF (new, inference-only) */
    BrokerUART.print("INF ");
    BrokerUART.println(cached_inf);

    /* IMAGE */
    BrokerUART.print("IMAGE ");
    BrokerUART.print(cached_image_len);
    BrokerUART.print(" ");
    BrokerUART.printf("%08lx\n", cached_image_crc);
    BrokerUART.print(cached_image);

    /* END */
    BrokerUART.println("END");

    last_send_ms = millis();
    awaiting_ack = true;

    Serial.printf(
        "📤 frame %lu sent (%u bytes)\n",
        frame_id,
        cached_image_len
    );
}

/* ================================
   PREPARE NEXT FRAME
   ================================ */
bool prepare_frame()
{
    int rc = AI.invoke(1, false, false);
    if (rc != CMD_OK)
        return false;

    frame_id++;

    /* ---------- Build inference JSON (NEW) ---------- */
    cached_inf = "";
    cached_inf += "{\"frame\":";
    cached_inf += frame_id;
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

    /* ---------- Build legacy JSON (UNCHANGED) ---------- */
    cached_json = cached_inf;  // identical content, preserved for compatibility

    /* ---------- Cache image ---------- */
    cached_image = AI.last_image();
    cached_image_len = cached_image.length();

    cached_image_crc = esp_crc32_le(
        0,
        (const uint8_t*)cached_image.c_str(),
        cached_image_len
    );

    Serial.printf(
        "🧠 prepared frame %lu (img=%u, crc=%08lx)\n",
        frame_id,
        cached_image_len,
        cached_image_crc
    );

    return true;
}

/* ================================
   SETUP
   ================================ */
void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("=======================================");
    Serial.println(" XIAO ESP32-S3 | SSCMA UART BROKER ");
    Serial.println("=======================================");

    BrokerUART.begin(
        UART_BAUD,
        SERIAL_8N1,
        UART_RX_PIN,
        UART_TX_PIN
    );

    Wire.begin();
    Wire.setClock(400000);

    if (!AI.begin(&Wire)) {
        Serial.println("❌ SSCMA init failed");
        while (1);
    }

    Serial.println("✅ SSCMA initialized");
    log_memory();
}

/* ================================
   LOOP
   ================================ */
void loop()
{
    /* ---------- Handle ACK / NACK ---------- */
    while (BrokerUART.available())
    {
        String line = BrokerUART.readStringUntil('\n');
        line.trim();

        if (line.startsWith("ACK "))
        {
            uint32_t ack_id = line.substring(4).toInt();
            if (ack_id == frame_id)
            {
                awaiting_ack = false;
                Serial.printf("✅ ACK %lu\n", ack_id);
            }
        }
        else if (line.startsWith("NACK "))
        {
            uint32_t nack_id = line.substring(5).toInt();
            if (nack_id == frame_id)
            {
                Serial.printf("🔁 NACK %lu → resend\n", nack_id);
                send_cached_frame();
            }
        }
    }

    /* ---------- Timeout ---------- */
    if (awaiting_ack && millis() - last_send_ms > ACK_TIMEOUT_MS)
    {
        Serial.printf("⏱ ACK timeout for frame %lu → resend\n", frame_id);
        send_cached_frame();
        return;
    }

    /* ---------- Send next frame ---------- */
    if (!awaiting_ack)
    {
        if (prepare_frame())
            send_cached_frame();
    }
}

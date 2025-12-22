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

/* ================================
   ACTUATORS
   ================================ */
static constexpr int LED_PIN_1 = 1;   // D0, PIN 1, RED
static constexpr int LED_PIN_2 = 2;   // D1, PIN 2, GREEN
static constexpr int LED_PIN_3 = 3;   // D2, PIN 3, WHITE

/* LED timing */
static constexpr uint32_t LED_ON_MS = 1000;

static uint32_t led1_until = 0;
static uint32_t led2_until = 0;
static uint32_t led3_until = 0;

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
static String cached_inf;
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
    BrokerUART.print("JSON ");
    BrokerUART.println(cached_json);

    BrokerUART.print("INF ");
    BrokerUART.println(cached_inf);

    BrokerUART.print("IMAGE ");
    BrokerUART.print(cached_image_len);
    BrokerUART.print(" ");
    BrokerUART.printf("%08lx\n", cached_image_crc);
    BrokerUART.print(cached_image);

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

    /* ---------- RAW INFERENCE LOG ---------- */
    Serial.println("🧠 RAW INFERENCE RESULT");
    Serial.printf("boxes: %u\n", AI.boxes().size());

    uint32_t now = millis();

    for (size_t i = 0; i < AI.boxes().size(); i++)
    {
        auto &b = AI.boxes()[i];
        Serial.printf(
            "  [%u] target=%u score=%u x=%u y=%u w=%u h=%u\n",
            i,
            b.target,
            b.score,
            b.x,
            b.y,
            b.w,
            b.h
        );

        /* ---------- LED LOGIC (NON-BLOCKING) ---------- */
        if (b.target == 3)
        {
            digitalWrite(LED_PIN_1, HIGH);
            led1_until = now + LED_ON_MS;
        }
        else if (b.target == 2)
        {
            digitalWrite(LED_PIN_2, HIGH);
            led2_until = now + LED_ON_MS;
        }
        else if (b.target == 3)
        {
            digitalWrite(LED_PIN_3, HIGH);
            led3_until = now + LED_ON_MS;
        }
    }

    if (AI.boxes().empty())
    {
        Serial.println("  (no detections)");
    }

    frame_id++;

    /* ---------- Build inference JSON ---------- */
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

    cached_json = cached_inf;

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
    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);
    
    uint32_t now = millis();
    digitalWrite(LED_PIN_1, HIGH);
        led1_until = now + LED_ON_MS;

    digitalWrite(LED_PIN_2, HIGH);
        led2_until = now + LED_ON_MS;

    digitalWrite(LED_PIN_3, HIGH);
        led3_until = now + LED_ON_MS;

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

    if (!AI.begin(&Wire))
    {
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
    uint32_t now = millis();

    /* ---------- LED timeout handling ---------- */
    if (led1_until && now > led1_until)
    {
        digitalWrite(LED_PIN_1, LOW);
        led1_until = 0;
    }

    if (led2_until && now > led2_until)
    {
        digitalWrite(LED_PIN_2, LOW);
        led2_until = 0;
    }

    if (led3_until && now > led3_until)
    {
        digitalWrite(LED_PIN_3, LOW);
        led3_until = 0;
    }

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

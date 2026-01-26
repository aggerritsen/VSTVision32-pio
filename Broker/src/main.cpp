#include <Arduino.h>
#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include <esp_heap_caps.h>
#include "esp_crc.h"
#include "esp_timer.h"

SSCMA AI;

/* ================================
   CONFIDENCE THRESHOLD
   ================================ */
static constexpr uint8_t CONFIDENCE_THRESHOLD = 70; // percent

/* ================================
   UART CONFIG (XIAO → T-SIM)
   ================================ */
static constexpr uint32_t UART_BAUD = 921600;
static constexpr int UART_TX_PIN = 43;
static constexpr int UART_RX_PIN = 44;

/* ================================
   TRANSPORT ENABLE FLAG
   ================================ */
static constexpr bool ENABLE_UART_TRANSPORT = true; // switch to enable/disable UART transport

/* ================================
   ACTUATORS
   ================================ */
static constexpr int LED_PIN_1 = 1;   // D0, PIN 1, RED  (Grove LED / Relay)
static constexpr int LED_PIN_2 = 2;   // D1, PIN 2, GREEN
static constexpr int LED_PIN_3 = 3;   // D2, PIN 3, WHITE

/* LED timing */
static constexpr uint32_t LED_ON_MS = 5000;

static uint32_t led1_until = 0;
static uint32_t led2_until = 0;
static uint32_t led3_until = 0;

/* Actuator timers (more responsive than relying on loop timing) */
static esp_timer_handle_t led1_timer = nullptr;
static esp_timer_handle_t led2_timer = nullptr;
static esp_timer_handle_t led3_timer = nullptr;

HardwareSerial BrokerUART(1);

/* ================================
   TRANSPORT
   ================================ */
static constexpr uint32_t ACK_TIMEOUT_MS = 5000;
static constexpr uint8_t  MAX_ACK_TIMEOUT_RETRIES = 5;

/* ================================
   STATE
   ================================ */
static uint32_t frame_id = 0;
static bool     awaiting_ack = false;
static uint32_t last_send_ms = 0;

/* timeout retry tracking + transport pause */
static uint8_t ack_timeout_retries = 0;
static bool    transport_paused = false;

/* Cached frame (for resend) */
static String cached_json;
static String cached_inf;
static String cached_image;
static size_t cached_image_len = 0;
static uint32_t cached_image_crc = 0;

/* UART RX line assembly (non-blocking) */
static String uart_line;

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
   ACTUATOR TIMER CALLBACKS
   ================================ */
static void led1_off_cb(void *arg)
{
    (void)arg;
    digitalWrite(LED_PIN_1, LOW);
    led1_until = 0;
}
static void led2_off_cb(void *arg)
{
    (void)arg;
    digitalWrite(LED_PIN_2, LOW);
    led2_until = 0;
}
static void led3_off_cb(void *arg)
{
    (void)arg;
    digitalWrite(LED_PIN_3, LOW);
    led3_until = 0;
}

/* Trigger helper: immediate ON + scheduled OFF even if loop is busy */
static inline void trigger_actuator(int pin, esp_timer_handle_t tmr, uint32_t &until_ms)
{
    digitalWrite(pin, HIGH);
    uint32_t now = millis();
    until_ms = now + LED_ON_MS;

    if (tmr)
    {
        // restart one-shot
        esp_timer_stop(tmr);
        esp_timer_start_once(tmr, (uint64_t)LED_ON_MS * 1000ULL);
    }
}

/* ================================
   SEND FRAME (CACHED)
   ================================ */
void send_cached_frame()
{
    if (!ENABLE_UART_TRANSPORT)
        return;

    if (transport_paused)
        return;

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
        (unsigned)cached_image_len
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

    Serial.println("🧠 RAW INFERENCE RESULT");
    Serial.printf("boxes: %u\n", (unsigned)AI.boxes().size());

    // Actuation should be fast and not dependent on loop timing
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

        if (b.target == 3 && b.score >= CONFIDENCE_THRESHOLD)
        {
            trigger_actuator(LED_PIN_1, led1_timer, led1_until);
        }
        else if (b.target == 2 && b.score >= CONFIDENCE_THRESHOLD)
        {
            trigger_actuator(LED_PIN_2, led2_timer, led2_until);
        }
        else if (b.target == 3 && b.score >= CONFIDENCE_THRESHOLD)
        {
            trigger_actuator(LED_PIN_3, led3_timer, led3_until);
        }
}

    frame_id++;

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

    // If UART transport is paused (timeouts), skip heavy image work entirely
    // This makes inference + actuation remain responsive and avoids remote image reception load.
    if (!ENABLE_UART_TRANSPORT || transport_paused)
    {
        cached_image = "";
        cached_image_len = 0;
        cached_image_crc = 0;

        Serial.printf(
            "🧠 prepared frame %lu (img=SKIPPED transport_paused=%s)\n",
            frame_id,
            transport_paused ? "YES" : "NO"
        );
        return true;
    }

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
        (unsigned)cached_image_len,
        cached_image_crc
    );

    return true;
}

/* ================================
   UART RX LINE PROCESSING
   ================================ */
static void process_uart_line(const String &line)
{
    if (line.startsWith("ACK "))
    {
        uint32_t ack_id = line.substring(4).toInt();

        // Any ACK can be treated as "link is alive again" if we were paused
        if (transport_paused)
        {
            transport_paused = false;
            ack_timeout_retries = 0;
            awaiting_ack = false;
            Serial.printf("🔓 transport resumed on ACK %lu\n", ack_id);
        }

        if (ack_id == frame_id)
        {
            awaiting_ack = false;
            ack_timeout_retries = 0;
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

/* Non-blocking UART poll: assemble lines char-by-char */
static void poll_uart_nonblocking()
{
    while (BrokerUART.available())
    {
        char c = (char)BrokerUART.read();

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            uart_line.trim();
            if (uart_line.length() > 0)
            {
                process_uart_line(uart_line);
            }
            uart_line = "";
        }
        else
        {
            // prevent runaway memory usage if peer sends junk without newlines
            if (uart_line.length() < 200)
                uart_line += c;
        }
    }
}

/* ================================
   SETUP
   ================================ */
void setup()
{
    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);

    // Create timers (one-shot)
    {
        esp_timer_create_args_t a1 = {};
        a1.callback = &led1_off_cb;
        a1.name = "led1_off";
        esp_timer_create(&a1, &led1_timer);

        esp_timer_create_args_t a2 = {};
        a2.callback = &led2_off_cb;
        a2.name = "led2_off";
        esp_timer_create(&a2, &led2_timer);

        esp_timer_create_args_t a3 = {};
        a3.callback = &led3_off_cb;
        a3.name = "led3_off";
        esp_timer_create(&a3, &led3_timer);
    }

    // Power-on blink (still works, now via trigger helper)
    trigger_actuator(LED_PIN_1, led1_timer, led1_until);
    trigger_actuator(LED_PIN_2, led2_timer, led2_until);
    trigger_actuator(LED_PIN_3, led3_timer, led3_until);

    Serial.begin(115200);
    delay(500);

    Serial.println("=======================================");
    Serial.println(" XIAO ESP32-S3 | SSCMA UART BROKER ");
    Serial.println("=======================================");

    if (ENABLE_UART_TRANSPORT)
    {
        BrokerUART.begin(
            UART_BAUD,
            SERIAL_8N1,
            UART_RX_PIN,
            UART_TX_PIN
        );
    }

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
    // UART RX (non-blocking)
    if (ENABLE_UART_TRANSPORT)
    {
        poll_uart_nonblocking();

        // ACK timeout / resend / pause logic
        if (awaiting_ack && (millis() - last_send_ms > ACK_TIMEOUT_MS))
        {
            ack_timeout_retries++;

            if (ack_timeout_retries >= MAX_ACK_TIMEOUT_RETRIES)
            {
                Serial.printf(
                    "⏱ ACK timeout x%u for frame %lu → STOP SENDING, keep inference running (skip images)\n",
                    ack_timeout_retries,
                    frame_id
                );

                transport_paused = true;
                awaiting_ack = false;
                return;
            }

            Serial.printf(
                "⏱ ACK timeout for frame %lu (retry %u/%u) → resend\n",
                frame_id,
                ack_timeout_retries,
                MAX_ACK_TIMEOUT_RETRIES
            );
            send_cached_frame();
            return;
        }
    }

    // Prepare new frame only when we are not waiting on ACK
    if (!awaiting_ack)
    {
        if (prepare_frame())
        {
            if (ENABLE_UART_TRANSPORT)
            {
                // send_cached_frame() will no-op if transport_paused==true
                send_cached_frame();
            }
        }
    }
}
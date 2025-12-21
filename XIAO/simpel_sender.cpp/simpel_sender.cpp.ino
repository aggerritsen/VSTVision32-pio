#include <Arduino.h>

/* ================================
   HARD UART CONFIG (XIAO ESP32-S3)
   ================================ */
static constexpr int UART_TX_PIN = 43;
static constexpr int UART_RX_PIN = 44; // unused, but required
static constexpr uint32_t UART_BAUD = 921600;

HardwareSerial BrokerUART(1);

void setup()
{
    // USB debug
    Serial.begin(115200);
    delay(500);

    Serial.println("=================================");
    Serial.println(" XIAO ESP32-S3 | MINIMAL TX TEST ");
    Serial.println("=================================");

    // Hardware UART
    BrokerUART.begin(
        UART_BAUD,
        SERIAL_8N1,
        UART_RX_PIN,
        UART_TX_PIN
    );

    Serial.printf("UART TX on GPIO%d @ %d baud\n",
                  UART_TX_PIN, UART_BAUD);
}

void loop()
{
    static uint32_t counter = 0;

    // Send deterministic ASCII payload
    BrokerUART.printf(
        "TX TEST %lu | HELLO T-SIM | 0123456789 ABCDEF\r\n",
        counter++
    );

    // USB echo so you know it's alive
    Serial.println("TX sent");

    delay(1000);
}

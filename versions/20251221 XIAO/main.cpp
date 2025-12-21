#include <Arduino.h>
#include <Wire.h>

/* =============================
   I2C CONFIG (LOCKED)
   ============================= */
static constexpr int SDA_PIN = 16;
static constexpr int SCL_PIN = 17;

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println();
    Serial.println("=======================================");
    Serial.println(" RFC LOW-LEVEL I2C SCAN (T-SIM7080G-S3)");
    Serial.println(" SDA=GPIO16  SCL=GPIO17");
    Serial.println("=======================================");

    // Force pins to known state BEFORE Wire
    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, INPUT_PULLUP);
    delay(10);

    Serial.print("Initial pin levels: SDA=");
    Serial.print(digitalRead(SDA_PIN));
    Serial.print(" SCL=");
    Serial.println(digitalRead(SCL_PIN));

    // Start I2C explicitly on these pins
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);   // slow & safe

    Serial.println("Starting I2C address scan...");

    int found = 0;

    for (uint8_t addr = 1; addr < 127; addr++)
    {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission(true);

        if (err == 0)
        {
            Serial.print("✅ ACK @ 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
            found++;
        }
        else if (err == 4)
        {
            Serial.print("⚠️  BUS ERROR @ 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
        }

        delay(2);
    }

    Serial.print("Scan complete. Devices found = ");
    Serial.println(found);

    // Final bus state
    Serial.print("Final pin levels: SDA=");
    Serial.print(digitalRead(SDA_PIN));
    Serial.print(" SCL=");
    Serial.println(digitalRead(SCL_PIN));

    Serial.println("=======================================");
}

void loop()
{
    delay(1000);
}

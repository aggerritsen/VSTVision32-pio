#include <Arduino.h>
#include <Wire.h>

/*
  XIAO ESP32-S3 OLED test sketch (I2C)

  Works with common XIAO expansion boards that have a 0.96" SSD1306 OLED (128x64) on I2C.
  Default I2C address is usually 0x3C.

  Install library:
    - "Adafruit SSD1306"
    - "Adafruit GFX Library"
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static constexpr uint8_t OLED_ADDR = 0x3C;   // common SSD1306 address
static constexpr int OLED_W = 128;
static constexpr int OLED_H = 64;

Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

static void i2c_scan()
{
  Serial.println("I2C scan...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0)
    {
      Serial.printf("  found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  no I2C devices found");
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println("================================");
  Serial.println(" XIAO OLED TEST (SSD1306 I2C) ");
  Serial.println("================================");

  Wire.begin();           // uses board default SDA/SCL
  Wire.setClock(400000);  // fast I2C

  i2c_scan();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("❌ SSD1306 init failed (wrong addr or wiring?)");
    while (1) delay(100);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("XIAO OLED TEST");
  display.println("----------------");

  display.setTextSize(1);
  display.print("I2C addr: 0x");
  display.println(OLED_ADDR, HEX);

  display.print("Res: ");
  display.print(OLED_W);
  display.print("x");
  display.println(OLED_H);

  display.println();
  display.println("Hello OLED :)");

  display.display();

  Serial.println("✅ OLED initialized and text drawn");
}

void loop()
{
  static uint32_t last_ms = 0;
  static uint32_t counter = 0;

  uint32_t now = millis();
  if (now - last_ms >= 1000)
  {
    last_ms = now;
    counter++;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("XIAO OLED TEST");
    display.println("----------------");

    display.print("Uptime: ");
    display.print(counter);
    display.println(" s");

    display.print("Millis: ");
    display.println(now);

    // simple animation bar
    int bar_w = (int)((counter % 128));
    display.drawRect(0, 52, 128, 10, SSD1306_WHITE);
    display.fillRect(1, 53, max(0, bar_w - 2), 8, SSD1306_WHITE);

    display.display();

    Serial.printf("OLED update: %lu s\n", (unsigned long)counter);
  }
}
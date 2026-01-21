// config.h (RFC) — central pin config for 7070 + 7080
#pragma once
#include <stdint.h>

/*
Build-time selection (platformio.ini build_flags):
  -DVST_BOARD_7070
  -DVST_BOARD_7080
If none provided, default to 7080.
*/
#if !defined(VST_BOARD_7070) && !defined(VST_BOARD_7080)
#define VST_BOARD_7080
#endif

// Common constants
static constexpr uint32_t LED_ON_MS  = 250;
static constexpr uint32_t AI_I2C_HZ  = 400000;
static constexpr uint32_t PMU_I2C_HZ = 400000;
static constexpr uint32_t MODEM_BAUD = 115200;

// =========================================================
// 7070 / ESP32 (SIM7000/SIM7070 family boards)
// =========================================================
#if defined(VST_BOARD_7070)

// VisionAI / SSCMA I2C (your chosen)
static constexpr int AI_I2C_SDA = 32;
static constexpr int AI_I2C_SCL = 33;

// Actuator GPIOs (your logical mapping; safe ESP32 pins)
static constexpr int ACT_GPIO_9  = 13;
static constexpr int ACT_GPIO_10 = 14;
static constexpr int ACT_GPIO_11 = 16;
static constexpr int ACT_GPIO_12 = 17;
static constexpr int ACT_GPIO_13 = 18;
static constexpr int ACT_GPIO_14 = 19;

static constexpr int ACT_GPIO_21 = 21;
static constexpr int ACT_GPIO_47 = 22; // remapped for ESP32
static constexpr int ACT_GPIO_48 = 23; // remapped for ESP32

static constexpr int LED_PIN_1 = ACT_GPIO_11;
static constexpr int LED_PIN_2 = ACT_GPIO_10;
static constexpr int LED_PIN_3 = ACT_GPIO_9;

// --- MODEM (matches the AllFunctions.ino example) ---
static constexpr int MODEM_RXD = 26;
static constexpr int MODEM_TXD = 27;

// This is the modem power key control pin in their example
// (They call it PWR_PIN and pulse it HIGH->LOW with >= 1s delay)
static constexpr int MODEM_PWR = 4;

// --- SD (ESP32 uses SPI SD in that example) ---
static constexpr int SD_SPI_SCLK  = 14;
static constexpr int SD_SPI_MISO = 2;
static constexpr int SD_SPI_MOSI = 15;
static constexpr int SD_SPI_CS   = 13;

// PMU: not used on this board (compile out / ignore)
static constexpr bool PMU_PRESENT = false;

#endif // VST_BOARD_7070

// =========================================================
// 7080 / ESP32-S3 (your working baseline)
// =========================================================
#if defined(VST_BOARD_7080)

// VisionAI / SSCMA I2C (your chosen)
static constexpr int AI_I2C_SDA = 16;
static constexpr int AI_I2C_SCL = 17;

// Preferred actuator GPIOs (your chosen)
static constexpr int ACT_GPIO_9  = 9;
static constexpr int ACT_GPIO_10 = 10;
static constexpr int ACT_GPIO_11 = 11;
static constexpr int ACT_GPIO_12 = 12;
static constexpr int ACT_GPIO_13 = 13;
static constexpr int ACT_GPIO_14 = 14;

static constexpr int ACT_GPIO_21 = 21;
static constexpr int ACT_GPIO_47 = 47;
static constexpr int ACT_GPIO_48 = 48;

static constexpr int LED_PIN_1 = ACT_GPIO_11;
static constexpr int LED_PIN_2 = ACT_GPIO_10;
static constexpr int LED_PIN_3 = ACT_GPIO_9;

// Modem wiring you already established for the 7080 family board
static constexpr int MODEM_RXD = 4;
static constexpr int MODEM_TXD = 5;
static constexpr int MODEM_PWR = 41;

// SD_MMC wiring you already use on 7080
static constexpr int SD_CMD  = 39;
static constexpr int SD_CLK  = 38;
static constexpr int SD_DATA = 40;

// PMU: present on your 7080 baseline
static constexpr bool PMU_PRESENT = true;

// PMU I2C (your known working wiring)
static constexpr int PMU_I2C_SDA = 15;
static constexpr int PMU_I2C_SCL = 7;

#endif // VST_BOARD_7080

// config.h  (RFC) — central pin config for T-SIM7080G-S3
#pragma once
#include <stdint.h>
#include "driver/uart.h"

/* =========================================================
   BOARD: T-SIM7080G-S3  (central pin config)
   ========================================================= */

/* ---------- Preferred actuator GPIOs ---------- */
static constexpr int ACT_GPIO_9  = 9;
static constexpr int ACT_GPIO_10 = 10;
static constexpr int ACT_GPIO_11 = 11;
static constexpr int ACT_GPIO_12 = 12;
static constexpr int ACT_GPIO_13 = 13;
static constexpr int ACT_GPIO_14 = 14;

/* ---------- Spare actuator GPIOs ---------- */
static constexpr int ACT_GPIO_21 = 21;
static constexpr int ACT_GPIO_47 = 47;
static constexpr int ACT_GPIO_48 = 48;

/* ---------- LED mapping used by main.cpp ---------- */
static constexpr int LED_PIN_1 = ACT_GPIO_11;
static constexpr int LED_PIN_2 = ACT_GPIO_10;
static constexpr int LED_PIN_3 = ACT_GPIO_9;
static constexpr uint32_t LED_ON_MS = 250;

/* ---------- SSCMA / VisionAI I2C (separate controller: Wire1) ---------- */
static constexpr int AI_I2C_SDA = 3;          // inference wiring
static constexpr int AI_I2C_SCL = 8;          // inference wiring
static constexpr uint32_t AI_I2C_HZ = 400000;

/* ---------- PMU I2C pins (used inside modem.cpp) ---------- */
static constexpr int PMU_I2C_SDA = 15;
static constexpr int PMU_I2C_SCL = 7;
static constexpr uint32_t PMU_I2C_HZ = 400000;

/* ---------- Modem UART + PWRKEY (used inside modem.cpp) ---------- */
static constexpr int MODEM_RXD   = 4;
static constexpr int MODEM_TXD   = 5;
static constexpr int MODEM_PWR   = 41;
static constexpr uint32_t MODEM_BAUD = 115200;

/* ---------- SD_MMC pins (used inside sdcard.cpp) ---------- */
static constexpr int SD_CMD  = 39;
static constexpr int SD_CLK  = 38;
static constexpr int SD_DATA = 40;

/* ---------- Optional: Broker UART ---------- */
static constexpr uart_port_t BROKER_UART = UART_NUM_2;
static constexpr int BROKER_RX_PIN = 18;
static constexpr int BROKER_TX_PIN = 17;
static constexpr int BROKER_BAUD   = 921600;
static constexpr int BROKER_BUF_SZ = 4096;

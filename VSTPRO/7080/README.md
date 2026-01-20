# VSTPRO – VisionAI System (T-SIM7080G-S3)

This repository contains the firmware for **VSTPRO**, a vision-based embedded system built on the **LILYGO T-SIM7080G-S3 (ESP32-S3)** platform.

The system integrates:

* SSCMA-based Vision AI inference over I²C
* Cellular modem (SIM7080) with PMU-managed power rails
* SD card storage for JPEG frames
* Actuator outputs (LEDs / future relays)
* Accurate system time derived from the cellular network

This document describes both **functional behavior** and **technical architecture**, including buses, ports, pinout, and PMU rails.

---

## 1. Functional Overview

### 1.1 Boot Sequence

1. **Serial startup** (115200 baud)
2. **PMU + Modem initialization**

   * PMU rails enabled
   * Modem powered and probed via AT
3. **Network time acquisition**

   * Modem queried via `AT+CCLK?`
   * Timestamp parsed and validated
   * ESP32 system time set via `settimeofday()`
4. **SD card initialization**

   * SD_MMC mounted using custom pin mapping
5. **VisionAI initialization**

   * SSCMA initialized on dedicated I²C bus (Wire1)
6. Enter main inference loop

System time is acquired **once at boot** and is thereafter used for all timestamps and filenames.

---

### 1.2 Runtime Behavior

* Vision inference runs continuously (~2–3 seconds per frame)
* For each frame:

  * Objects are detected (targets + bounding boxes)
  * Optional actuators (LEDs) are pulsed based on targets
  * JPEG image is retrieved (Base64 → binary)
  * JPEG is saved to SD card with timestamped filename

Example filename:

```
/20260120_210247_frame_000001.jpg
```

* If SSCMA reports `BUSY`, exponential backoff is applied
* If SSCMA stalls, it is reinitialized on Wire1

---

## 2. System Architecture

### 2.1 Processor

* **ESP32-S3** (dual-core, PSRAM enabled)
* Framework: Arduino (Espressif32)
* PSRAM used for JPEG buffers

---

## 3. Buses and Interfaces

### 3.1 I²C Buses

| Bus   | Purpose          | Controller | Notes                          |
| ----- | ---------------- | ---------- | ------------------------------ |
| Wire  | PMU (AXP2101)    | ESP32 I²C0 | Owned exclusively by modem/PMU |
| Wire1 | VisionAI (SSCMA) | ESP32 I²C1 | No shared devices              |

#### VisionAI I²C (Wire1)

* Dedicated bus to avoid PMU conflicts
* Clock: 400 kHz

---

### 3.2 UARTs

|  UART | Purpose       | TX GPIO | RX GPIO | Baud   |
| ----: | ------------- | ------: | ------: | ------ |
| UART0 | USB Serial    |       — |       — | 115200 |
| UART1 | SIM7080 Modem |   GPIO5 |   GPIO4 | 115200 |
| UART2 | Broker (opt.) |  GPIO17 |  GPIO18 | 921600 |

---

### 3.3 SD Card (SD_MMC)

* Interface: SD_MMC (1-bit mode)
* Mounted at: `/sdcard`

| Signal | GPIO |
| ------ | ---- |
| CMD    | 39   |
| CLK    | 38   |
| DATA   | 40   |

---

## 4. Pinout Summary

### 4.1 VisionAI (SSCMA)

| Function | GPIO |
| -------- | ---- |
| SDA      | 3    |
| SCL      | 8    |

---

### 4.2 PMU (AXP2101)

| Function | GPIO |
| -------- | ---- |
| SDA      | 15   |
| SCL      | 7    |

---

### 4.3 Modem (SIM7080)

| Function | GPIO |
| -------- | ---- |
| RXD      | 4    |
| TXD      | 5    |
| PWRKEY   | 41   |

---

### 4.4 Actuators / LEDs

Preferred actuator GPIOs:

| GPIO | Usage          |
| ---- | -------------- |
| 9    | LED / Actuator |
| 10   | LED / Actuator |
| 11   | LED / Actuator |
| 12   | Actuator       |
| 13   | Actuator       |
| 14   | Actuator       |

Spare actuator GPIOs:

| GPIO |
| ---- |
| 21   |
| 47   |
| 48   |

LED mapping used by firmware:

| LED  | GPIO |
| ---- | ---- |
| LED1 | 11   |
| LED2 | 10   |
| LED3 | 9    |

---

## 5. PMU (AXP2101) Power Rails

The PMU is controlled via I²C and initialized during modem startup.

### Enabled Rails

| Rail  | Voltage | Purpose                 |
| ----- | ------- | ----------------------- |
| DC3   | 3.0 V   | SIM7080 core supply     |
| BLDO2 | 3.3 V   | Modem I/O / peripherals |

### PMU Configuration Notes

* Touchscreen measurement disabled
* Rails enabled **before** modem AT probing
* PMU is initialized only once (early boot)

---

## 6. Time Handling

* Time source: **cellular network** via modem (`AT+CCLK?`)
* Format: `YYYYMMDD_HHMMSS`
* Applied using `settimeofday()`
* After boot:

  * `time()` / `localtime_r()` used everywhere
  * No further modem queries required

This ensures:

* Correct timestamps without RTC hardware
* Stable filenames
* Low runtime overhead

---

## 7. Memory Management

* JPEG buffers allocated with `heap_caps_malloc()`
* Buffers freed explicitly after SD write
* Heap statistics logged every frame
* PSRAM required and detected at runtime

---

## 8. Error Handling & Resilience

* SSCMA `BUSY` handled with exponential backoff
* Invoke deadline enforced
* Automatic SSCMA reinitialization on stall
* SD failures do not crash inference loop

---

## 9. Design Principles

* **Single ownership per bus** (no shared I²C)
* **No blocking modem calls in runtime loop**
* **Human-readable, sortable artifacts** (timestamped JPEGs)
* **Debug-first logging** (explicit boot phases)

---

## 10. Future Extensions (Planned)

* Per-session SD directories
* Metadata sidecar (CSV / JSON per frame)
* Actuator control logic beyond LEDs
* Periodic network time re-sync
* Remote upload via cellular data

---

**Status:** Stable, operational, and production-ready baseline.

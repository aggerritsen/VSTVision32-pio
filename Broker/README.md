# SSCMA Broker (Vision AI → XIAO → UART)

## Overview

This project implements a **stable, frozen broker** that connects a **Grove Vision AI (SSCMA)** device to downstream systems using a **Seeed Studio XIAO ESP32-S3** as an intermediary.

---

## Board Pinout

![XIAO-ESP32S3 Pinout](../docs/Seeed-XIAO-ESP32-S3-Pinout.jpg)

*Pinout diagram for the XIAO-ESP32-S3.*
For information on getting started with the ESP32-S3 board, see the
[XIAO ESP32S3 Getting Started guide on the Seeed Studio Wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).

This board is used in the XIAO Grove shield.

![XIAO Grove SHield Pinout](../docs/xiao_Grove_shield_pinout.png)

For details on the Grove Shield and its embedded battery management features, see the
[Grove Shield for Seeeduino XIAO – Embedded Battery Management Chip guide on the Seeed Studio Wiki](https://wiki.seeedstudio.com/Grove-Shield-for-Seeeduino-XIAO-embedded-battery-management-chip/).

---

This broker reliably:
- Communicates with **Grove Vision AI over I²C**
- Triggers inference using the **SSCMA protocol**
- Retrieves **inference results and image data**
- Forwards data over **UART** to external devices (e.g. T-SIM7080G-S3, PC, gateway)

The broker has been validated as **functionally correct and stable** and is now considered **frozen**.

---

## Architecture

```
┌────────────────┐ I²C (400 kHz) ┌────────────────────┐
│ Grove Vision AI│ <────────────────────────>│ XIAO ESP32-S3 │
│ (WiseEye2) │ │ SSCMA Broker │
└────────────────┘ │ │
│ • invoke models │
│ • read boxes │
│ • read images │
└─────────┬──────────┘
│ UART
▼
┌────────────────────┐
│ Downstream system │
│ (T-SIM / PC / LTE) │
└────────────────────┘
```

---

## File

**Canonical broker file (frozen):**
```
sscma_broker.txt
```

> ⚠️ The file extension `.txt` is intentional to emphasize its role as a
> **reference artifact**.  
> It can be compiled as `.ino` or `.cpp` without modification.

---

## Responsibilities of the Broker

### I²C (Vision AI → XIAO)
- Initialize SSCMA using `Seeed_Arduino_SSCMA`
- Ensure correct I²C clock (400 kHz)
- Invoke inference using:
```
```

> ⚠️ The file extension `.txt` is intentional to emphasize its role as a
> **reference artifact**.  
> It can be compiled as `.ino` or `.cpp` without modification.

---

## Responsibilities of the Broker

### I²C (Vision AI → XIAO)
- Initialize SSCMA using `Seeed_Arduino_SSCMA`
- Ensure correct I²C clock (400 kHz)
- Invoke inference using:
```
AT+INVOKE=1,0,0
```
(image **included**, not result-only)
- Drain **all event responses**
- Handle SSCMA timing constraints correctly

### Data Retrieved
- Inference performance:
- preprocess
- inference
- postprocess
- Detection results:
- boxes (target, score, x, y, w, h)
- Image:
- Base64-encoded JPEG (`last_image()`)

### UART (XIAO → downstream)
- Forward **each frame** as a complete unit
- Includes:
- frame counter
- timing delta
- inference metadata
- bounding boxes
- base64 image payload
- No flow control assumptions
- No retries at protocol level

---

## Design Principles

### 1. Frozen Behavior
The broker is **not allowed to change**:
- No refactoring
- No timing changes
- No buffer size changes
- No retries added
- No protocol reinterpretation

All future work happens **downstream**.

---

### 2. Drain Before Delay
A critical lesson learned:

> SSCMA requires time to emit event replies after `INVOKE`.

The broker **always delays (~120 ms)** after each invoke to ensure:
- Image events are fully received
- No I²C congestion
- No timeout on next invoke

---

### 3. Memory Discipline
- Heap usage is monitored every frame
- PSRAM is **not required**
- Typical image size: **13–16 kB base64**
- Stable heap observed over long runs

---

### 4. Explicit Defaults (No Hidden State)
The broker explicitly logs or validates:
- TSCORE
- TIOU
- INVOKE state
- SAMPLE state
- KV registry
- ACTION configuration

This eliminates dependency on **XIAO firmware magic**.

---

## What the Broker Does NOT Do

- ❌ No LTE / NB-IoT
- ❌ No MQTT
- ❌ No file storage
- ❌ No SD card
- ❌ No cloud logic
- ❌ No retries or ACK handling
- ❌ No JSON parsing on Vision side

Those responsibilities belong **downstream**.

---

## Why a Broker Is Necessary

Directly connecting Grove Vision AI to complex boards (e.g. T-SIM7080G-S3) revealed:

- SSCMA timing sensitivity
- Event-driven I²C behavior
- Image transfers that stall without correct draining
- Subtle defaults handled implicitly by XIAO firmware

The XIAO acts as:
- A **protocol stabilizer**
- A **timing firewall**
- A **clean UART emitter**

---

## Known-Good Output Example
```
[3] +1954ms invoke...
invoke success
perf: preprocess=7 inference=36 postprocess=0
Box[0] target=3 score=69 x=383 y=201 w=189 h=148
image_base64_len=13496
✅ frame 3 sent (img=13496 bytes)
heap_free=288976 heap_min=273156 psram=NO
```

---

## Status

| Item                         | State |
|------------------------------|-------|
| Vision AI communication      | ✅ Stable |
| Image retrieval              | ✅ Stable |
| Inference results            | ✅ Stable |
| UART forwarding              | ✅ Stable |
| Heap stability               | ✅ Stable |
| SSCMA timeouts               | ❌ None |
| Further broker changes       | 🔒 Frozen |

---

## Next Steps (Outside the Broker)

- Define UART message schema (JSON / hybrid)
- Implement T-SIM UART receiver
- Add CRC / framing downstream
- Transport via LTE / MQTT / storage
- Long-term logging and monitoring

---

## Final Note

This broker is the **reference implementation**.

If something fails downstream:
> **Do not change the broker.**  
> Compare against it.

---


# TB6612FNG Stepper + Dual DC Motor Test (XIAO ESP32-S3)

This repository contains a single `src/main.cpp` that can drive a **TB6612FNG** either as:

* a **bipolar (2‑coil) stepper driver** for a *28BYJ‑48* (red wire disconnected), or
* a **dual DC motor driver** (Motor A + Motor B).

The mode is selected at compile time with a single parameter.

---

## Hardware

* MCU: **Seeed XIAO ESP32‑S3** (or compatible ESP32‑S3 board)
* Driver: **TB6612FNG** dual H‑bridge
* Motor option A: **28BYJ‑48** wired in *bipolar-like* mode (see below)
* Motor option B: **Two DC motors**

### Power notes

* **VM (motor supply)**: connect to your motor supply (commonly 5 V for small motors)
* **VCC (logic)**: 3.3 V from the ESP32‑S3
* **GND**: all grounds must be common
* **STBY**: this project assumes STBY is wired to **3.3 V** (always enabled)

---

## Mode selection

In `src/main.cpp`:

```cpp
#define MODE_STEPPER 1
#define MODE_DC      2
#define MODE MODE_STEPPER   // <-- change to MODE_DC for DC motor mode
```

* `MODE_STEPPER` runs the 28BYJ‑48 as a 2‑coil stepper at a fixed speed.
* `MODE_DC` turns the TB6612FNG into a dual DC motor driver with serial commands.

---

## TB6612FNG pinout (ESP32‑S3 GPIO)

The project uses these GPIOs:

| TB6612FNG pin | Function                 | ESP32‑S3 GPIO              |
| ------------- | ------------------------ | -------------------------- |
| PWMA          | PWM speed Motor/Bridge A | GPIO 9                     |
| AIN1          | Direction A              | GPIO 11                    |
| AIN2          | Direction A              | GPIO 10                    |
| PWMB          | PWM speed Motor/Bridge B | GPIO 14                    |
| BIN1          | Direction B              | GPIO 13                    |
| BIN2          | Direction B              | GPIO 12                    |
| STBY          | Enable                   | Wired to 3.3 V (always on) |

PWM is generated with ESP32 **LEDC** at **20 kHz**, 8‑bit (0…255).

---

## 28BYJ‑48 “bipolar” mode (red disconnected)

The classic 28BYJ‑48 is a **5‑wire unipolar** stepper (red is the common). In this project you run it in a **2‑coil H‑bridge configuration** by:

* **disconnecting the red wire** (do not connect it to VM/VCC/GND)
* wiring the remaining four wires as two coil pairs

### Confirmed working coil pairing

* **Bridge A (A01/A02) = Orange + Pink**
* **Bridge B (B01/B02) = Yellow + Blue**
* **Red disconnected**

This pairing provided strong torque in both directions in testing.

### Wiring table (as requested)

|Connector | Color  | Winding | Channel|Pin|
| --- | ------ | ------- | ------- | ------ |
| X | Red    | - |         |        |
| A | Orange | A1  | CH1 (−) | P4 (A1) |
| B | Yellow | B1  | CH2 (−) | P6 (B2) |
| C | Pink   | A2  | CH1 (+) | P5 (A2) |
| D | Blue   | B2  | CH2 (+) | P7 (B1) |

> Note: swapping the two wires of **one** bridge simply flips direction for that coil.

### Recommended stepper speed range

Based on your measurements:

* **Best torque:** **200–400 steps/s**
* Above ~400 steps/s the motor tends to slip / lose torque.

---

## Stepper mode behavior

Stepper mode uses **full‑step (2‑phase ON)** driving:

State sequence (A,B):

1. A+, B+
2. A−, B+
3. A−, B−
4. A+, B−

Implementation details:

* Fixed speed is set by `FIXED_STEPS_PER_SEC` (default 400)
* Direction alternates automatically:

  * forward for `RUN_DIR_MS`
  * brief coast (`COAST_MS`)
  * reverse for `RUN_DIR_MS`

### Stepper mode serial controls

While running, you can flip a coil polarity in software:

* `a` toggles `FLIP_COIL_A`
* `b` toggles `FLIP_COIL_B`
* `r` resets both flips to `false`

Use this if direction is reversed or stepping is rough after changing wiring.

---

## DC motor mode behavior

DC mode treats the TB6612FNG as **two independent DC motor channels**:

* Motor A: AIN1/AIN2 + PWMA
* Motor B: BIN1/BIN2 + PWMB

### DC mode serial commands

Open Serial Monitor at **115200**, newline enabled.

Commands:

* `A <speed>` sets Motor A speed from **−255..255**

  * Example: `A 200` (forward)
  * Example: `A -150` (reverse)
* `B <speed>` sets Motor B speed from **−255..255**
* `S` stops both motors
* `H` prints help

### Recommended DC operating range

From your testing:

* **Best torque band:** **150–255** (PWM duty)
* Below ~120 duty, torque is insufficient.

> Note: 255 is the maximum for this build because PWM is configured as 8‑bit. Higher numbers do not increase power beyond 100% duty.

---

## PWM limits (why 255 is the max)

The sketch uses **8‑bit PWM**:

* duty range: **0…255**
* **255 = 100% ON**

Increasing PWM resolution (e.g., 10‑bit → 0…1023) gives *finer steps* but does **not** exceed 100% duty.

If you need more torque beyond 100% duty, that requires hardware changes (motor supply voltage within spec, better supply current, different motor/gear ratio, etc.).

---

## Quick start

1. Wire TB6612FNG power: VM, VCC, GND, STBY.
2. Choose your motor wiring:

   * Stepper: Orange+Pink on Bridge A, Yellow+Blue on Bridge B, red disconnected.
   * DC: connect Motor A to A01/A02 and Motor B to B01/B02.
3. Select mode in `src/main.cpp`.
4. Build and flash.
5. Use Serial Monitor at 115200 to observe output and control DC mode.

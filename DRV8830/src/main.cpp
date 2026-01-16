#include <Arduino.h>
#include <Wire.h>

/*
  RFC: Full code
  Target: XIAO ESP32 + Grove Mini I2C Motor Driver (DRV8830 x2)
  Found I2C addresses (7-bit): 0x60 and 0x65
  Stepper: 28BYJ-48 after bipolar mod (bipolar, 2 coils)
*/

// ---- DRV8830 registers ----
static constexpr uint8_t REG_CONTROL = 0x00;

// Your scanned 7-bit I2C addresses:
static constexpr uint8_t ADDR_COIL_A = 0x60; // DRV8830 #1
static constexpr uint8_t ADDR_COIL_B = 0x65; // DRV8830 #2

// VSET: 0x00..0x3F (start moderate; raise if it stalls)
static uint8_t VSET = 0x30;

enum Dir : int8_t { COAST = 0, FWD = +1, REV = -1 };

static bool drvWriteReg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr7);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static uint8_t makeControl(uint8_t vset, Dir d)
{
    // CONTROL: [VSET(6 bits)] [IN2] [IN1]
    uint8_t in1 = 0, in2 = 0;
    if (d == FWD) { in1 = 1; in2 = 0; }
    else if (d == REV) { in1 = 0; in2 = 1; }
    else { in1 = 0; in2 = 0; } // coast

    return (uint8_t)((vset & 0x3F) << 2) | (in2 << 1) | (in1 << 0);
}

static void coilSet(uint8_t addr7, Dir d)
{
    uint8_t ctrl = makeControl(VSET, d);
    drvWriteReg(addr7, REG_CONTROL, ctrl);
}

static void stepperRelease()
{
    coilSet(ADDR_COIL_A, COAST);
    coilSet(ADDR_COIL_B, COAST);
}

static void stepperStepFull(uint32_t idx)
{
    // Full-step 4-state sequence (deterministic)
    switch (idx & 0x03)
    {
        case 0: coilSet(ADDR_COIL_A, FWD); coilSet(ADDR_COIL_B, FWD); break; // A+, B+
        case 1: coilSet(ADDR_COIL_A, REV); coilSet(ADDR_COIL_B, FWD); break; // A-, B+
        case 2: coilSet(ADDR_COIL_A, REV); coilSet(ADDR_COIL_B, REV); break; // A-, B-
        case 3: coilSet(ADDR_COIL_A, FWD); coilSet(ADDR_COIL_B, REV); break; // A+, B-
    }
}

static void printBanner()
{
    Serial.println();
    Serial.println("=== DRV8830 I2C Stepper Speed Test ===");
    Serial.print("Coil A address: 0x"); Serial.println(ADDR_COIL_A, HEX);
    Serial.print("Coil B address: 0x"); Serial.println(ADDR_COIL_B, HEX);
    Serial.print("VSET: 0x"); Serial.println(VSET, HEX);
    Serial.println("Wiring reminder (bipolar 28BYJ-48 mod):");
    Serial.println("  Coil A = Orange+Pink  -> one motor terminal");
    Serial.println("  Coil B = Yellow+Blue  -> other motor terminal");
    Serial.println("If direction is reversed/rough: swap the 2 wires of ONE coil.");
    Serial.println();
}

static void runStepsForward(uint32_t steps, uint16_t stepDelayMs)
{
    for (uint32_t i = 0; i < steps; i++)
    {
        stepperStepFull(i);
        delay(stepDelayMs);
    }
}

static void runStepsReverse(uint32_t steps, uint16_t stepDelayMs)
{
    // reverse by counting backwards (deterministic reverse)
    for (uint32_t i = 0; i < steps; i++)
    {
        stepperStepFull(steps - 1 - i);
        delay(stepDelayMs);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1500);

    unsigned long start = millis();
    while (!Serial && millis() - start < 5000) { delay(10); }

    Wire.begin();

    printBanner();
    stepperRelease();
    delay(300);
}

void loop()
{
    // User request: test up to 1500 steps, increase speed to see capabilities
    const uint32_t maxSteps = 1500;

    // Speed sweep: start slow (safe), then faster by lowering delay.
    // You can extend to 0ms later, but 1ms is already very fast for this setup.
    const uint16_t delaysMs[] = { 12, 10, 8, 6, 5, 4, 3, 2, 1 };
    const size_t nDelays = sizeof(delaysMs) / sizeof(delaysMs[0]);

    for (size_t k = 0; k < nDelays; k++)
    {
        uint16_t d = delaysMs[k];

        Serial.print("Forward: steps=");
        Serial.print(maxSteps);
        Serial.print(" delay_ms=");
        Serial.println(d);

        runStepsForward(maxSteps, d);
        stepperRelease();
        delay(500);

        Serial.print("Reverse: steps=");
        Serial.print(maxSteps);
        Serial.print(" delay_ms=");
        Serial.println(d);

        runStepsReverse(maxSteps, d);
        stepperRelease();
        delay(1200);
    }

    Serial.println("Sweep finished. Waiting 5s, then repeating...");
    delay(5000);
}

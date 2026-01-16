#include <Arduino.h>
#include <AccelStepper.h>

// ==== GPIO mapping for Seeed XIAO ESP32S3 ====
// (matches your working setup)
static constexpr int PIN_IN1 = 1;  // GPIO1
static constexpr int PIN_IN2 = 2;  // GPIO2
static constexpr int PIN_IN3 = 3;  // GPIO3
static constexpr int PIN_IN4 = 4;  // GPIO4

// 28BYJ-48 via ULN2003 works best in HALF4WIRE
AccelStepper stepper(
    AccelStepper::HALF4WIRE,
    PIN_IN1, PIN_IN3, PIN_IN2, PIN_IN4
);

// --- Test parameters ---
static constexpr int START_SPEED = 200;    // steps/sec
static constexpr int END_SPEED   = 1000;   // upper stress limit
static constexpr int SPEED_STEP  = 100;    // increment
static constexpr uint32_t RUN_MS = 1500;   // run time per speed

void setup()
{
    Serial.begin(115200);

    // Give USB CDC time to enumerate (ESP32-S3 specific)
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0 < 1500)) {
        delay(10);
    }

    Serial.println();
    Serial.println("=== 28BYJ-48 MAX SPEED TEST ===");
    Serial.println("Board: Seeed XIAO ESP32S3");
    Serial.println("Mode : HALF4WIRE (ULN2003)");
    Serial.println();

    stepper.enableOutputs();
    stepper.setMaxSpeed(1000);  // ceiling only
}

void runAtSpeed(float speed, uint32_t durationMs)
{
    stepper.setSpeed(speed);
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        stepper.runSpeed();
        delay(0);  // allow ESP32 background tasks
    }
}

void loop()
{
    int lastGoodSpeed = START_SPEED;

    Serial.println("Starting forward speed sweep...");
    for (int sp = START_SPEED; sp <= END_SPEED; sp += SPEED_STEP) {
        Serial.print("Speed = ");
        Serial.print(sp);
        Serial.println(" steps/s");

        runAtSpeed((float)sp, RUN_MS);

        // Small pause between steps
        delay(300);

        lastGoodSpeed = sp;
    }

    Serial.println();
    Serial.print("Sweep finished. Last tested speed = ");
    Serial.println(lastGoodSpeed);
    Serial.println("Running continuously at this speed...");
    Serial.println(">>> Listen/watch for missed steps or buzzing <<<");
    Serial.println();

    // Hold at max tested speed so you can judge stability
    while (true) {
        stepper.setSpeed((float)lastGoodSpeed);
        stepper.runSpeed();
        delay(0);
    }
}

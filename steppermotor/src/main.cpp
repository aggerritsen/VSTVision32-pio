#include <Arduino.h>
#include <AccelStepper.h>

// --- GPIO pin mapping for Seeed XIAO ESP32S3 ---
static constexpr int PIN_IN1 = 1;  // GPIO1
static constexpr int PIN_IN2 = 2;  // GPIO2
static constexpr int PIN_IN3 = 3;  // GPIO3
static constexpr int PIN_IN4 = 4;  // GPIO4

// 28BYJ-48 + ULN2003 works best in HALF4WIRE mode
AccelStepper stepper(
    AccelStepper::HALF4WIRE,
    PIN_IN1,
    PIN_IN3,
    PIN_IN2,
    PIN_IN4
);

// Approximate for 28BYJ-48 in half-step mode
static constexpr long STEPS_PER_REV = 2048;

void moveRevs(float revs, float speed, float accel)
{
    stepper.setMaxSpeed(speed);
    stepper.setAcceleration(accel);

    long steps = (long)(revs * STEPS_PER_REV);
    long target = stepper.currentPosition() + steps;

    Serial.print("Moving ");
    Serial.print(revs, 2);
    Serial.print(" revs -> steps: ");
    Serial.println(steps);

    stepper.moveTo(target);

    while (stepper.distanceToGo() != 0) {
        stepper.run();
        delay(0); // allow ESP32 background tasks
    }

    Serial.println("Done.");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("28BYJ-48 + ULN2003 test (XIAO ESP32S3)");

    stepper.setMaxSpeed(600);
    stepper.setAcceleration(400);
    stepper.enableOutputs();
}

void loop()
{
    moveRevs(+1.0f, 500, 300);
    delay(500);

    moveRevs(-1.0f, 500, 300);
    delay(1000);
}

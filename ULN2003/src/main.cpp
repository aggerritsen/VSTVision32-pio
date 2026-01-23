#include <Arduino.h>
#include <AccelStepper.h>

// ==== GPIO mapping for Seeed XIAO ESP32S3 ====
// GPIO -> ULN2003 IN pins
static constexpr int PIN_IN1 = 1;  // GPIO1 -> IN1
static constexpr int PIN_IN2 = 2;  // GPIO2 -> IN2
static constexpr int PIN_IN3 = 3;  // GPIO3 -> IN3
static constexpr int PIN_IN4 = 4;  // GPIO4 -> IN4

// 28BYJ-48 + ULN2003: use FULLSTEP with pin order IN1-IN3-IN2-IN4 (common working order)
#define FULLSTEP 4
AccelStepper stepper(FULLSTEP, PIN_IN1, PIN_IN3, PIN_IN2, PIN_IN4);

// ---- Tuning parameters ----
static constexpr int   CENTER_SPEED   = 400;   // your known good reference
static constexpr int   MIN_SPEED      = 100;   // safety lower bound
static constexpr int   MAX_SPEED      = 1200;  // safety upper bound (not necessarily achievable)
static constexpr int   INITIAL_STEP   = 40;    // first up/down jump from center
static constexpr int   MIN_STEP       = 5;     // stop when step size gets this small
static constexpr uint32_t TEST_MS     = 4000;  // run duration per test
static constexpr uint32_t PAUSE_MS    = 300;   // pause between tests
static constexpr uint32_t LOG_EVERY_MS= 500;   // throttled logging

static int currentSpeed = CENTER_SPEED;
static int stepSize     = INITIAL_STEP;

static int bestGoodSpeed = -1; // highest speed you marked GOOD
static int lastTestSpeed = -1;

void runAtSpeedForMs(int speed, uint32_t durationMs)
{
  stepper.setSpeed((float)speed);

  uint32_t tStart = millis();
  uint32_t lastLog = 0;

  while (millis() - tStart < durationMs) {
    stepper.runSpeed();
    delay(0);

    uint32_t now = millis();
    if (now - lastLog >= LOG_EVERY_MS) {
      lastLog = now;
      Serial.print("  running speed=");
      Serial.print(speed);
      Serial.print(" steps/s  (time ");
      Serial.print((now - tStart) / 1000.0f, 1);
      Serial.println("s)");
    }
  }
}

char waitForGoodBad()
{
  Serial.println();
  Serial.println("Mark result: press 'g' = GOOD (torque OK / stable), 'b' = BAD (weak/buzz/miss)");
  Serial.println("Tip: you can hold the motor/load lightly to feel torque, but don’t stall it hard.");
  Serial.print("> ");

  while (true) {
    while (Serial.available() == 0) {
      delay(10);
    }
    char c = (char)Serial.read();
    if (c == 'g' || c == 'G') return 'g';
    if (c == 'b' || c == 'B') return 'b';
  }
}

int clampSpeed(int sp)
{
  if (sp < MIN_SPEED) return MIN_SPEED;
  if (sp > MAX_SPEED) return MAX_SPEED;
  return sp;
}

void setup()
{
  Serial.begin(115200);

  // Give USB CDC time (ESP32-S3)
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 1500)) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== 28BYJ-48 ULN2003 MAX SPEED (UP/DOWN) FINDER ===");
  Serial.println("Method: you judge each test as GOOD or BAD; sketch converges to max.");
  Serial.println("Mode  : FULLSTEP");
  Serial.println("Order : IN1-IN3-IN2-IN4");
  Serial.print("Start : ");
  Serial.print(CENTER_SPEED);
  Serial.println(" steps/s");
  Serial.println();

  stepper.enableOutputs();
  stepper.setMaxSpeed((float)MAX_SPEED); // ceiling only (runSpeed uses setSpeed)

  currentSpeed = clampSpeed(currentSpeed);
}

void loop()
{
  Serial.println();
  Serial.print("=== TEST speed = ");
  Serial.print(currentSpeed);
  Serial.print(" steps/s  (stepSize=");
  Serial.print(stepSize);
  Serial.println(") ===");

  lastTestSpeed = currentSpeed;

  // Run forward for half the test, reverse for half (so you validate both directions)
  Serial.println("Forward...");
  runAtSpeedForMs(+currentSpeed, TEST_MS / 2);
  delay(PAUSE_MS);

  Serial.println("Reverse...");
  runAtSpeedForMs(-currentSpeed, TEST_MS / 2);
  delay(PAUSE_MS);

  char verdict = waitForGoodBad();

  if (verdict == 'g') {
    // Record as best-known good
    if (currentSpeed > bestGoodSpeed) bestGoodSpeed = currentSpeed;

    Serial.print("Marked GOOD at ");
    Serial.print(currentSpeed);
    Serial.println(" steps/s.");

    // Push upward
    currentSpeed = clampSpeed(currentSpeed + stepSize);

  } else {
    Serial.print("Marked BAD at ");
    Serial.print(currentSpeed);
    Serial.println(" steps/s.");

    // If it's bad, go down and reduce step size to narrow in
    currentSpeed = clampSpeed(currentSpeed - stepSize);
    stepSize = max(stepSize / 2, MIN_STEP);
  }

  // Stop condition: step size is small and we’ve got a good speed
  if (stepSize <= MIN_STEP && bestGoodSpeed >= 0) {
    Serial.println();
    Serial.println("=== DONE (resolution reached) ===");
    Serial.print("Best GOOD speed you confirmed: ");
    Serial.print(bestGoodSpeed);
    Serial.println(" steps/s");

    Serial.println("Holding forward/reverse continuously at that speed (you can observe stability).");
    Serial.println("Reset board to run the search again.");
    Serial.println();

    while (true) {
      // alternate direction every few seconds at the best speed
      static uint32_t dirT0 = 0;
      static bool forward = true;

      if (millis() - dirT0 > 3000) {
        dirT0 = millis();
        forward = !forward;
        Serial.print("Direction now: ");
        Serial.println(forward ? "FORWARD" : "REVERSE");
      }

      stepper.setSpeed(forward ? (float)bestGoodSpeed : (float)-bestGoodSpeed);
      stepper.runSpeed();
      delay(0);
    }
  }

  delay(200);
}

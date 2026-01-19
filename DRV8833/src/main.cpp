#include <Arduino.h>

/*
DRV8833 + bipolar stepper test (NO PWM, only digitalWrite)

Your wiring:
  D0 -> IN4
  D1 -> IN3
  D2 -> IN2
  D3 -> IN1

DRV8833 mapping:
  IN1/IN2 -> OUT1/OUT2 (Channel A)
  IN3/IN4 -> OUT3/OUT4 (Channel B)

Motor wiring (as you stated):
  A1 -> OUT1
  A2 -> OUT2
  A3 -> OUT3
  A4 -> OUT4

Assumption:
  Coil A = A1-A2 on OUT1/OUT2
  Coil B = A3-A4 on OUT3/OUT4
*/

static const int PIN_IN1 = D3; // IN1
static const int PIN_IN2 = D2; // IN2
static const int PIN_IN3 = D1; // IN3
static const int PIN_IN4 = D0; // IN4

// Start slow; lower = faster (and easier to stall if torque is low)
static const uint16_t STEP_DELAY_MS = 15;
static const uint16_t STEPS_PER_DIR = 200;
static const uint16_t PAUSE_MS = 800;

static void coastAll() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
}

// Drive channel A (OUT1/OUT2) with polarity
// +1: OUT1=H OUT2=L, -1: OUT1=L OUT2=H, 0: coast
static void driveA(int pol) {
  if (pol > 0) { digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW); }
  else if (pol < 0) { digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, HIGH); }
  else { digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW); }
}

// Drive channel B (OUT3/OUT4) with polarity
static void driveB(int pol) {
  if (pol > 0) { digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW); }
  else if (pol < 0) { digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, HIGH); }
  else { digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, LOW); }
}

// Full-step (2-phase ON) sequence: strong holding torque
// idx 0..3: (A,B)
// 0: +,+
// 1: -, +
// 2: -,-
// 3: +,-
static const int8_t FULLSTEP[4][2] = {
  { +1, +1 },
  { -1, +1 },
  { -1, -1 },
  { +1, -1 },
};

static void applyPhase(uint8_t idx) {
  idx &= 3;
  driveA(FULLSTEP[idx][0]);
  driveB(FULLSTEP[idx][1]);
}

static void stepForward(uint16_t steps) {
  uint8_t phase = 0;
  applyPhase(phase);
  delay(STEP_DELAY_MS);

  for (uint16_t i = 0; i < steps; i++) {
    phase = (phase + 1) & 3;
    applyPhase(phase);
    delay(STEP_DELAY_MS);
  }
}

static void stepReverse(uint16_t steps) {
  uint8_t phase = 3;
  applyPhase(phase);
  delay(STEP_DELAY_MS);

  for (uint16_t i = 0; i < steps; i++) {
    phase = (phase + 3) & 3; // -1 mod 4
    applyPhase(phase);
    delay(STEP_DELAY_MS);
  }
}

static void holdTest() {
  Serial.println("Hold A+ B+ (feel shaft resistance) for 2s...");
  applyPhase(0);
  delay(2000);
  Serial.println("Coast 1s...");
  coastAll();
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);

  coastAll();

  Serial.println("\nDRV8833 stepper test (digitalWrite only)");
  Serial.println("Pins: D3->IN1, D2->IN2, D1->IN3, D0->IN4");
}

void loop() {
  holdTest();

  Serial.println("Forward...");
  stepForward(STEPS_PER_DIR);
  coastAll();
  delay(PAUSE_MS);

  Serial.println("Reverse...");
  stepReverse(STEPS_PER_DIR);
  coastAll();
  delay(PAUSE_MS);
}

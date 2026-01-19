// src/main.cpp
#include <Arduino.h>

// ===== MODE =====
#define MODE 1  // keep stepper mode here

// ===== TB6612FNG pin map (your available pins) =====
// STBY is wired to 3V3 (always enabled)
static const int PIN_AIN1 = D0;
static const int PIN_AIN2 = D1;
static const int PIN_BIN1 = D2;
static const int PIN_BIN2 = D3;
static const int PIN_PWMA = D9;   // HIGH for stepper
static const int PIN_PWMB = D10;  // HIGH for stepper

// ===== Stepper tuning =====
static const uint16_t STEP_DELAY_MS = 10;   // try 10..30
static const uint16_t STEPS_PER_DIR = 400;
static const uint16_t PAUSE_MS      = 800;

// ===== Polarity flip flags =====
// Flip coil polarity in software without rewiring.
// false = normal, true = reversed.
static bool FLIP_COIL_A = false;
static bool FLIP_COIL_B = false;

// ===== Helpers =====
static inline void coastA() { digitalWrite(PIN_AIN1, LOW); digitalWrite(PIN_AIN2, LOW); }
static inline void coastB() { digitalWrite(PIN_BIN1, LOW); digitalWrite(PIN_BIN2, LOW); }

static inline void driveA_pol(int pol) { // +1, -1, 0
  if (FLIP_COIL_A) pol = -pol;
  if (pol > 0) { digitalWrite(PIN_AIN1, HIGH); digitalWrite(PIN_AIN2, LOW); }
  else if (pol < 0) { digitalWrite(PIN_AIN1, LOW); digitalWrite(PIN_AIN2, HIGH); }
  else { coastA(); }
}

static inline void driveB_pol(int pol) { // +1, -1, 0
  if (FLIP_COIL_B) pol = -pol;
  if (pol > 0) { digitalWrite(PIN_BIN1, HIGH); digitalWrite(PIN_BIN2, LOW); }
  else if (pol < 0) { digitalWrite(PIN_BIN1, LOW); digitalWrite(PIN_BIN2, HIGH); }
  else { coastB(); }
}

// Full-step (2-phase ON) for torque:
// 0: A+,B+
// 1: A-,B+
// 2: A-,B-
// 3: A+,B-
static const int8_t FULLSTEP[4][2] = {
  { +1, +1 },
  { -1, +1 },
  { -1, -1 },
  { +1, -1 },
};

static void applyPhase(uint8_t idx) {
  idx &= 3;
  driveA_pol(FULLSTEP[idx][0]);
  driveB_pol(FULLSTEP[idx][1]);
}

static void stepRun(bool forward, uint16_t steps) {
  uint8_t phase = forward ? 0 : 3;
  applyPhase(phase);
  delay(STEP_DELAY_MS);

  for (uint16_t i = 0; i < steps; i++) {
    phase = forward ? ((phase + 1) & 3) : ((phase + 3) & 3);
    applyPhase(phase);
    delay(STEP_DELAY_MS);
  }
}

static void stepperCoast() { coastA(); coastB(); }

// Optional: simple serial control to flip polarity at runtime
static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'a' || c == 'A') { FLIP_COIL_A = !FLIP_COIL_A; Serial.printf("FLIP_COIL_A=%d\n", FLIP_COIL_A); }
    if (c == 'b' || c == 'B') { FLIP_COIL_B = !FLIP_COIL_B; Serial.printf("FLIP_COIL_B=%d\n", FLIP_COIL_B); }
    if (c == 'r' || c == 'R') { FLIP_COIL_A = false; FLIP_COIL_B = false; Serial.println("Flips reset."); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_PWMA, OUTPUT);
  pinMode(PIN_PWMB, OUTPUT);

  // Full power for stepper (pins-only)
  digitalWrite(PIN_PWMA, HIGH);
  digitalWrite(PIN_PWMB, HIGH);

  stepperCoast();

  Serial.println("\nTB6612FNG stepper test (pins-only)");
  Serial.println("STBY wired to 3V3. PWMA=D9 HIGH, PWMB=D10 HIGH.");
  Serial.println("Press 'A' to toggle coil A polarity, 'B' for coil B, 'R' reset.");
}

void loop() {
  handleSerial();

  Serial.println("Forward...");
  stepRun(true, STEPS_PER_DIR);
  stepperCoast();
  delay(PAUSE_MS);

  handleSerial();

  Serial.println("Reverse...");
  stepRun(false, STEPS_PER_DIR);
  stepperCoast();
  delay(PAUSE_MS);
}


#include <Arduino.h>
#include <Stepper.h>
//ESP32-S3 MicroPython Stepper 28BYJ48
//Stepper 1
constexpr uint8_t S1_PIN_PHASE1{21};
constexpr uint8_t S1_PIN_PHASE2{47};
constexpr uint8_t S1_PIN_PHASE3{34};
constexpr uint8_t S1_PIN_PHASE4{35};

//Stepper 2
constexpr uint8_t S2_PIN_PHASE1{36};
constexpr uint8_t S2_PIN_PHASE2{48};
constexpr uint8_t S2_PIN_PHASE3{33};
constexpr uint8_t S2_PIN_PHASE4{26};
constexpr uint16_t STEPS_PER_REVOLUTION{2048};

constexpr uint16_t ROTATE_RPM{12};
constexpr uint16_t ROTATE_PAUSE_MS{600};
constexpr uint16_t MELODY_NOTE_GAP_MS{30};

Stepper stepper1(STEPS_PER_REVOLUTION, S1_PIN_PHASE1, S1_PIN_PHASE3, S1_PIN_PHASE2, S1_PIN_PHASE4);
Stepper stepper2(STEPS_PER_REVOLUTION, S2_PIN_PHASE1, S2_PIN_PHASE3, S2_PIN_PHASE2, S2_PIN_PHASE4);
Stepper &stepper = stepper2;

struct Note {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

const Note melody[] = {
  {262, 180}, // C4
  {330, 180}, // E4
  {392, 220}, // G4
  {330, 160}, // E4
};

constexpr size_t MELODY_COUNT = sizeof(melody) / sizeof(melody[0]);

void playToneNoMove(uint16_t frequencyHz, uint16_t durationMs) {
  if (frequencyHz == 0 || durationMs == 0) {
    delay(durationMs);
    return;
  }

  const uint32_t stepsPerSecond = static_cast<uint32_t>(frequencyHz) * 2UL;
  uint16_t toneRpm = (stepsPerSecond * 60UL) / STEPS_PER_REVOLUTION;
  if (toneRpm == 0) {
    toneRpm = 1;
  }

  stepper.setSpeed(toneRpm);

  const uint32_t cycles = (static_cast<uint32_t>(frequencyHz) * durationMs) / 1000UL;
  for (uint32_t i = 0; i < cycles; ++i) {
    stepper.step(1);
    stepper.step(-1);
  }
}

void playMelody() {
  Serial.println("Melodie...");
  for (size_t i = 0; i < MELODY_COUNT; ++i) {
    playToneNoMove(melody[i].frequencyHz, melody[i].durationMs);
    delay(MELODY_NOTE_GAP_MS);
  }

  stepper.setSpeed(ROTATE_RPM);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("ESP32 SuperMini gestartet");
  stepper1.setSpeed(ROTATE_RPM);
  stepper2.setSpeed(ROTATE_RPM);
}

void loop() {
  Serial.println("Stepper 1 rechts");
  stepper1.step(STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  Serial.println("Stepper 1 links");
  stepper1.step(-STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  Serial.println("Stepper 2 rechts");
  stepper2.step(STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  Serial.println("Stepper 2 links");
  stepper2.step(-STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  playMelody();
  delay(400);
}

#include <Arduino.h>
#include <Stepper.h>
//ESP32-S3 MicroPython Stepper 28BYJ48
constexpr uint8_t PIN_PHASE1{21};
constexpr uint8_t PIN_PHASE2{47};
constexpr uint8_t PIN_PHASE3{34};
constexpr uint8_t PIN_PHASE4{35};
constexpr uint16_t STEPS_PER_REVOLUTION{2048};

constexpr uint16_t ROTATE_RPM{12};
constexpr uint16_t ROTATE_PAUSE_MS{600};
constexpr uint16_t MELODY_NOTE_GAP_MS{30};

Stepper stepper(STEPS_PER_REVOLUTION, PIN_PHASE1, PIN_PHASE3, PIN_PHASE2, PIN_PHASE4);

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
  stepper.setSpeed(ROTATE_RPM);
}

void loop() {
  Serial.println("clockwise");
  stepper.step(STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  Serial.println("counterclockwise");
  stepper.step(-STEPS_PER_REVOLUTION);
  delay(ROTATE_PAUSE_MS);

  playMelody();
  delay(400);
}
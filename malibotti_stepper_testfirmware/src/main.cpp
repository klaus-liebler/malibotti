#include <Arduino.h>

constexpr uint8_t DIR_PIN = 4;
constexpr uint8_t STEP_PIN = 10;


// LEDC PWM configurations (ESP32 specific)
const int pwmChannel = 0;
const int pwmResolution = 8; // 8-bit resolution

void setup() {
  // Set pins as outputs
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  // Set motor direction (HIGH = Clockwise, LOW = Counterclockwise)
  digitalWrite(DIR_PIN, HIGH);

  // Configure LEDC PWM channel: channel, frequency, resolution
  ledcSetup(pwmChannel, 500, pwmResolution); // Start at 500 Hz
  
  // Attach the PWM channel to the STEP pin
  ledcAttachPin(STEP_PIN, pwmChannel);
}

void loop() {
  digitalWrite(DIR_PIN, HIGH);
  ledcWriteTone(pwmChannel, 500);
  delay(2000); // Pause
  ledcWriteTone(pwmChannel, 0); // Stop the motor
  delay(500); // Pause

  digitalWrite(DIR_PIN, LOW);
  ledcWriteTone(pwmChannel, 500);
  delay(2000); // Pause
  ledcWriteTone(pwmChannel, 0); // Stop the motor
  delay(500); // Pause
}
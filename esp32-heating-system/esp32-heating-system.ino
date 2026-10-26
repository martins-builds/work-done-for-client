#include <ESP32Servo.h>

Servo clampServo;

#define SERVO_PIN 27

void setup() {
  clampServo.attach(SERVO_PIN);

  // Start open
  clampServo.write(30);

  delay(1000);
}

void loop() {

  // CLOSE
  clampServo.write(110);
  delay(2000);

  // OPEN
  clampServo.write(30);
  delay(2000);
}
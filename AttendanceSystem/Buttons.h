/*
  Buttons - simple debounced reader for the UP/DOWN/ENTER buttons.

  Final wiring spec: active LOW (HIGH idle via pull-up, LOW when pressed).
    UP    -> GPIO32 (internal pull-up used, no external resistor needed)
    DOWN  -> GPIO35 (input-only: needs an external ~10k pull-up to 3.3V)
    ENTER -> GPIO34 (input-only: needs an external ~10k pull-up to 3.3V)
  Each button's other leg goes to GND; pressing pulls the pin LOW.

  These buttons drive the on-device menu (status screens, time, and
  viewing attendance records). Adding/enrolling students is done through
  the web dashboard, since typing a name with three buttons isn't
  practical - there is intentionally no text-entry handling here.
*/
#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "Config.h"

class Buttons {
public:
  void begin() {
    pinMode(PIN_BTN_UP, INPUT_PULLUP);   // GPIO32 supports an internal pull-up
    pinMode(PIN_BTN_DOWN, INPUT);        // GPIO35: needs an external pull-up
    pinMode(PIN_BTN_ENTER, INPUT);       // GPIO34: needs an external pull-up
  }

  // Call once per loop(); returns true once per press (falling edge, debounced).
  bool upPressed()    { return pressed(PIN_BTN_UP, lastUp_, lastUpTime_); }
  bool downPressed()  { return pressed(PIN_BTN_DOWN, lastDown_, lastDownTime_); }
  bool enterPressed() { return pressed(PIN_BTN_ENTER, lastEnter_, lastEnterTime_); }

  // True whenever any button is currently held/was just pressed - handy
  // for resetting an inactivity timer without caring which button it was.
  bool anyActivity() {
    bool up = upPressed(), down = downPressed(), enter = enterPressed();
    return up || down || enter;
  }

private:
  bool lastUp_ = true, lastDown_ = true, lastEnter_ = true;   // idle = HIGH
  uint32_t lastUpTime_ = 0, lastDownTime_ = 0, lastEnterTime_ = 0;
  static const uint32_t DEBOUNCE_MS = 50;

  bool pressed(int pin, bool& lastState, uint32_t& lastTime) {
    bool state = digitalRead(pin) == HIGH;
    bool edge = false;
    if (state != lastState && millis() - lastTime > DEBOUNCE_MS) {
      lastTime = millis();
      if (!state) edge = true;   // falling edge (HIGH -> LOW) = press
      lastState = state;
    }
    return edge;
  }
};

#endif

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include <LiquidCrystal.h>
#include "Config.h"

class LCDDisplay {
public:
  LCDDisplay()
    : lcd_(PIN_LCD_RS, PIN_LCD_E, PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7) {}

  void begin() {
    lcd_.begin(LCD_COLS, LCD_ROWS);
  }

  void showLine(uint8_t row, const String& text) {
    lcd_.setCursor(0, row);
    String padded = text;
    while (padded.length() < LCD_COLS) padded += ' ';
    lcd_.print(padded.substring(0, LCD_COLS));
  }

  void clear() { lcd_.clear(); }

  void showStartup(bool fpOK, bool wifiOK, bool timeOK) {
    clear();
    showLine(0, "BIOMETRIC ATTEND.");
    showLine(1, "Initializing...");
    showLine(2, String("Finger: ") + (fpOK ? "OK" : "FAIL"));
    showLine(3, String("WiFi:") + (wifiOK ? "OK " : "FAIL ") + "Time:" + (timeOK ? "OK" : "FAIL"));
  }

  void showAttendanceOK(const String& name, const String& studentId, const String& timeStr) {
    clear();
    showLine(0, "ATTENDANCE OK");
    showLine(1, name);
    showLine(2, "ID: " + studentId);
    showLine(3, timeStr);
  }

  void showAlreadyLogged(const String& name) {
    clear();
    showLine(0, "ALREADY LOGGED");
    showLine(1, name);
    showLine(2, "Try again later");
  }

  void showWrongLevel(const String& name, const String& sessionLevel, const String& studentLevel) {
    clear();
    showLine(0, "WRONG LEVEL");
    showLine(1, name);
    showLine(2, "Session: " + sessionLevel);
    showLine(3, "Your level: " + studentLevel);
  }

  void showNotRegistered() {
    clear();
    showLine(0, "NOT REGISTERED");
    showLine(1, "Fingerprint:");
    showLine(2, "Not Found");
    showLine(3, "Try Again");
  }

  void showError(const String& msg) {
    clear();
    showLine(0, "ERROR");
    showLine(1, msg);
  }

  void showEnrolling(uint16_t id, const String& status) {
    clear();
    showLine(0, "ENROLLING");
    showLine(1, "ID: " + String(id));
    showLine(2, status);
  }

private:
  LiquidCrystal lcd_;
};

#endif
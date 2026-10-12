/*
  Config.h - pin assignments and system-wide constants.

  Architecture: no RTC, no SD card, no cloud sync - time comes from
  Wi-Fi/NTP (with a flash-based fallback), storage is LittleFS (internal
  flash), and attendance/student data is pulled off the device as CSV via
  the web dashboard's download links instead of pushing to a cloud service.
*/
#ifndef ATT_CONFIG_H
#define ATT_CONFIG_H

// ---- S120 fingerprint module ----
#define PIN_S120_RX   16
#define PIN_S120_TX   17
#define PIN_S120_INT   4

// ---- 16x4 parallel LCD ----
#define PIN_LCD_RS    13
#define PIN_LCD_E     14
#define PIN_LCD_DB4   27
#define PIN_LCD_DB5   26
#define PIN_LCD_DB6   25
#define PIN_LCD_DB7   33
#define LCD_COLS      16
#define LCD_ROWS       4

// ---- Buttons (active LOW: HIGH idle via pull-up/pull-down, LOW when
//      pressed). GPIO32 has an internal pull-up. GPIO34/35 are input-only
//      pins with no internal pull resistors at all, so they each need an
//      external ~10k pull-up to 3.3V, with the button's other leg to GND. ----
#define PIN_BTN_UP     32
#define PIN_BTN_DOWN   35
#define PIN_BTN_ENTER  34

// ---- LittleFS files (internal flash) ----
#define STUDENTS_CSV      "/students.csv"
#define ATTENDANCE_CSV    "/attendance.csv"

// ---- Wi-Fi ----
#define WIFI_SSID       "Galaxy S21+ 5G"
#define WIFI_PASSWORD   "1234567890"
#define WIFI_CONNECT_TIMEOUT_MS   10000
#define WIFI_RETRY_INTERVAL_MS    30000UL

// ---- NTP time ----
#define NTP_SERVER             "pool.ntp.org"
#define NTP_SERVER_BACKUP      "time.google.com"
#define NTP_GMT_OFFSET_SEC     3600   // Lagos/Nigeria: UTC+1, no DST
#define NTP_DST_OFFSET_SEC     0
#define NTP_SYNC_TIMEOUT_MS    8000
#define NTP_RESYNC_INTERVAL_MS 300000UL

// ---- Behavior ----
#define MENU_IDLE_TIMEOUT_MS     20000UL

#endif
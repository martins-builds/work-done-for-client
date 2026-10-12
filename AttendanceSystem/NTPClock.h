/*
  NTPClock - replaces the DS1307 RTC entirely. Time comes from Wi-Fi/NTP.

  Call begin() once, AFTER Wi-Fi has connected. It tries to sync with NTP;
  if that fails (no internet yet), it falls back to the last known good
  time saved in flash (via the Preferences/NVS library), advanced by
  however many seconds have passed since boot. That fallback is only an
  estimate (it drifts by whatever time the device was powered off), but it
  is far better than having no time reference at all after a brief outage.

  Call poll() once per loop() iteration; it is cheap and only does real
  work every NTP_RESYNC_INTERVAL_MS, when it retries NTP (useful if the
  device booted before Wi-Fi was available) and refreshes the saved
  fallback checkpoint whenever a real sync is confirmed.

  Same interface shape as the old RTCClock so the rest of the project
  (LCDDisplay, main sketch) didn't need to change: dateString(),
  timeString(), shortDateString(), shortTimeString().
*/
#ifndef NTP_CLOCK_H
#define NTP_CLOCK_H

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Config.h"

class NTPClock {
public:
  bool begin() {
    configTime(NTP_GMT_OFFSET_SEC, NTP_DST_OFFSET_SEC, NTP_SERVER, NTP_SERVER_BACKUP);

    struct tm t;
    bool synced = getLocalTime(&t, NTP_SYNC_TIMEOUT_MS);

    prefs_.begin("timefallback", false);
    if (synced) {
      ok_ = true;
      fallback_ = false;
      saveCheckpoint();
    } else {
      ok_ = loadFallback();
      fallback_ = ok_;
    }
    prefs_.end();

    lastResyncAttempt_ = millis();
    return ok_;
  }

  // Call once per loop(). Retries NTP periodically (in case the device
  // booted offline) and refreshes the saved fallback checkpoint whenever a
  // real sync succeeds, so the fallback estimate never drifts far behind.
  void poll() {
    uint32_t now = millis();
    if (now - lastResyncAttempt_ < NTP_RESYNC_INTERVAL_MS) return;
    lastResyncAttempt_ = now;

    if (WiFi.status() != WL_CONNECTED) return;

    struct tm t;
    if (getLocalTime(&t, 2000)) {
      ok_ = true;
      fallback_ = false;
      prefs_.begin("timefallback", false);
      saveCheckpoint();
      prefs_.end();
    }
  }

  bool isOk() const { return ok_; }
  // True if the current time is an estimate derived from the last known
  // good checkpoint rather than a live NTP sync.
  bool isFallback() const { return fallback_; }

  String dateString() {   // YYYY-MM-DD
    struct tm t;
    if (!getNow(t)) return "0000-00-00";
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
  }

  String timeString() {   // HH:MM:SS
    struct tm t;
    if (!getNow(t)) return "00:00:00";
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }

  String shortDateString() {   // DD/MM/YY for the LCD ready screen
    struct tm t;
    if (!getNow(t)) return "00/00/00";
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d/%02d/%02d", t.tm_mday, t.tm_mon + 1, (t.tm_year + 1900) % 100);
    return String(buf);
  }

  String shortTimeString() {   // HH:MM for the LCD ready screen
    struct tm t;
    if (!getNow(t)) return "00:00";
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
  }

private:
  Preferences prefs_;
  bool ok_ = false;
  bool fallback_ = false;
  uint32_t lastResyncAttempt_ = 0;

  bool getNow(struct tm& out) {
    if (!ok_) return false;
    time_t now = time(nullptr);
    localtime_r(&now, &out);
    return true;
  }

  void saveCheckpoint() {
    prefs_.putULong("epoch", (uint32_t)time(nullptr));
    prefs_.putULong("savedMillis", millis());
  }

  // Loads the last saved (epoch, millis) pair, estimates how much real
  // time has passed since then using the free-running millis() counter,
  // and sets the system clock to that estimate via settimeofday() so the
  // rest of the class (getNow(), strftime, etc.) works exactly as if a
  // real sync had happened.
  bool loadFallback() {
    if (!prefs_.isKey("epoch")) return false;
    uint32_t savedEpoch = prefs_.getULong("epoch", 0);
    uint32_t savedMillis = prefs_.getULong("savedMillis", 0);
    if (savedEpoch == 0) return false;

    uint32_t elapsedMs = millis() - savedMillis;   // unsigned subtraction wraps safely
    uint32_t estimatedEpoch = savedEpoch + (elapsedMs / 1000UL);

    struct timeval tv;
    tv.tv_sec = estimatedEpoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    return true;
  }
};

#endif

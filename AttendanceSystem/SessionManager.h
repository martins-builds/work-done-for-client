#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <Arduino.h>
#include "NTPClock.h"

class SessionManager {
public:
  SessionManager(NTPClock& clock) : clock_(clock) {}

  void start(const String& level = "ALL") {
    id_ = clock_.dateString() + " " + clock_.timeString();
    level_ = level.length() ? level : "ALL";
    active_ = true;
    count_ = 0;
  }

  void end() { active_ = false; }

  bool isActive() const { return active_; }
  const String& id() const { return id_; }
  const String& level() const { return level_; }

  // Compares levels by their digits only, so "100", "100L", "100 level" and
  // "100lvl" all match. Falls back to a case-insensitive text compare if
  // either side has no digits.
  bool levelMatches(const String& studentLevel) const {
    String sess = level_;
    sess.trim();
    if (sess.equalsIgnoreCase("ALL") || sess.length() == 0) return true;

    String a = digitsOf(sess);
    String b = digitsOf(studentLevel);
    if (a.length() > 0 && b.length() > 0) return a == b;

    String t = studentLevel;
    t.trim();
    return t.equalsIgnoreCase(sess);
  }

  void noteAttendance() { count_++; }
  uint16_t count() const { return count_; }

private:
  static String digitsOf(const String& v) {
    String d;
    for (unsigned i = 0; i < v.length(); i++) {
      if (isDigit(v[i])) d += v[i];
    }
    return d;
  }

  NTPClock& clock_;
  bool     active_ = false;
  String   id_;
  String   level_ = "ALL";
  uint16_t count_ = 0;
};

#endif
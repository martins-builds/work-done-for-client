#ifndef ATTENDANCE_LOG_H
#define ATTENDANCE_LOG_H

#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"

struct AttendanceRecord {
  String   date;
  String   time;
  uint16_t fingerprintId;
  String   studentId;
  String   name;
  String   status;
  String   sessionId;
  String   level;
};

class AttendanceLog {
public:
  bool begin() {
    if (!LittleFS.exists(ATTENDANCE_CSV)) {
      File f = LittleFS.open(ATTENDANCE_CSV, FILE_WRITE);
      if (!f) return false;
      f.println("date,time,fingerprint_id,student_id,name,status,session_id,level");
      f.close();
    }
    return true;
  }

  bool append(const AttendanceRecord& r) {
    File f = LittleFS.open(ATTENDANCE_CSV, FILE_APPEND);
    if (!f) return false;
    f.println(toLine(r));
    f.close();
    return true;
  }

  bool hasRecordInSession(uint16_t fingerprintId, const String& sessionId) {
    File f = LittleFS.open(ATTENDANCE_CSV, FILE_READ);
    if (!f) return false;
    f.readStringUntil('\n');
    bool found = false;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      AttendanceRecord r;
      if (parseLine(line, r) && r.fingerprintId == fingerprintId && r.sessionId == sessionId) {
        found = true;
        break;
      }
    }
    f.close();
    return found;
  }

  bool parseLine(const String& line, AttendanceRecord& r) {
    int idx[7];
    int start = 0;
    for (int i = 0; i < 7; i++) {
      idx[i] = line.indexOf(',', start);
      if (idx[i] < 0) return false;
      start = idx[i] + 1;
    }
    r.date          = line.substring(0, idx[0]);
    r.time          = line.substring(idx[0] + 1, idx[1]);
    r.fingerprintId = line.substring(idx[1] + 1, idx[2]).toInt();
    r.studentId     = line.substring(idx[2] + 1, idx[3]);
    r.name          = line.substring(idx[3] + 1, idx[4]);
    r.status        = line.substring(idx[4] + 1, idx[5]);
    r.sessionId     = line.substring(idx[5] + 1, idx[6]);
    r.level         = line.substring(idx[6] + 1);
    return true;
  }

private:
  String toLine(const AttendanceRecord& r) {
    return r.date + "," + r.time + "," + String(r.fingerprintId) + "," +
          r.studentId + "," + r.name + "," + r.status + "," + r.sessionId +
          "," + r.level;
  }
};

#endif
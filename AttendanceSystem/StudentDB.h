/*
  StudentDB - maps a fingerprint ID to a student record.

  Stored as a simple CSV on LittleFS (the ESP32's internal flash
  filesystem - replaces the SD card, which proved unreliable on hardware):
    fingerprint_id,student_id,name,class,active

  Kept independent of the S120Fingerprint library on purpose: the library
  only knows about raw fingerprint IDs, this module is where an ID becomes
  "John Doe, Grade 10".
*/
#ifndef STUDENT_DB_H
#define STUDENT_DB_H

#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"

struct Student {
  uint16_t fingerprintId;
  String   studentId;
  String   name;
  String   className;
  bool     active;
};

class StudentDB {
public:
  bool begin() {
    if (!LittleFS.exists(STUDENTS_CSV)) {
      File f = LittleFS.open(STUDENTS_CSV, FILE_WRITE);
      if (!f) return false;
      f.println("fingerprint_id,student_id,name,class,active");
      f.close();
    }
    return true;
  }

  // Linear scan; fine for a few hundred students in a flash-backed file.
  bool findByFingerprintId(uint16_t id, Student& out) {
    File f = LittleFS.open(STUDENTS_CSV, FILE_READ);
    if (!f) return false;
    f.readStringUntil('\n');   // skip header
    bool found = false;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      Student s;
      if (parseLine(line, s) && s.fingerprintId == id) {
        out = s;
        found = true;
        break;
      }
    }
    f.close();
    return found;
  }

  bool addOrUpdate(const Student& s) {
    // Rewrite the file, replacing any existing row for this fingerprint ID.
    File in = LittleFS.open(STUDENTS_CSV, FILE_READ);
    if (!in) return false;

    File out = LittleFS.open("/students.tmp", FILE_WRITE);
    if (!out) { in.close(); return false; }

    String header = in.readStringUntil('\n');
    header.trim();               // println adds \r\n; without trim the header grew a stray \r on every save
    out.println(header);

    bool replaced = false;
    while (in.available()) {
      String line = in.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      Student existing;
      if (parseLine(line, existing) && existing.fingerprintId == s.fingerprintId) {
        out.println(toLine(s));
        replaced = true;
      } else {
        out.println(line);
      }
    }
    if (!replaced) out.println(toLine(s));

    in.close();
    out.close();
    LittleFS.remove(STUDENTS_CSV);
    if (!LittleFS.rename("/students.tmp", STUDENTS_CSV)) return false;
    return true;
  }

  bool deactivate(uint16_t fingerprintId) {
    Student s;
    if (!findByFingerprintId(fingerprintId, s)) return false;
    s.active = false;
    return addOrUpdate(s);
  }

  // Public so the LCD "View Students"-style features (or future ones) can
  // reuse the same parsing logic without duplicating it.
  bool parseLine(const String& line, Student& s) {
    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) return false;
    s.fingerprintId = line.substring(0, p1).toInt();
    s.studentId     = line.substring(p1 + 1, p2);
    s.name          = line.substring(p2 + 1, p3);
    s.className     = line.substring(p3 + 1, p4);
    s.active        = line.substring(p4 + 1).toInt() != 0;
    return true;
  }

private:
  // Commas/newlines inside a field would shift the CSV columns and corrupt the row.
  static String clean(String v) {
    v.replace(",", " ");
    v.replace("\r", " ");
    v.replace("\n", " ");
    v.replace("\"", "'");
    v.trim();
    return v;
  }

  String toLine(const Student& s) {
    String l = String(s.fingerprintId) + "," + clean(s.studentId) + "," + clean(s.name) +
              "," + clean(s.className) + "," + (s.active ? "1" : "0");
    return l;
  }
};

#endif

#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "Config.h"
#include "StudentDB.h"
#include "AttendanceLog.h"
#include "SessionManager.h"

struct EnrollRequest {
  volatile bool    pending = false;
  uint16_t         fingerprintId = 0;
  String           studentId;
  String           name;
  String           className;
};

struct EnrollStatus {
  volatile bool inProgress = false;
  String        stageText  = "Idle";
  volatile bool lastResultKnown = false;
  volatile bool lastResultOk    = false;
};

class WebDashboard {
public:
  WebDashboard(StudentDB& db, AttendanceLog& log, EnrollRequest& req, EnrollStatus& status, SessionManager& session)
    : server_(80), db_(db), log_(log), enrollReq_(req), enrollStatus_(status), session_(session) {}

  void begin() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/students", HTTP_GET, [this]() { handleListStudents(); });
    server_.on("/api/students", HTTP_POST, [this]() { handleAddStudent(); });
    server_.on("/api/enroll", HTTP_POST, [this]() { handleEnroll(); });
    server_.on("/api/enroll/status", HTTP_GET, [this]() { handleEnrollStatus(); });
    server_.on("/api/session/start", HTTP_POST, [this]() { handleSessionStart(); });
    server_.on("/api/session/end", HTTP_POST, [this]() { handleSessionEnd(); });
    server_.on("/api/session/status", HTTP_GET, [this]() { handleSessionStatus(); });
    server_.on("/api/attendance", HTTP_GET, [this]() { handleAttendance(); });
    server_.on("/download/students", HTTP_GET, [this]() { handleDownloadStudents(); });
    server_.on("/download/attendance", HTTP_GET, [this]() { handleDownloadAttendance(); });
    server_.begin();
  }

  void handleClient() { server_.handleClient(); }

private:
  WebServer server_;
  StudentDB& db_;
  AttendanceLog& log_;
  EnrollRequest& enrollReq_;
  EnrollStatus& enrollStatus_;
  SessionManager& session_;

  void handleRoot() {
    const char* page = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Attendance Dashboard</title>
<style>
body{font-family:sans-serif;margin:1em;background:#f4f4f4}
h2{margin-top:1.5em}
table{width:100%;border-collapse:collapse;background:#fff}
th,td{border:1px solid #ccc;padding:6px;font-size:14px;text-align:left}
input,select,button{padding:6px;margin:3px 0;font-size:14px}
.card{background:#fff;padding:1em;border-radius:8px;margin-bottom:1em}
#enrollStatus,#sessionStatus{font-weight:bold}
#addStudentError{color:#c5221f;font-weight:bold}
.active{color:#188038}
.inactive{color:#c5221f}
a button{width:100%}
</style></head><body>
<h1>Attendance Dashboard</h1>

<div class="card">
<h2>Session</h2>
<p id="sessionStatus">Loading...</p>
<select id="sessionLevel">
<option value="ALL">All Levels</option>
<option value="100L">100L</option>
<option value="200L">200L</option>
<option value="300L">300L</option>
<option value="400L">400L</option>
<option value="500L">500L</option>
<option value="600L">600L</option>
</select><br>
<button onclick="startSession()">Start Session</button>
<button onclick="endSession()">End Session</button>
</div>

<div class="card">
<h2>Add Student</h2>
<input id="fpId" type="number" min="1" placeholder="Fingerprint ID (required, must be unique)"><br>
<input id="stuId" placeholder="Student ID (e.g. STU001)"><br>
<input id="name" placeholder="Full name"><br>
<input id="cls" placeholder="Level (e.g. 200L)"><br>
<button onclick="addStudent()">Save Student</button>
<p id="addStudentError"></p>
</div>

<div class="card">
<h2>Enrollment</h2>
<p style="font-size:13px;color:#555">Fill in the student details above, then enter (or leave blank to reuse) the Fingerprint ID. Details are saved when the scan succeeds.</p>
<input id="enrollFpId" type="number" min="1" placeholder="Fingerprint ID to enroll (defaults to ID above)"><br>
<button onclick="startEnroll()">Start Enrollment</button>
<p id="enrollStatus">Idle</p>
</div>

<div class="card">
<h2>Students</h2>
<table id="studentTable"><tr><th>FP ID</th><th>Student ID</th><th>Name</th><th>Level</th><th>Active</th></tr></table>
</div>

<div class="card">
<h2>Recent Attendance</h2>
<table id="attTable"><tr><th>Session</th><th>Level</th><th>Time</th><th>Student</th><th>Name</th></tr></table>
</div>

<div class="card">
<h2>Export Data</h2>
<select id="dlLevel">
<option value="all">All Levels</option>
<option value="100L">100L</option>
<option value="200L">200L</option>
<option value="300L">300L</option>
<option value="400L">400L</option>
<option value="500L">500L</option>
<option value="600L">600L</option>
</select><br>
<button onclick="downloadStudents()">Download Students CSV</button>
<button onclick="downloadAttendance()">Download Attendance CSV</button>
</div>

<script>
async function addStudent(){
  const errEl = document.getElementById('addStudentError');
  errEl.innerText = '';
  errEl.style.color = '';
  const body = {
    fingerprint_id: document.getElementById('fpId').value,
    student_id: document.getElementById('stuId').value,
    name: document.getElementById('name').value,
    class: document.getElementById('cls').value
  };
  const r = await fetch('/api/students', {method:'POST', body: JSON.stringify(body)});
  const j = await r.json();
  if (!j.ok) { errEl.innerText = j.error || 'Failed to save student.'; return; }
  errEl.style.color = '#188038'; errEl.innerText = 'Saved.'; setTimeout(()=>{errEl.innerText='';errEl.style.color='';},2500);
  document.getElementById('fpId').value = '';
  document.getElementById('stuId').value = '';
  document.getElementById('name').value = '';
  document.getElementById('cls').value = '';
  loadStudents();
}
async function startEnroll(){
  const errEl = document.getElementById('addStudentError');
  errEl.innerText = '';
  let id = document.getElementById('enrollFpId').value || document.getElementById('fpId').value;
  document.getElementById('enrollFpId').value = id;
  const body = {
    fingerprint_id: id,
    student_id: document.getElementById('stuId').value,
    name: document.getElementById('name').value,
    class: document.getElementById('cls').value
  };
  const r = await fetch('/api/enroll', {method:'POST', body: JSON.stringify(body)});
  const j = await r.json();
  if (!j.ok) { document.getElementById('enrollStatus').innerText = j.error || 'Could not start enrollment.'; return; }
  document.getElementById('enrollStatus').innerText = 'Starting... follow the prompts on the device';
  setTimeout(pollEnroll, 800);
}
async function pollEnroll(){
  const r = await fetch('/api/enroll/status');
  const j = await r.json();
  document.getElementById('enrollStatus').innerText = j.stage;
  if (j.inProgress) setTimeout(pollEnroll, 1000);
  else loadStudents();
}
async function startSession(){
  const level = document.getElementById('sessionLevel').value;
  await fetch('/api/session/start', {method:'POST', body: JSON.stringify({level:level})});
  loadSession();
}
async function endSession(){
  await fetch('/api/session/end', {method:'POST'});
  loadSession();
}
async function loadSession(){
  const r = await fetch('/api/session/status');
  const j = await r.json();
  const el = document.getElementById('sessionStatus');
  if (j.active) {
    el.innerHTML = '<span class="active">ACTIVE</span> - ' + j.sessionId + ' [' + j.level + '] (' + j.count + ' logged)';
  } else {
    el.innerHTML = '<span class="inactive">No active session</span>';
  }
}
async function loadStudents(){
  const r = await fetch('/api/students');
  const j = await r.json();
  const t = document.getElementById('studentTable');
  t.innerHTML = '<tr><th>FP ID</th><th>Student ID</th><th>Name</th><th>Level</th><th>Active</th></tr>';
  j.forEach(s => t.innerHTML += `<tr><td>${s.fingerprint_id}</td><td>${s.student_id}</td><td>${s.name}</td><td>${s.class}</td><td>${s.active?'Yes':'No'}</td></tr>`);
}
async function loadAttendance(){
  const r = await fetch('/api/attendance');
  const j = await r.json();
  const t = document.getElementById('attTable');
  t.innerHTML = '<tr><th>Session</th><th>Level</th><th>Time</th><th>Student</th><th>Name</th></tr>';
  j.forEach(a => t.innerHTML += `<tr><td>${a.session_id}</td><td>${a.level}</td><td>${a.time}</td><td>${a.student_id}</td><td>${a.name}</td></tr>`);
}
function downloadStudents(){
  const lvl = document.getElementById('dlLevel').value;
  window.location = '/download/students?level=' + encodeURIComponent(lvl);
}
function downloadAttendance(){
  const lvl = document.getElementById('dlLevel').value;
  window.location = '/download/attendance?level=' + encodeURIComponent(lvl);
}
loadStudents(); loadAttendance(); loadSession();
setInterval(loadAttendance, 5000);
setInterval(loadSession, 5000);
</script>
</body></html>
)HTML";
    server_.send(200, "text/html", page);
  }

  void handleListStudents() {
    File f = LittleFS.open(STUDENTS_CSV, FILE_READ);
    String json = "[";
    if (f) {
      f.readStringUntil('\n');
      bool first = true;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int p1 = line.indexOf(','), p2 = line.indexOf(',', p1+1),
            p3 = line.indexOf(',', p2+1), p4 = line.indexOf(',', p3+1);
        if (p1<0||p2<0||p3<0||p4<0) continue;
        if (!first) json += ",";
        first = false;
        json += "{\"fingerprint_id\":" + line.substring(0,p1) +
                ",\"student_id\":\"" + line.substring(p1+1,p2) + "\"" +
                ",\"name\":\"" + line.substring(p2+1,p3) + "\"" +
                ",\"class\":\"" + line.substring(p3+1,p4) + "\"" +
                ",\"active\":" + (line.substring(p4+1).toInt()!=0 ? "true":"false") + "}";
      }
      f.close();
    }
    json += "]";
    server_.send(200, "application/json", json);
  }

  void handleAddStudent() {
    String body = server_.arg("plain");
    Student s;
    long fpIdRaw    = extractInt(body, "fingerprint_id");
    s.fingerprintId = (fpIdRaw >= 1 && fpIdRaw <= 65535) ? (uint16_t)fpIdRaw : 0;
    s.studentId     = extractString(body, "student_id");
    s.name          = extractString(body, "name");
    s.className     = extractString(body, "class");
    s.active        = true;

    if (s.fingerprintId < 1) {
      server_.send(400, "application/json",
        "{\"ok\":false,\"error\":\"Fingerprint ID must be a positive number. Leaving it blank saves everyone under ID 0 and overwrites the previous student.\"}");
      return;
    }
    if (s.studentId.length() == 0 || s.name.length() == 0) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"Student ID and name are required.\"}");
      return;
    }

    Student existing;
    bool idTaken = db_.findByFingerprintId(s.fingerprintId, existing);
    if (idTaken && existing.studentId != s.studentId) {
      server_.send(409, "application/json",
        "{\"ok\":false,\"error\":\"Fingerprint ID " + String(s.fingerprintId) + " is already assigned to " + existing.name +
        " (" + existing.studentId + "). Use a different ID, or deactivate/remove that student first.\"}");
      return;
    }

    bool ok = db_.addOrUpdate(s);
    server_.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Could not write to storage.\"}");
  }

  void handleEnroll() {
    String body = server_.arg("plain");
    long idRaw = extractInt(body, "fingerprint_id");
    if (enrollStatus_.inProgress) {
      server_.send(409, "application/json", "{\"ok\":false,\"error\":\"enrollment already in progress\"}");
      return;
    }
    if (idRaw < 1 || idRaw > 65535) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"fingerprint_id must be a positive number\"}");
      return;
    }
    uint16_t id = (uint16_t)idRaw;

    // Student details are optional here. If given, they are saved once the scan succeeds.
    String stuId = extractString(body, "student_id");
    String name  = extractString(body, "name");
    String cls   = extractString(body, "class");

    if (stuId.length() > 0 || name.length() > 0) {
      Student existing;
      if (db_.findByFingerprintId(id, existing) && existing.studentId != stuId) {
        server_.send(409, "application/json",
          "{\"ok\":false,\"error\":\"Fingerprint ID " + String(id) + " is already assigned to " + existing.name + ".\"}");
        return;
      }
    }

    enrollReq_.fingerprintId = id;
    enrollReq_.studentId = stuId;
    enrollReq_.name      = name;
    enrollReq_.className = cls;
    enrollReq_.pending = true;
    server_.send(200, "application/json", "{\"ok\":true}");
  }

  void handleEnrollStatus() {
    String json = String("{\"inProgress\":") + (enrollStatus_.inProgress ? "true" : "false") +
                  ",\"stage\":\"" + enrollStatus_.stageText + "\"" +
                  ",\"lastResultKnown\":" + (enrollStatus_.lastResultKnown ? "true" : "false") +
                  ",\"lastResultOk\":" + (enrollStatus_.lastResultOk ? "true" : "false") + "}";
    server_.send(200, "application/json", json);
  }

  void handleSessionStart() {
    String body = server_.arg("plain");
    String level = extractString(body, "level");
    if (level.length() == 0) level = "ALL";
    session_.start(level);
    server_.send(200, "application/json",
      "{\"ok\":true,\"sessionId\":\"" + session_.id() + "\",\"level\":\"" + session_.level() + "\"}");
  }

  void handleSessionEnd() {
    session_.end();
    server_.send(200, "application/json", "{\"ok\":true}");
  }

  void handleSessionStatus() {
    String json = String("{\"active\":") + (session_.isActive() ? "true" : "false") +
                  ",\"sessionId\":\"" + session_.id() + "\"" +
                  ",\"level\":\"" + session_.level() + "\"" +
                  ",\"count\":" + String(session_.count()) + "}";
    server_.send(200, "application/json", json);
  }

  void handleAttendance() {
    File f = LittleFS.open(ATTENDANCE_CSV, FILE_READ);
    String json = "[";
    if (f) {
      f.readStringUntil('\n');
      const int MAXN = 30;
      AttendanceRecord recs[MAXN];
      int count = 0, total = 0;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        AttendanceRecord r;
        if (!log_.parseLine(line, r)) continue;
        recs[total % MAXN] = r;
        total++;
      }
      f.close();
      count = min(total, MAXN);
      bool first = true;
      for (int i = 0; i < count; i++) {
        int idx = (total - count + i) % MAXN;
        AttendanceRecord& r = recs[idx];
        if (!first) json += ",";
        first = false;
        json += "{\"date\":\"" + r.date + "\"" +
                ",\"time\":\"" + r.time + "\"" +
                ",\"fingerprint_id\":" + String(r.fingerprintId) +
                ",\"student_id\":\"" + r.studentId + "\"" +
                ",\"name\":\"" + r.name + "\"" +
                ",\"session_id\":\"" + r.sessionId + "\"" +
                ",\"level\":\"" + r.level + "\"}";
      }
    }
    json += "]";
    server_.send(200, "application/json", json);
  }

  void handleDownloadStudents() {
    String level = server_.hasArg("level") ? server_.arg("level") : "all";
    File f = LittleFS.open(STUDENTS_CSV, FILE_READ);
    if (!f) { server_.send(404, "text/plain", "students.csv not found"); return; }

    if (level == "all" || level.length() == 0) {
      server_.sendHeader("Content-Disposition", "attachment; filename=students.csv");
      server_.streamFile(f, "text/csv");
      f.close();
      return;
    }

    String out = "fingerprint_id,student_id,name,class,active\n";
    f.readStringUntil('\n');
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int p1 = line.indexOf(','), p2 = line.indexOf(',', p1+1),
          p3 = line.indexOf(',', p2+1), p4 = line.indexOf(',', p3+1);
      if (p1<0||p2<0||p3<0||p4<0) continue;
      if (line.substring(p3+1, p4) != level) continue;
      out += line + "\n";
    }
    f.close();

    server_.sendHeader("Content-Disposition", "attachment; filename=students_" + level + ".csv");
    server_.send(200, "text/csv", out);
  }

  void handleDownloadAttendance() {
    String level = server_.hasArg("level") ? server_.arg("level") : "all";
    File f = LittleFS.open(ATTENDANCE_CSV, FILE_READ);
    if (!f) { server_.send(404, "text/plain", "attendance.csv not found"); return; }

    if (level == "all" || level.length() == 0) {
      server_.sendHeader("Content-Disposition", "attachment; filename=attendance.csv");
      server_.streamFile(f, "text/csv");
      f.close();
      return;
    }

    String out = "date,time,fingerprint_id,student_id,name,status,session_id,level\n";
    f.readStringUntil('\n');
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      AttendanceRecord r;
      if (!log_.parseLine(line, r)) continue;
      if (r.level != level) continue;
      out += line + "\n";
    }
    f.close();

    server_.sendHeader("Content-Disposition", "attachment; filename=attendance_" + level + ".csv");
    server_.send(200, "text/csv", out);
  }

  long extractInt(const String& json, const String& key) {
    int i = json.indexOf("\"" + key + "\"");
    if (i < 0) return -1;
    i = json.indexOf(':', i) + 1;
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '"')) i++;
    String numStr = json.substring(i);
    if (numStr.length() == 0 || !(isDigit(numStr[0]) || numStr[0] == '-')) return -1;
    return numStr.toInt();
  }
  String extractString(const String& json, const String& key) {
    int i = json.indexOf("\"" + key + "\"");
    if (i < 0) return "";
    i = json.indexOf(':', i) + 1;
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '"')) i++;
    int end = json.indexOf('"', i);
    if (end < 0) end = json.length();
    return json.substring(i, end);
  }
};

#endif
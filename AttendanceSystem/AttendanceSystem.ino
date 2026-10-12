#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "Config.h"
#include "S120Fingerprint.h"
#include "LCDDisplay.h"
#include "Buttons.h"
#include "NTPClock.h"
#include "SessionManager.h"
#include "StudentDB.h"
#include "AttendanceLog.h"
#include "WebDashboard.h"

LCDDisplay     lcd;
Buttons        buttons;
NTPClock       clock_;
SessionManager session(clock_);
StudentDB      studentDB;
AttendanceLog  attLog;

HardwareSerial  S120Serial(2);
S120Fingerprint finger(S120Serial, PIN_S120_INT);

EnrollRequest enrollReq;
EnrollStatus  enrollStatus;
WebDashboard  dashboard(studentDB, attLog, enrollReq, enrollStatus, session);

bool fpOk   = false;
bool wifiOk = false;

// Fingerprint sensor arming state. The sensor is put to sleep + INT armed ONCE,
// then we wait for a touch. (Previously this was redone every loop pass, which
// kept re-sleeping the sensor and clearing the touch flag, so touches were missed.)
bool sensorArmed        = false;
bool lastSessionActive  = false;


uint32_t lastWifiRetry = 0;

struct MenuItem;
struct Menu {
  const char* title;
  MenuItem*   items;
  uint8_t     itemCount;
  Menu*       parent;
};
struct MenuItem {
  const char* label;
  Menu*       submenu;
  void (*action)();
};

Menu*  currentMenu = nullptr;
int8_t selectedIndex = 0;

void actionStartSession();
void actionEndSession();
void actionSessionStatus();
void actionViewToday();
void actionViewAll();
void actionWifiStatus();
void actionTimeStatus();
void actionFingerprintStatus();
void actionSystemInfo();

MenuItem sessionItems[] = {
  { "Start Session",  nullptr, actionStartSession },
  { "End Session",    nullptr, actionEndSession },
  { "Session Status", nullptr, actionSessionStatus },
};
Menu sessionMenu = { "Session", sessionItems, 3, nullptr };

MenuItem recordsItems[] = {
  { "Today",  nullptr, actionViewToday },
  { "All",    nullptr, actionViewAll   },
};
Menu recordsMenu = { "View Records", recordsItems, 2, nullptr };

MenuItem statusItems[] = {
  { "WiFi",        nullptr, actionWifiStatus },
  { "Time",        nullptr, actionTimeStatus },
  { "Fingerprint", nullptr, actionFingerprintStatus },
};
Menu statusMenu = { "Status", statusItems, 3, nullptr };

MenuItem rootItems[] = {
  { "Session",       &sessionMenu, nullptr },
  { "View Records",  &recordsMenu, nullptr },
  { "Status",        &statusMenu,  nullptr },
  { "System Info",   nullptr,      actionSystemInfo },
};
Menu rootMenu = { "Main Menu", rootItems, 4, nullptr };

void armSensor() {
  if (!fpOk) return;
  finger.sleepAndArm();
  sensorArmed = true;
}

bool hasBackRow() { return currentMenu->parent != nullptr; }
uint8_t totalRows() { return currentMenu->itemCount + (hasBackRow() ? 1 : 0); }

String rowLabel(uint8_t row) {
  if (hasBackRow()) {
    if (row == 0) return "< Back";
    return currentMenu->items[row - 1].label;
  }
  return currentMenu->items[row].label;
}

void enterMenu(Menu* m) { currentMenu = m; selectedIndex = 0; }

void goBack() {
  if (currentMenu->parent != nullptr) { currentMenu = currentMenu->parent; selectedIndex = 0; }
}

void renderMenu() {
  lcd.clear();
  uint8_t total = totalRows();
  uint8_t topRow = 0;
  if (selectedIndex >= LCD_ROWS) topRow = selectedIndex - LCD_ROWS + 1;

  for (uint8_t visibleRow = 0; visibleRow < LCD_ROWS; visibleRow++) {
    uint8_t row = topRow + visibleRow;
    if (row >= total) break;
    String label = rowLabel(row);
    lcd.showLine(visibleRow, (row == selectedIndex ? ">" : " ") + label);
  }
}

void selectCurrentRow() {
  if (hasBackRow() && selectedIndex == 0) { goBack(); renderMenu(); return; }
  uint8_t itemIdx = selectedIndex - (hasBackRow() ? 1 : 0);
  MenuItem& item = currentMenu->items[itemIdx];
  if (item.submenu != nullptr) {
    enterMenu(item.submenu);
    renderMenu();
  } else if (item.action != nullptr) {
    item.action();
    renderMenu();
  }
}

void handleMenuButtons() {
  bool changed = false;
  if (buttons.upPressed()) {
    if (selectedIndex > 0) selectedIndex--;
    else selectedIndex = totalRows() - 1;
    changed = true;
  }
  if (buttons.downPressed()) {
    if (selectedIndex < totalRows() - 1) selectedIndex++;
    else selectedIndex = 0;
    changed = true;
  }
  if (buttons.enterPressed()) {
    selectCurrentRow();
    changed = true;
  }
  if (changed) renderMenu();
}

void actionStartSession() {
  lcd.clear();
  if (session.isActive()) {
    lcd.showLine(0, "Session already");
    lcd.showLine(1, "active:");
    lcd.showLine(2, session.id());
    lcd.showLine(3, "[" + session.level() + "]");
  } else {
    session.start();
    lcd.showLine(0, "Session started");
    lcd.showLine(1, session.id());
    lcd.showLine(2, "[All Levels]");
  }
  delay(2000);
}

void actionEndSession() {
  lcd.clear();
  if (!session.isActive()) {
    lcd.showLine(0, "No active session");
  } else {
    lcd.showLine(0, "Session ended");
    lcd.showLine(1, session.id());
    lcd.showLine(2, String(session.count()) + " student(s) logged");
    session.end();
  }
  delay(2500);
}

void actionSessionStatus() {
  lcd.clear();
  if (session.isActive()) {
    lcd.showLine(0, "ACTIVE [" + session.level() + "]");
    lcd.showLine(1, session.id());
    lcd.showLine(2, String(session.count()) + " logged so far");
  } else {
    lcd.showLine(0, "No active session");
    lcd.showLine(1, "Start one to take");
    lcd.showLine(2, "attendance");
  }
  delay(2500);
}

void viewRecordsScreen(bool todayOnly) {
  const int MAXN = 100;
  static AttendanceRecord recs[MAXN];
  int count = 0;
  String todayStr = clock_.dateString();

  File f = LittleFS.open(ATTENDANCE_CSV, FILE_READ);
  if (f) {
    f.readStringUntil('\n');
    while (f.available() && count < MAXN) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      AttendanceRecord r;
      if (!attLog.parseLine(line, r)) continue;
      if (todayOnly && r.date != todayStr) continue;
      recs[count++] = r;
    }
    f.close();
  }

  if (count == 0) {
    lcd.clear();
    lcd.showLine(0, todayOnly ? "No records today" : "No records found");
    lcd.showLine(1, "Press ENTER");
    while (!buttons.enterPressed()) { delay(10); }
    return;
  }

  const int perPage = 3;
  int pages = (count + perPage - 1) / perPage;
  int page = 0;
  bool redraw = true;

  while (true) {
    if (redraw) {
      lcd.clear();
      lcd.showLine(0, "Rec " + String(page + 1) + "/" + String(pages) + (todayOnly ? " Today" : " All"));
      for (int i = 0; i < perPage; i++) {
        int idx = page * perPage + i;
        if (idx >= count) break;
        AttendanceRecord& r = recs[idx];
        lcd.showLine(i + 1, r.time + " " + r.name.substring(0, 10));
      }
      redraw = false;
    }
    if (buttons.upPressed())   { page = (page > 0) ? page - 1 : pages - 1; redraw = true; }
    if (buttons.downPressed()) { page = (page < pages - 1) ? page + 1 : 0; redraw = true; }
    if (buttons.enterPressed()) break;
    delay(10);
  }
}

void actionViewToday() { viewRecordsScreen(true); }
void actionViewAll()   { viewRecordsScreen(false); }

void actionWifiStatus() {
  lcd.clear();
  lcd.showLine(0, "WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
  if (WiFi.status() == WL_CONNECTED) {
    lcd.showLine(1, WiFi.SSID());
    lcd.showLine(2, WiFi.localIP().toString());
  }
  delay(2500);
}

void actionTimeStatus() {
  lcd.clear();
  lcd.showLine(0, clock_.isOk() ? "Time: OK" : "Time: NOT SET");
  lcd.showLine(1, clock_.dateString());
  lcd.showLine(2, clock_.timeString());
  lcd.showLine(3, clock_.isFallback() ? "(fallback est.)" : "(NTP synced)");
  delay(2500);
}

void actionFingerprintStatus() {
  lcd.clear();
  lcd.showLine(0, fpOk ? "Fingerprint: OK" : "Fingerprint: FAIL");
  lcd.showLine(1, "Capacity: " + String(finger.capacity()));
  delay(2000);
}

void actionSystemInfo() {
  lcd.clear();
  lcd.showLine(0, "FP: " + String(fpOk ? "OK" : "FAIL") + " Cap:" + String(finger.capacity()));
  lcd.showLine(1, "WiFi: " + String(wifiOk ? "OK" : "FAIL"));
  lcd.showLine(2, "Time: " + String(clock_.isOk() ? (clock_.isFallback() ? "fallback" : "OK") : "FAIL"));
  lcd.showLine(3, session.isActive() ? ("Session: " + session.level()) : "Session: none");
  delay(2500);
}

bool connectWifi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

void handleAttendanceFlow() {
  if (!fpOk) return;

  // Session just started or ended (from the LCD menu OR the web dashboard):
  // re-arm the sensor right away so scanning works immediately.
  bool active = session.isActive();
  if (active != lastSessionActive) {
    lastSessionActive = active;
    sensorArmed = false;
    if (active) {
      lcd.clear();
      lcd.showLine(0, "Session active");
      lcd.showLine(1, "Place finger on");
      lcd.showLine(2, "the scanner");
    }
  }

  if (!sensorArmed) {
    armSensor();
    return;
  }

  if (!finger.touchDetected()) return;

  sensorArmed = false;   // sensor is awake after a touch; re-arm after handling it

  S120Match m = finger.identifyResult();

  if (!m.matched) {
    if (m.error == S120_ERR_NOT_FOUND) lcd.showNotRegistered();
    else lcd.showError(finger.errorString());
    delay(2000);
    finger.waitForRemoval();
    renderMenu();
    return;
  }

  Student s;
  bool known = studentDB.findByFingerprintId(m.id, s);

  if (!known || !s.active) {
    lcd.showNotRegistered();
    delay(2000);
  } else if (!session.isActive()) {
    lcd.clear();
    lcd.showLine(0, "NO ACTIVE SESSION");
    lcd.showLine(1, s.name);
    lcd.showLine(2, "Start a session");
    lcd.showLine(3, "to log attendance");
    delay(2500);
  } else if (!session.levelMatches(s.className)) {
    lcd.showWrongLevel(s.name, session.level(), s.className);
    delay(2500);
  } else if (attLog.hasRecordInSession(m.id, session.id())) {
    lcd.showAlreadyLogged(s.name);
    delay(2000);
  } else {
    AttendanceRecord r;
    r.date = clock_.dateString();
    r.time = clock_.timeString();
    r.fingerprintId = m.id;
    r.studentId = s.studentId;
    r.name = s.name;
    r.status = "PRESENT";
    r.sessionId = session.id();
    r.level = session.level();
    attLog.append(r);
    session.noteAttendance();
    lcd.showAttendanceOK(s.name, s.studentId, r.time);
    delay(2000);
  }

  finger.waitForRemoval();
  renderMenu();
}

bool onEnrollProgress(const S120EnrollProgress& p, void* /*user*/) {
  enrollStatus.stageText = S120Fingerprint::enrollStageName(p.stage);
  if (p.sample) enrollStatus.stageText += " (" + String(p.sample) + "/" + String(p.totalSamples) + ")";
  lcd.showEnrolling(enrollReq.fingerprintId, enrollStatus.stageText);
  return true;
}

void handleEnrollRequest() {
  if (!enrollReq.pending) return;

  enrollReq.pending = false;
  enrollStatus.inProgress = true;
  enrollStatus.lastResultKnown = false;
  enrollStatus.stageText = "Starting";

  bool ok = finger.enroll(enrollReq.fingerprintId, nullptr, onEnrollProgress);

  if (ok) {
    Student s;
    bool exists = studentDB.findByFingerprintId(enrollReq.fingerprintId, s);

    if (enrollReq.studentId.length() > 0 || enrollReq.name.length() > 0) {
      // Student details were sent with the enroll request: save them.
      s.fingerprintId = enrollReq.fingerprintId;
      s.studentId     = enrollReq.studentId;
      s.name          = enrollReq.name;
      s.className     = enrollReq.className;
      s.active        = true;
      bool saved = studentDB.addOrUpdate(s);
      enrollStatus.stageText = saved ? "Done - student saved" : "Enrolled, but SAVE FAILED";
    } else if (exists) {
      // Keep the details that were already saved for this ID (never overwrite with blanks).
      enrollStatus.stageText = "Done - linked to " + s.name;
    } else {
      enrollStatus.stageText = "Enrolled. Now save student details for ID " + String(enrollReq.fingerprintId);
    }
  } else {
    enrollStatus.stageText = String("Failed: ") + finger.errorString();
  }

  enrollReq.studentId = "";
  enrollReq.name = "";
  enrollReq.className = "";
  sensorArmed = false;   // force re-arm after enrollment

  enrollStatus.inProgress = false;
  enrollStatus.lastResultKnown = true;
  enrollStatus.lastResultOk = ok;

  renderMenu();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Attendance System - session-based final build ===");

  sessionMenu.parent = &rootMenu;
  recordsMenu.parent = &rootMenu;
  statusMenu.parent  = &rootMenu;

  buttons.begin();
  lcd.begin();

  lcd.showLine(0, "Starting up...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount FAILED even after format attempt.");
  }
  studentDB.begin();
  attLog.begin();

  fpOk = finger.begin(PIN_S120_RX, PIN_S120_TX);
  Serial.println(fpOk ? "Fingerprint: OK" : "Fingerprint: FAILED");
  if (fpOk) {
    Serial.printf("Capacity: %u\n", finger.capacity());
    finger.sleepAndArm();
  }

  wifiOk = connectWifi(WIFI_CONNECT_TIMEOUT_MS);
  Serial.println(wifiOk ? "WiFi: connected" : "WiFi: FAILED (will keep retrying)");
  if (wifiOk) Serial.println(WiFi.localIP().toString());

  bool timeOk = clock_.begin();
  Serial.println(timeOk ? (clock_.isFallback() ? "Time: fallback estimate" : "Time: NTP synced")
                         : "Time: NOT SET (no NTP and no saved fallback)");

  lcd.showStartup(fpOk, wifiOk, timeOk);
  delay(1500);

  dashboard.begin();

  lastWifiRetry = millis();

  currentMenu = &rootMenu;
  selectedIndex = 0;
  renderMenu();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry > WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetry = millis();
    wifiOk = connectWifi(5000);
  } else {
    wifiOk = (WiFi.status() == WL_CONNECTED);
  }

  clock_.poll();
  dashboard.handleClient();

  handleMenuButtons();

  handleEnrollRequest();

  if (!enrollStatus.inProgress) {
    handleAttendanceFlow();
  }

  delay(5);
}
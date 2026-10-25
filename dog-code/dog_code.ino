#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ---------- PINS ----------
const int EYE_LEFT_PIN  = 3;                    // idle LOW, HIGH on detection
const int EYE_RIGHT_PIN = 4;                    // idle LOW, HIGH on detection
const int LDR_PIN       = A0;

// ---------- SETTINGS ----------
const char* PHONES[] = {
  "+2348037273256",                             // <-- number 1
  "+2347063910623"                              // <-- number 2
};
const int NUM_PHONES = sizeof(PHONES) / sizeof(PHONES[0]);

const bool DARK_ONLY      = true;              // true = only react when it's dark
const int  DARK_THRESHOLD = 300;                // 0-1023, below this = dark

const unsigned long PERSIST_MS     = 240000UL;  // motion must last 4 min before calling
const unsigned long CLEAR_MS       = 15000UL;   // no motion for 15 s = motion ended
const unsigned long BARK_REPEAT    = 8000UL;   // re-bark interval while motion persists
const unsigned long ALERT_COOLDOWN = 300000UL;  // 5 min between alerts
const unsigned long RING_MS        = 20000UL;   // how long each call rings
const unsigned long CALL_GAP_MS    = 3000UL;    // pause between calls

SoftwareSerial dfSerial(10, 11);                // RX, TX
DFRobotDFPlayerMini player;

bool active = false, alertSent = false;
unsigned long motionStart = 0, lastMotion = 0, lastBark = 0, lastAlert = 0;

// ---------- CALL STATE MACHINE ----------
enum CallState { CALL_IDLE, CALL_RINGING, CALL_GAP };
CallState callState = CALL_IDLE;
int callIndex = 0;
unsigned long callTimer = 0;

void dial(int i) {
  Serial.print("ATD");
  Serial.print(PHONES[i]);
  Serial.println(";");
  callTimer = millis();
  callState = CALL_RINGING;
}

void startCalls() {
  callIndex = 0;
  dial(callIndex);
}

void callTask() {                               // runs every loop, never blocks
  unsigned long now = millis();

  if (callState == CALL_RINGING && now - callTimer >= RING_MS) {
    Serial.println("ATH");                      // hang up
    callTimer = now;
    callState = CALL_GAP;
  }
  else if (callState == CALL_GAP && now - callTimer >= CALL_GAP_MS) {
    callIndex++;
    if (callIndex < NUM_PHONES) dial(callIndex);
    else callState = CALL_IDLE;                 // all numbers done
  }
}

// ---------- HELPERS ----------
bool eyesTriggered() {
  return digitalRead(EYE_LEFT_PIN) == HIGH || digitalRead(EYE_RIGHT_PIN) == HIGH;
}

bool lightOk() {
  if (!DARK_ONLY) return true;
  return analogRead(LDR_PIN) < DARK_THRESHOLD;
}

void bark() {
  player.play(1);                               // plays 0001.mp3
  lastBark = millis();
}

// ---------- MAIN ----------
void setup() {
  pinMode(EYE_LEFT_PIN, INPUT);
  pinMode(EYE_RIGHT_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  Serial.begin(9600);                           // SIM800L on hardware serial
  dfSerial.begin(9600);
  delay(1500);
  player.begin(dfSerial, false, false);
  player.volume(30);                            // 0-30
  delay(500);

  Serial.println("AT");                         // wake GSM
  delay(20000);                                 // sensor warm-up + network registration
}

void loop() {
  unsigned long now = millis();

  callTask();                                   // keeps calls going in the background

  bool motion = eyesTriggered() && lightOk();

  if (motion) {
    lastMotion = now;

    if (!active) {                              // new motion event
      active = true;
      alertSent = false;
      motionStart = now;
      bark();
    } else if (now - lastBark >= BARK_REPEAT) { // still there: bark again
      bark();
    }

    if (!alertSent && callState == CALL_IDLE &&
        now - motionStart >= PERSIST_MS &&
        (lastAlert == 0 || now - lastAlert >= ALERT_COOLDOWN)) {
      startCalls();
      alertSent = true;
      lastAlert = now;
    }
  } else if (active && now - lastMotion >= CLEAR_MS) {
    active = false;                             // motion gone, reset
  }
}

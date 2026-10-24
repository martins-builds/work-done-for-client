#include <ESP32Servo.h>

// ===================== PINS =====================
#define TRIG_PERSON   25
#define ECHO_PERSON   34
#define TRIG_FILL     26
#define ECHO_FILL     35
#define SERVO_PIN     27
#define BUZZER_PIN    33
#define LED_GREEN     32
#define LED_YELLOW    4
#define LED_RED       2
#define GSM_RX_PIN    16   // ESP32 RX <- SIM800 TX
#define GSM_TX_PIN    17   // ESP32 TX -> SIM800 RX

// ===================== SETTINGS =====================
// Phone numbers: CHANGE THESE
const char PHONE_NUMBER_1[] = "+2348037273256";
const char PHONE_NUMBER_2[] = "+2347066374076";
const char SMS_TEXT[]       = "Smart Bin is FULL. Please empty it.";
const bool SEND_SMS         = false;           // false = calls only
const uint32_t CALL_TIME_MS = 30000;          // how long each call rings before hang-up

// Bin / detection
const float BIN_HEIGHT_CM       = 24.1;   // sensor to floor, empty bin
const float DETECT_DISTANCE_CM  = 58.0;   // closer than this = person detected
const uint32_t VERIFY_DELAY_MS  = 1500;   // wait before 2nd person measurement
const uint32_t REARM_DELAY_MS   = 2000;   // pause after lid closes

// Fill levels (percent): GREEN 0-50, YELLOW 51-80, RED 80-100
const float GREEN_MAX_PCT       = 50.0;   // above this -> yellow
const float FULL_PCT            = 80.0;   // this and above -> red / full
const float FULL_RESET_PCT      = 70.0;   // must drop below this to count as emptied

// Lid
const uint32_t LID_OPEN_MS      = 10000;
const uint32_t WARNING_MS       = 3000;
const int SERVO_CLOSED_ANGLE    = 60;
const int SERVO_OPEN_ANGLE      = 0;

// Fill sensor timing
const uint32_t FILL_CHECK_MS    = 1000;
const uint32_t FILL_SETTLE_MS   = 1500;
const uint8_t  FULL_CONFIRMS    = 3;

// ===================== STATE =====================
enum LidState { LID_CLOSED, LID_OPEN, LID_WARNING };
LidState lidState = LID_CLOSED;
uint32_t lidTimer = 0, lastCloseTime = 0, buzzerToggleTime = 0, lastFillCheck = 0;
bool buzzerOn = false;

Servo lidServo;
HardwareSerial sim800(2);

float fillPercent = 0;
bool binFull = false;
bool fullNotified = false;
uint8_t fullCount = 0;

// ===================== GSM =====================
void waitForResponse(unsigned long timeout = 3000) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (sim800.available()) Serial.write(sim800.read());
  }
}

void sendAT(String cmd) {
  Serial.print(">> ");
  Serial.println(cmd);
  sim800.println(cmd);
  waitForResponse();
}

void send_AT(String command, int waitTime) {
  sim800.println(command);
  delay(waitTime);
  while (sim800.available()) Serial.write(sim800.read());
}

void callNumber(const char* number) {
  sendAT("AT");
  sendAT("ATE0");
  sendAT("AT+CLIP=1");

  Serial.print("Calling ");
  Serial.println(number);
  sim800.print("ATD");
  sim800.print(number);
  sim800.println(";");
  waitForResponse();

  Serial.println("Phone should now be ringing...");
  delay(CALL_TIME_MS);

  Serial.println("Ending call...");
  sendAT("ATH");
  Serial.println("Call done.");
}

void sendSMS(const char* number, const char* text) {
  send_AT("AT", 1000);
  send_AT("ATE0", 1000);
  send_AT("AT+CMGF=1", 1000);
  send_AT("AT+CSCS=\"GSM\"", 1000);

  Serial.println("Sending SMS...");
  sim800.print("AT+CMGS=\"");
  sim800.print(number);
  sim800.println("\"");
  delay(2000);
  sim800.print(text);
  delay(500);
  sim800.write(26);   // CTRL+Z ends the SMS
  delay(8000);
  Serial.println("SMS done.");
}

// Called once per FULL event
void notifyFull() {
  Serial.println("Bin FULL: calling responsible persons...");
  callNumber(PHONE_NUMBER_1);
  delay(2000);
  callNumber(PHONE_NUMBER_2);
  if (SEND_SMS) {
    sendSMS(PHONE_NUMBER_1, SMS_TEXT);
  }
  Serial.println("Notification finished.");
}

// ===================== ULTRASONIC =====================
float readDistanceOnce(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long d = pulseIn(echo, HIGH, 30000UL);
  if (d == 0) return -1;
  return d * 0.0343f / 2.0f;
}

float readDistanceMedian(uint8_t trig, uint8_t echo, uint8_t n = 5) {
  float v[9];
  uint8_t count = 0;
  if (n > 9) n = 9;
  for (uint8_t i = 0; i < n; i++) {
    float d = readDistanceOnce(trig, echo);
    if (d > 0) v[count++] = d;
    delay(40);
  }
  if (count < 3) return -1;
  for (uint8_t i = 0; i < count - 1; i++)
    for (uint8_t j = i + 1; j < count; j++)
      if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[count / 2];
}

bool personPresent(float &dist) {
  dist = readDistanceMedian(TRIG_PERSON, ECHO_PERSON, 5);
  return (dist > 0 && dist < DETECT_DISTANCE_CM);
}

// ===================== LEDs =====================
void updateLeds() {
  bool g = false, y = false, r = false;
  if (binFull)                          r = true;
  else if (fillPercent > GREEN_MAX_PCT) y = true;
  else                                  g = true;
  digitalWrite(LED_GREEN, g);
  digitalWrite(LED_YELLOW, y);
  digitalWrite(LED_RED, r);
}

// ===================== FILL LEVEL =====================
void updateFillLevel() {
  float dist = readDistanceMedian(TRIG_FILL, ECHO_FILL, 5);
  if (dist < 0) { Serial.println("Fill sensor: no echo"); return; }

  float fillHeight = constrain(BIN_HEIGHT_CM - dist, 0.0f, BIN_HEIGHT_CM);
  fillPercent = fillHeight / BIN_HEIGHT_CM * 100.0f;

  if (!binFull) {
    if (fillPercent >= FULL_PCT) {
      if (++fullCount >= FULL_CONFIRMS) {
        binFull = true;
        Serial.println("Bin is FULL");
      }
    } else {
      fullCount = 0;
    }
  } else if (fillPercent < FULL_RESET_PCT) {
    binFull = false;
    fullNotified = false;
    fullCount = 0;
    Serial.println("Bin emptied - notification reset");
  }

  updateLeds();   // show red before the (slow) calls start
  Serial.printf("Fill sensor: %.1f cm | trash height: %.1f cm | %.1f%% | %s\n",
                dist, fillHeight, fillPercent,
                binFull ? "FULL (red)" : (fillPercent > GREEN_MAX_PCT ? "yellow" : "green"));

  // One notification per FULL event
  if (binFull && !fullNotified) {
    fullNotified = true;
    notifyFull();
  }
}

// ===================== LID =====================
void handleLid() {
  uint32_t now = millis();
  switch (lidState) {
    case LID_OPEN:
      if (now - lidTimer >= LID_OPEN_MS) {
        lidState = LID_WARNING;
        lidTimer = now;
        buzzerOn = true;
        buzzerToggleTime = now;
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("Warning: lid closing soon");
      }
      break;

    case LID_WARNING:
      if (now - buzzerToggleTime >= 300) {
        buzzerOn = !buzzerOn;
        digitalWrite(BUZZER_PIN, buzzerOn);
        buzzerToggleTime = now;
      }
      if (now - lidTimer >= WARNING_MS) {
        digitalWrite(BUZZER_PIN, LOW);
        lidServo.write(SERVO_CLOSED_ANGLE);
        lidState = LID_CLOSED;
        lastCloseTime = now;
        lastFillCheck = now + FILL_SETTLE_MS - FILL_CHECK_MS;
        Serial.println("Lid closed");
      }
      break;

    default:
      break;
  }
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PERSON, OUTPUT);
  pinMode(ECHO_PERSON, INPUT);
  pinMode(TRIG_FILL, OUTPUT);
  pinMode(ECHO_FILL, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lidServo.setPeriodHertz(50);
  lidServo.attach(SERVO_PIN, 500, 2400);
  lidServo.write(SERVO_CLOSED_ANGLE);

  sim800.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  Serial.println("Initializing SIM800L...");
  delay(3000);

  for (uint8_t i = 0; i < FULL_CONFIRMS; i++) updateFillLevel();
  lastFillCheck = millis();
  Serial.println("Smart Waste Bin ready");
}

void loop() {
  handleLid();

  if (lidState != LID_CLOSED) return;

  if (millis() - lastFillCheck >= FILL_CHECK_MS) {
    lastFillCheck = millis();
    updateFillLevel();
  }

  if (millis() - lastCloseTime < REARM_DELAY_MS) return;

  // Step 1: first detection
  float d1;
  if (!personPresent(d1)) return;
  Serial.printf("Person detected at %.1f cm - verifying...\n", d1);

  // Step 2: verify still there
  delay(VERIFY_DELAY_MS);
  float d2;
  if (!personPresent(d2)) {
    Serial.println("Person walked past - lid stays closed");
    return;
  }
  Serial.printf("Person standing at %.1f cm\n", d2);

  // Step 3: fill check has priority
  updateFillLevel();
  if (binFull) {
    Serial.println("Bin FULL - lid stays closed");
    return;
  }

  // Step 4: open lid
  Serial.println("Opening lid");
  lidServo.write(SERVO_OPEN_ANGLE);
  lidState = LID_OPEN;
  lidTimer = millis();
}
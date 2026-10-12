/*
  S120Fingerprint - driver for the S120R / S120S capacitive fingerprint module
  (Guangzhou Gouku / "SFPM" protocol) on ESP32 + Arduino.

  Identification uses the manufacturer-style workflow that was verified on
  real hardware:

      Sleep(0x33) -> wait INT rising -> Detect(0x01) -> Extract(0x02, buf 1)
      -> Search(0x04, buf 1, 0..capacity) -> wait finger removed -> Sleep

  UART: 57600 baud, 8 data bits, no parity, 2 stop bits (SERIAL_8N2).
  All checksums are calculated dynamically, never hard-coded.

  The module drives its own LED (green on match, red on failure), so this
  library does not attempt to synchronize an LED with the result - it just
  exposes controlLight() if you want to override that behavior yourself.
*/
#ifndef S120FINGERPRINT_H
#define S120FINGERPRINT_H

#include <Arduino.h>
#include <HardwareSerial.h>

#define S120_LIB_VERSION          "1.0.0"
#define S120_DEFAULT_CAPACITY     100
#define S120_DEFAULT_BAUD         57600

// ---------------------------------------------------------------------
// Error / status codes.
// 0x00..0x35 are the module's own response codes (RCODE), unchanged, so
// they can be cross-referenced against the S120 developer guide.
// 0xF0..0xFE are generated locally by this library (UART/timeout/etc).
// ---------------------------------------------------------------------
enum S120Error : uint8_t {
  S120_OK                     = 0x00,
  S120_ERR_PACKET             = 0x01,  // module received a malformed packet
  S120_ERR_NO_FINGER          = 0x02,
  S120_ERR_IMAGE_CAPTURE      = 0x03,
  S120_ERR_IMAGE_TOO_LIGHT    = 0x04,
  S120_ERR_IMAGE_TOO_MUSHY    = 0x05,
  S120_ERR_IMAGE_TOO_MESSY    = 0x06,
  S120_ERR_FEW_FEATURES       = 0x07,  // feature generation failed
  S120_ERR_MISMATCH           = 0x08,  // two prints do not match
  S120_ERR_NOT_FOUND          = 0x09,  // search: not in database
  S120_ERR_MERGE_FAILED       = 0x0A,
  S120_ERR_ID_OUT_OF_RANGE    = 0x0B,
  S120_ERR_TEMPLATE_INVALID   = 0x0C,
  S120_ERR_UPLOAD_FEATURES    = 0x0D,
  S120_ERR_RECEIVE_DATA       = 0x0E,
  S120_ERR_UPLOAD_IMAGE       = 0x0F,
  S120_ERR_DELETE_FAILED      = 0x10,
  S120_ERR_CLEAR_FAILED       = 0x11,
  S120_ERR_LOW_POWER_FAILED   = 0x12,
  S120_ERR_NO_VALID_IMAGE     = 0x15,
  S120_ERR_RESIDUAL_FINGER    = 0x17,
  S120_ERR_FLASH               = 0x18,
  S120_ERR_DB_FULL             = 0x1F,
  S120_ERR_TEMPLATE_NOT_EMPTY  = 0x22,  // ID already occupied
  S120_ERR_TEMPLATE_EMPTY      = 0x23,
  S120_ERR_DB_EMPTY            = 0x24,
  S120_ERR_BAD_ENROLL_TIMES    = 0x25,
  S120_ERR_MODULE_TIMEOUT      = 0x26,
  S120_ERR_DUPLICATE           = 0x27,  // fingerprint already enrolled elsewhere
  S120_ERR_SENSOR               = 0x29,  // sensor operation failed
  S120_ERR_INVALID_DATA         = 0x35,
  // ---- generated locally by this library ----
  S120_ERR_NO_RESPONSE          = 0xF0,  // UART timeout / nothing received
  S120_ERR_CHECKSUM             = 0xF1,  // reply checksum did not match
  S120_ERR_BAD_ARGUMENT         = 0xF2,
  S120_ERR_ABORTED              = 0xF3,  // idle/progress callback asked to stop
  S120_ERR_TOUCH_TIMEOUT        = 0xF4,  // no touch (INT) within timeout
  S120_ERR_REMOVAL_TIMEOUT      = 0xF5,  // finger still on sensor
  S120_ERR_NOT_BEGUN            = 0xF6,  // begin() was never called / failed
  S120_ERR_OTHER                = 0xFE   // module code not in this table; see lastRCode()
};

// ---------------------------------------------------------------------
// Result structures
// ---------------------------------------------------------------------
struct S120Match {
  bool      matched = false;
  uint16_t  id       = 0;
  uint16_t  score    = 0;
  S120Error error     = S120_OK;
  uint8_t   rcode     = 0;
};

struct S120Params {
  uint16_t enrollTimes     = 0;
  uint16_t templateSize    = 0;
  uint16_t capacity        = 0;
  uint16_t scoreLevel      = 0;
  uint32_t deviceAddress   = 0;
  uint16_t packetSizeCode  = 0;   // 0:32  1:64  2:128  3:256 bytes
  uint16_t packetSizeBytes = 0;
  uint16_t baudCode        = 0;   // baud = baudCode * 9600
  uint32_t baudRate        = 0;
};

// ---- Enrollment --------------------------------------------------------
enum S120EnrollStage : uint8_t {
  S120_STAGE_VALIDITY    = 0,   // ID / options accepted
  S120_STAGE_IMAGE       = 1,   // image acquired for current sample
  S120_STAGE_FEATURES    = 2,   // features generated for current sample
  S120_STAGE_FINGER_OFF  = 3,   // finger removed, sample accepted
  S120_STAGE_MERGE       = 4,   // samples merged into one template
  S120_STAGE_DUP_CHECK   = 5,   // duplicate-fingerprint inspection
  S120_STAGE_STORE       = 6    // template written to flash
};

struct S120EnrollProgress {
  S120EnrollStage stage;
  uint8_t  sample;         // current sample index (1-based) during stages 1-3, else 0
  uint8_t  totalSamples;
  uint8_t  rcode;          // module response code reported for this step
  bool     retry;          // true: this sample failed and must be retaken
};

// Return false to cancel the enrollment (library sends CMD_Cancel).
typedef bool (*S120EnrollCallback)(const S120EnrollProgress& progress, void* user);
// Called periodically while the library blocks waiting for touch/replies.
// Return false to abort the current operation.
typedef bool (*S120IdleCallback)(void* user);

struct S120EnrollOptions {
  uint8_t  samples            = 0;       // 0 = use the module's own EnrollTimes parameter
  bool     checkDuplicates    = true;    // reject a finger already enrolled under another ID
  bool     allowOverwrite     = false;   // allow re-using an ID that already holds a template
  bool     requireFingerLeave = true;    // finger must lift between samples
  bool     ledOffAfterCapture = false;
  uint32_t stepTimeoutMs      = 20000;   // max wait for one progress packet
  uint32_t totalTimeoutMs     = 120000;  // max wall-clock time for the whole enrollment
};

// ---- Optional LED control (command 0x3C) --------------------------------
enum S120LightMode  : uint8_t {
  S120_LIGHT_BREATHING = 1, S120_LIGHT_FLASHING = 2, S120_LIGHT_ON = 3,
  S120_LIGHT_OFF       = 4, S120_LIGHT_FADE_IN  = 5, S120_LIGHT_FADE_OUT = 6
};
enum S120LightColor : uint8_t {
  S120_DARK = 0, S120_BLUE = 1, S120_GREEN = 2, S120_CYAN = 3,
  S120_RED  = 4, S120_MAGENTA = 5, S120_YELLOW = 6, S120_WHITE = 7
};

// =========================================================================
class S120Fingerprint {
public:
  // serial : any HardwareSerial instance the caller owns, e.g. HardwareSerial S120Serial(2)
  // intPin : touch-interrupt GPIO (S120 INT, active HIGH); pass -1 if not wired
  S120Fingerprint(HardwareSerial& serial, int8_t intPin = -1);

  // Configures the UART as SERIAL_8N2, waits briefly for the module boot
  // byte (0x55), performs a handshake and (optionally) reads the module's
  // own parameters so capacity() reflects the real hardware.
  // Never touches the fingerprint database.
  bool begin(int8_t rxPin = 16, int8_t txPin = 17, uint32_t baud = S120_DEFAULT_BAUD,
             bool readParams = true);
  // Use this overload if the caller already called serial.begin(...) itself.
  bool beginPrepared(bool readParams = true);

  // ---- basic commands ----------------------------------------------
  bool handshake();                                            // 0x35
  bool sleep();                                                // 0x33
  bool cancel();                                                // 0x30
  bool controlLight(S120LightMode mode, S120LightColor startColor,
                    S120LightColor endColor, uint8_t cycles = 0);  // 0x3C

  // ---- touch / interrupt handling -----------------------------------
  // Non-blocking pattern:
  //   finger.sleepAndArm();
  //   ...
  //   if (finger.touchDetected()) { finger.disarmTouch(); ... identify ... }
  bool sleepAndArm();          // sleep() then armTouch()
  void armTouch();             // clear flag, attach INT rising-edge handler
  void disarmTouch();          // detach INT handler
  bool touchDetected();        // true once per touch; consumes the flag

  // Blocking helper equivalent to the tested example sketch.
  // sleepFirst=true: puts the module to sleep before arming INT (required,
  // the S120 INT only fires while the sensor is dormant).
  // timeoutMs = 0 waits indefinitely (an idle callback can still abort it).
  bool waitForTouch(uint32_t timeoutMs = 30000, bool sleepFirst = true);

  // ---- identification steps (manufacturer workflow, unchanged) --------
  bool detect(uint32_t timeoutMs = 2000);                       // 0x01, retries internally
  bool extract(uint8_t buffer = 1);                             // 0x02
  bool search(uint16_t& id, uint16_t& score, uint8_t buffer = 1,
              uint16_t startId = 0, uint16_t count = 0);        // 0x04 (count 0 = capacity())
  bool waitForRemoval(uint32_t timeoutMs = 8000);

  // detect() + extract() + search() combined.
  bool      identify(uint16_t& id, uint16_t& score, uint32_t detectTimeoutMs = 2000);
  S120Match identifyResult(uint32_t detectTimeoutMs = 2000);

  // ---- enrollment -----------------------------------------------------
  bool enroll(uint16_t id, const S120EnrollOptions* options = nullptr,
              S120EnrollCallback callback = nullptr, void* user = nullptr);  // 0x31

  // ---- database ---------------------------------------------------------
  bool deleteTemplate(uint16_t id, uint16_t count = 1);          // 0x0C
  bool clearDatabase();                                          // 0x0D (only when you call it)
  bool isEnrolled(uint16_t id, bool& enrolled);                  // via read index table (0x1F)
  bool readIndexTable(uint8_t page, uint8_t bitmap[32]);         // 0x1F, 256 IDs per page

  // ---- parameters ---------------------------------------------------------
  bool getParameters(S120Params& params);                        // 0x0F
  uint16_t capacity() const { return capacity_; }

  // ---- status / errors ----------------------------------------------------
  S120Error   lastError()  const { return lastError_; }
  uint8_t     lastRCode()  const { return lastRCode_; }   // raw module code of the last reply
  const char* errorString() const { return errorName(lastError_); }
  static const char* errorName(S120Error e);
  static const char* enrollStageName(S120EnrollStage s);

  // ---- configuration --------------------------------------------------------
  void setIdleCallback(S120IdleCallback cb, void* user = nullptr) { idleCb_ = cb; idleUser_ = user; }
  void setDebug(Print* p)             { debug_ = p; }            // prints TX/RX hex dumps
  void setDeviceAddress(uint32_t a);                              // default 0xFFFFFFFF
  void setCapacity(uint16_t c)        { if (c) capacity_ = c; }
  void setReplyTimeout(uint32_t ms)   { replyTimeoutMs_ = ms; }   // per-packet UART timeout
  void setSleepSettleMs(uint16_t ms)  { sleepSettleMs_ = ms; }    // delay after sleep() before arming INT
  void setWakeDelayMs(uint16_t ms)    { wakeDelayMs_ = ms; }      // delay after INT before talking again

private:
  struct Ack { uint8_t rcode; uint8_t data[40]; uint8_t dataLen; };

  HardwareSerial& serial_;
  int8_t   intPin_;
  uint8_t  addr_[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint16_t capacity_ = S120_DEFAULT_CAPACITY;
  volatile bool touchFlag_ = false;
  bool     armed_ = false;
  bool     begun_ = false;
  S120Error lastError_ = S120_ERR_NOT_BEGUN;
  uint8_t  lastRCode_ = 0;
  Print*   debug_ = nullptr;
  S120IdleCallback idleCb_ = nullptr;
  void*    idleUser_ = nullptr;
  uint32_t replyTimeoutMs_ = 1000;
  uint16_t sleepSettleMs_ = 50;
  uint16_t wakeDelayMs_ = 30;

  bool init(bool readParams);
  void clearRx();
  void sendCommand(const uint8_t* params, uint8_t n);
  S120Error receiveAck(Ack& ack, uint32_t timeoutMs);
  bool command(const uint8_t* params, uint8_t n, Ack& ack, uint32_t timeoutMs);
  bool commandSimple(const uint8_t* params, uint8_t n, uint32_t timeoutMs);
  bool fail(S120Error e) { lastError_ = e; return false; }
  bool idle();
  bool pause(uint32_t ms);                // delay that keeps polling idle(); false if aborted
  void hexDump(const char* tag, const uint8_t* p, size_t n);
  static S120Error toError(uint8_t rcode);
  static void IRAM_ATTR isrTrampoline(void* arg);
};

#endif

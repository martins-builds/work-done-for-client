#include "S120Fingerprint.h"

// ---------------------------------------------------------------------
// Command codes (module side, PID = 0x01 in the packet header)
// ---------------------------------------------------------------------
namespace {
  enum : uint8_t {
    CMD_DETECT      = 0x01,
    CMD_EXTRACT     = 0x02,
    CMD_SEARCH      = 0x04,
    CMD_DELETE      = 0x0C,
    CMD_CLEAR       = 0x0D,
    CMD_GET_PARAMS  = 0x0F,
    CMD_READ_INDEX  = 0x1F,
    CMD_ENROLL      = 0x31,
    CMD_CANCEL      = 0x30,
    CMD_SLEEP       = 0x33,
    CMD_HANDSHAKE   = 0x35,
    CMD_LIGHT       = 0x3C
  };
  const uint8_t RESP_PID = 0x07;
}

// ---------------------------------------------------------------------
S120Fingerprint::S120Fingerprint(HardwareSerial& serial, int8_t intPin)
  : serial_(serial), intPin_(intPin) {}

void S120Fingerprint::setDeviceAddress(uint32_t a) {
  addr_[0] = (a >> 24) & 0xFF;
  addr_[1] = (a >> 16) & 0xFF;
  addr_[2] = (a >> 8)  & 0xFF;
  addr_[3] =  a         & 0xFF;
}

// ---------------------------------------------------------------------
bool S120Fingerprint::begin(int8_t rxPin, int8_t txPin, uint32_t baud, bool readParams) {
  serial_.begin(baud, SERIAL_8N2, rxPin, txPin);
  return init(readParams);
}

bool S120Fingerprint::beginPrepared(bool readParams) {
  return init(readParams);
}

bool S120Fingerprint::init(bool readParams) {
  if (intPin_ >= 0) pinMode(intPin_, INPUT);

  // The module boots in ~130 ms and emits 0x55 once ready. This byte may
  // already have been missed if begin() was called late; that is fine,
  // the handshake retry below covers it either way.
  uint32_t t0 = millis();
  while (millis() - t0 < 300) {
    if (serial_.available() && serial_.read() == 0x55) break;
  }

  bool ok = false;
  for (uint8_t i = 0; i < 5 && !ok; i++) {
    ok = handshake();
    if (!ok) delay(150);
  }
  if (!ok) {
    lastError_ = S120_ERR_NO_RESPONSE;
    begun_ = false;
    return false;
  }

  begun_ = true;
  lastError_ = S120_OK;

  if (readParams) {
    S120Params p;
    if (getParameters(p) && p.capacity > 0) {
      capacity_ = p.capacity;
    }
    // If reading parameters failed, capacity_ keeps its previous/default
    // value (see setCapacity()); this is not treated as a begin() failure.
  }
  return true;
}

// ---------------------------------------------------------------------
// Low level: packet build / send / receive
// ---------------------------------------------------------------------
void S120Fingerprint::hexDump(const char* tag, const uint8_t* p, size_t n) {
  if (!debug_) return;
  debug_->print(tag);
  for (size_t i = 0; i < n; i++) {
    if (p[i] < 0x10) debug_->print('0');
    debug_->print(p[i], HEX);
    debug_->print(' ');
  }
  debug_->println();
}

void S120Fingerprint::clearRx() {
  while (serial_.available()) serial_.read();
}

void S120Fingerprint::sendCommand(const uint8_t* params, uint8_t n) {
  uint8_t pkt[48];
  uint16_t plen = (uint16_t)n + 2;              // params + 2 checksum bytes
  pkt[0] = 0xEF; pkt[1] = 0x01;
  pkt[2] = addr_[0]; pkt[3] = addr_[1]; pkt[4] = addr_[2]; pkt[5] = addr_[3];
  pkt[6] = 0x01;                                 // PID = command
  pkt[7] = (uint8_t)(plen >> 8);
  pkt[8] = (uint8_t)(plen & 0xFF);

  uint16_t sum = pkt[6] + pkt[7] + pkt[8];
  for (uint8_t i = 0; i < n; i++) {
    pkt[9 + i] = params[i];
    sum += params[i];
  }
  pkt[9 + n]     = (uint8_t)(sum >> 8);
  pkt[10 + n]    = (uint8_t)(sum & 0xFF);
  uint16_t total = 11 + n;

  clearRx();                                     // drop any stale bytes first
  serial_.write(pkt, total);
  serial_.flush();
  hexDump("S120 TX: ", pkt, total);
}

S120Error S120Fingerprint::receiveAck(Ack& ack, uint32_t timeoutMs) {
  uint8_t rx[64];
  size_t idx = 0;
  size_t total = 0;
  ack.dataLen = 0;
  uint32_t t0 = millis();

  const uint8_t hdr[7] = {0xEF, 0x01, addr_[0], addr_[1], addr_[2], addr_[3], RESP_PID};

  while (millis() - t0 < timeoutMs) {
    while (serial_.available()) {
      uint8_t b = serial_.read();

      if (idx < 7) {
        if (b == hdr[idx]) {
          rx[idx++] = b;
        } else {
          idx = (b == 0xEF) ? 1 : 0;
          if (idx) rx[0] = b;
        }
        continue;
      }

      if (idx >= sizeof(rx)) { idx = 0; continue; }   // overflow guard
      rx[idx++] = b;

      if (idx == 9) {
        uint16_t plen = ((uint16_t)rx[7] << 8) | rx[8];
        if (plen < 3 || 9 + plen > sizeof(rx)) { idx = 0; continue; }
        total = 9 + plen;
      }

      if (total && idx >= total) {
        hexDump("S120 RX: ", rx, total);

        uint16_t sum = 0;
        for (size_t i = 6; i < total - 2; i++) sum += rx[i];
        uint16_t got = ((uint16_t)rx[total - 2] << 8) | rx[total - 1];
        if (sum != got) return S120_ERR_CHECKSUM;

        ack.rcode = rx[9];
        size_t dataLen = total - 12;              // bytes between RCODE and SUM
        if (dataLen > sizeof(ack.data)) dataLen = sizeof(ack.data);
        ack.dataLen = (uint8_t)dataLen;
        memcpy(ack.data, &rx[10], dataLen);
        return S120_OK;   // packet itself is valid; ack.rcode carries the module result
      }
    }
    if (!idle()) return S120_ERR_ABORTED;
    delay(1);
  }
  return S120_ERR_NO_RESPONSE;
}

bool S120Fingerprint::command(const uint8_t* params, uint8_t n, Ack& ack, uint32_t timeoutMs) {
  sendCommand(params, n);
  S120Error transportErr = receiveAck(ack, timeoutMs);
  if (transportErr != S120_OK) {
    lastRCode_ = 0;
    return fail(transportErr);
  }
  lastRCode_ = ack.rcode;
  lastError_ = toError(ack.rcode);
  return ack.rcode == 0x00;
}

bool S120Fingerprint::commandSimple(const uint8_t* params, uint8_t n, uint32_t timeoutMs) {
  Ack a;
  return command(params, n, a, timeoutMs);
}

bool S120Fingerprint::idle() {
  if (idleCb_) return idleCb_(idleUser_);
  return true;
}

bool S120Fingerprint::pause(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (!idle()) return false;
    delay(2);
  }
  return true;
}

S120Error S120Fingerprint::toError(uint8_t rcode) {
  switch (rcode) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x12: case 0x15: case 0x17: case 0x18: case 0x1F: case 0x22:
    case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x29:
    case 0x35:
      return (S120Error)rcode;
    default:
      return S120_ERR_OTHER;
  }
}

// ---------------------------------------------------------------------
// Basic commands
// ---------------------------------------------------------------------
bool S120Fingerprint::handshake() {
  const uint8_t p[] = { CMD_HANDSHAKE };
  return commandSimple(p, sizeof(p), 400);
}

bool S120Fingerprint::sleep() {
  const uint8_t p[] = { CMD_SLEEP };
  return commandSimple(p, sizeof(p), replyTimeoutMs_);
}

bool S120Fingerprint::cancel() {
  const uint8_t p[] = { CMD_CANCEL };
  return commandSimple(p, sizeof(p), replyTimeoutMs_);
}

bool S120Fingerprint::controlLight(S120LightMode mode, S120LightColor startColor,
                                   S120LightColor endColor, uint8_t cycles) {
  const uint8_t p[] = { CMD_LIGHT, (uint8_t)mode, (uint8_t)startColor,
                        (uint8_t)endColor, cycles };
  return commandSimple(p, sizeof(p), replyTimeoutMs_);
}

// ---------------------------------------------------------------------
// Touch / interrupt handling
// ---------------------------------------------------------------------
// One ISR trampoline shared through a static pointer: HardwareSerial-based
// designs normally use a single sensor instance, and IRAM ISRs cannot be
// non-static member functions.
static S120Fingerprint* g_touchInstance = nullptr;

void IRAM_ATTR S120Fingerprint::isrTrampoline(void* /*arg*/) {
  if (g_touchInstance) g_touchInstance->touchFlag_ = true;
}

void S120Fingerprint::armTouch() {
  if (intPin_ < 0) return;
  touchFlag_ = false;
  g_touchInstance = this;
  attachInterruptArg(digitalPinToInterrupt(intPin_),
                     (void (*)(void*))isrTrampoline, this, RISING);
  armed_ = true;
}

void S120Fingerprint::disarmTouch() {
  if (intPin_ < 0) return;
  if (armed_) detachInterrupt(digitalPinToInterrupt(intPin_));
  armed_ = false;
}

bool S120Fingerprint::touchDetected() {
  if (!touchFlag_) return false;
  touchFlag_ = false;
  return true;
}

bool S120Fingerprint::sleepAndArm() {
  bool ok = sleep();
  delay(sleepSettleMs_);
  armTouch();
  return ok;
}

bool S120Fingerprint::waitForTouch(uint32_t timeoutMs, bool sleepFirst) {
  if (intPin_ < 0) return fail(S120_ERR_BAD_ARGUMENT);

  if (sleepFirst) sleepAndArm();
  else armTouch();

  uint32_t t0 = millis();
  while (!touchFlag_) {
    if (timeoutMs != 0 && millis() - t0 > timeoutMs) {
      disarmTouch();
      return fail(S120_ERR_TOUCH_TIMEOUT);
    }
    if (!idle()) { disarmTouch(); return fail(S120_ERR_ABORTED); }
    delay(5);
  }
  touchFlag_ = false;
  disarmTouch();
  delay(wakeDelayMs_);
  lastError_ = S120_OK;
  return true;
}

// ---------------------------------------------------------------------
// Identification workflow (unchanged from the tested implementation)
// ---------------------------------------------------------------------
bool S120Fingerprint::detect(uint32_t timeoutMs) {
  const uint8_t p[] = { CMD_DETECT };
  Ack a;
  uint32_t t0 = millis();
  uint32_t attempt = 0;

  // INT fires the instant of touch, before the image is stable, so the
  // first reply is often "no finger" (0x02) or nothing at all. Retry
  // for up to timeoutMs, matching the tested behavior.
  while (millis() - t0 < timeoutMs) {
    bool ok = command(p, sizeof(p), a, 600);
    attempt++;
    if (ok) return true;
    if (lastError_ != S120_ERR_NO_FINGER &&
        lastError_ != S120_ERR_NO_RESPONSE &&
        lastError_ != S120_ERR_IMAGE_CAPTURE) {
      return false;   // a real error, not just "not ready yet"
    }
    if (!pause(100)) return fail(S120_ERR_ABORTED);
  }
  if (attempt == 0) return fail(S120_ERR_NO_RESPONSE);
  return false;   // lastError_ already set by the final attempt
}

bool S120Fingerprint::extract(uint8_t buffer) {
  const uint8_t p[] = { CMD_EXTRACT, buffer };
  return commandSimple(p, sizeof(p), 2500);
}

bool S120Fingerprint::search(uint16_t& id, uint16_t& score, uint8_t buffer,
                             uint16_t startId, uint16_t count) {
  if (count == 0) count = capacity_;
  uint16_t endId = startId + count;
  const uint8_t p[] = {
    CMD_SEARCH, buffer,
    (uint8_t)(startId >> 8), (uint8_t)(startId & 0xFF),
    (uint8_t)(endId   >> 8), (uint8_t)(endId   & 0xFF)
  };
  Ack a;
  bool ok = command(p, sizeof(p), a, 2500);
  if (ok && a.dataLen >= 4) {
    id    = ((uint16_t)a.data[0] << 8) | a.data[1];
    score = ((uint16_t)a.data[2] << 8) | a.data[3];
  }
  return ok;
}

bool S120Fingerprint::waitForRemoval(uint32_t timeoutMs) {
  const uint8_t p[] = { CMD_DETECT };
  Ack a;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    bool ok = command(p, sizeof(p), a, 300);
    if (!ok && lastError_ == S120_ERR_NO_FINGER) {
      lastError_ = S120_OK;
      return true;
    }
    if (!pause(150)) return fail(S120_ERR_ABORTED);
  }
  return fail(S120_ERR_REMOVAL_TIMEOUT);
}

bool S120Fingerprint::identify(uint16_t& id, uint16_t& score, uint32_t detectTimeoutMs) {
  if (!detect(detectTimeoutMs)) return false;
  if (!extract(1)) return false;
  return search(id, score, 1);
}

S120Match S120Fingerprint::identifyResult(uint32_t detectTimeoutMs) {
  S120Match m;
  bool ok = identify(m.id, m.score, detectTimeoutMs);
  m.matched = ok;
  m.error   = lastError_;
  m.rcode   = lastRCode_;
  return m;
}

// ---------------------------------------------------------------------
// Enrollment
// ---------------------------------------------------------------------
bool S120Fingerprint::enroll(uint16_t id, const S120EnrollOptions* optionsIn,
                             S120EnrollCallback cb, void* user) {
  S120EnrollOptions opt;
  if (optionsIn) opt = *optionsIn;

  uint8_t samples = opt.samples;
  if (samples == 0) {
    S120Params p;
    if (getParameters(p) && p.enrollTimes > 0 && p.enrollTimes <= 255) {
      samples = (uint8_t)p.enrollTimes;
    } else {
      samples = 4;   // sane fallback if parameters cannot be read
    }
  }

  uint16_t misc = 0;
  if (opt.ledOffAfterCapture)                 misc |= (1 << 0);
  // bit1 (image preprocessing) left off / module default
  // bit2 = 0 -> module DOES report progress at each step (what we need)
  if (opt.allowOverwrite)                     misc |= (1 << 3);
  if (!opt.checkDuplicates)                   misc |= (1 << 4);
  if (!opt.requireFingerLeave)                misc |= (1 << 5);

  const uint8_t p[] = {
    CMD_ENROLL,
    (uint8_t)(id >> 8), (uint8_t)(id & 0xFF),
    samples,
    (uint8_t)(misc >> 8), (uint8_t)(misc & 0xFF)
  };

  sendCommand(p, sizeof(p));

  uint32_t start = millis();
  bool finished = false;
  bool success = false;

  while (!finished) {
    if (millis() - start > opt.totalTimeoutMs) {
      cancel();
      return fail(S120_ERR_MODULE_TIMEOUT);
    }

    Ack a;
    S120Error transportErr = receiveAck(a, opt.stepTimeoutMs);
    if (transportErr != S120_OK) {
      lastRCode_ = 0;
      return fail(transportErr);
    }
    lastRCode_ = a.rcode;
    lastError_ = toError(a.rcode);

    uint8_t param1 = a.dataLen >= 1 ? a.data[0] : 0;
    uint8_t param2 = a.dataLen >= 2 ? a.data[1] : 0;

    S120EnrollProgress prog{};
    prog.rcode = a.rcode;
    prog.retry = false;

    if (param2 == 0x00) {
      prog.stage = S120_STAGE_VALIDITY;
    } else if (param2 == 0xF0) {
      prog.stage = S120_STAGE_MERGE;
    } else if (param2 == 0xF1) {
      prog.stage = S120_STAGE_DUP_CHECK;
    } else if (param2 == 0xF2) {
      prog.stage = S120_STAGE_STORE;
    } else if (param2 >= 0x01 && param2 <= 0xEE) {
      // "number of current entries" -> which sample this step refers to
      prog.sample = param2;
      prog.totalSamples = samples;
      prog.stage = (param1 == 0x00) ? S120_STAGE_IMAGE :
                  (param1 == 0x02) ? S120_STAGE_FEATURES :
                  (param1 == 0x03) ? S120_STAGE_FINGER_OFF : S120_STAGE_IMAGE;
      if (param1 == 0x02 && a.rcode != 0x00) prog.retry = true;   // feature gen failed, retake
    } else {
      prog.stage = S120_STAGE_VALIDITY;
    }

    if (cb && !cb(prog, user)) {
      cancel();
      return fail(S120_ERR_ABORTED);
    }

    // Validity check failed outright (bad ID / times / db full / dup ID) -
    // module returns immediately with param1=param2=0x00 and rcode != 0x00.
    if (prog.stage == S120_STAGE_VALIDITY && a.rcode != 0x00) {
      return false;
    }

    if (prog.stage == S120_STAGE_STORE) {
      finished = true;
      success  = (a.rcode == 0x00);
    } else if (prog.stage == S120_STAGE_DUP_CHECK && a.rcode == 0x27) {
      finished = true;
      success  = false;   // duplicate fingerprint, enrollment aborted by module
    } else if (prog.stage == S120_STAGE_MERGE && a.rcode != 0x00) {
      finished = true;
      success  = false;
    }
  }

  return success;
}

// ---------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------
bool S120Fingerprint::deleteTemplate(uint16_t id, uint16_t count) {
  const uint8_t p[] = {
    CMD_DELETE,
    (uint8_t)(id >> 8), (uint8_t)(id & 0xFF),
    (uint8_t)(count >> 8), (uint8_t)(count & 0xFF)
  };
  return commandSimple(p, sizeof(p), replyTimeoutMs_);
}

bool S120Fingerprint::clearDatabase() {
  const uint8_t p[] = { CMD_CLEAR };
  return commandSimple(p, sizeof(p), replyTimeoutMs_);
}

bool S120Fingerprint::readIndexTable(uint8_t page, uint8_t bitmap[32]) {
  const uint8_t p[] = { CMD_READ_INDEX, page };
  Ack a;
  bool ok = command(p, sizeof(p), a, replyTimeoutMs_);
  if (ok && a.dataLen >= 32) {
    memcpy(bitmap, a.data, 32);
  }
  return ok;
}

bool S120Fingerprint::isEnrolled(uint16_t id, bool& enrolled) {
  uint8_t page = (uint8_t)(id / 256);
  uint8_t bitmap[32];
  if (!readIndexTable(page, bitmap)) return false;
  uint16_t bit = id % 256;
  enrolled = (bitmap[bit / 8] >> (bit % 8)) & 0x01;
  return true;
}

// ---------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------
bool S120Fingerprint::getParameters(S120Params& params) {
  const uint8_t p[] = { CMD_GET_PARAMS };
  Ack a;
  bool ok = command(p, sizeof(p), a, replyTimeoutMs_);
  if (!ok || a.dataLen < 16) return false;

  params.enrollTimes    = ((uint16_t)a.data[0]  << 8) | a.data[1];
  params.templateSize   = ((uint16_t)a.data[2]  << 8) | a.data[3];
  params.capacity       = ((uint16_t)a.data[4]  << 8) | a.data[5];
  params.scoreLevel     = ((uint16_t)a.data[6]  << 8) | a.data[7];
  params.deviceAddress  = ((uint32_t)a.data[8]  << 24) | ((uint32_t)a.data[9] << 16) |
                          ((uint32_t)a.data[10] << 8)  |  (uint32_t)a.data[11];
  params.packetSizeCode = ((uint16_t)a.data[12] << 8) | a.data[13];
  params.baudCode       = ((uint16_t)a.data[14] << 8) | a.data[15];

  switch (params.packetSizeCode) {
    case 0: params.packetSizeBytes = 32;  break;
    case 1: params.packetSizeBytes = 64;  break;
    case 2: params.packetSizeBytes = 128; break;
    case 3: params.packetSizeBytes = 256; break;
    default: params.packetSizeBytes = 0;  break;
  }
  params.baudRate = (uint32_t)params.baudCode * 9600UL;
  return true;
}

// ---------------------------------------------------------------------
// Error strings
// ---------------------------------------------------------------------
const char* S120Fingerprint::errorName(S120Error e) {
  switch (e) {
    case S120_OK:                   return "OK";
    case S120_ERR_PACKET:           return "Packet error";
    case S120_ERR_NO_FINGER:        return "No finger";
    case S120_ERR_IMAGE_CAPTURE:    return "Image capture failed";
    case S120_ERR_IMAGE_TOO_LIGHT:  return "Image too light";
    case S120_ERR_IMAGE_TOO_MUSHY:  return "Image too mushy";
    case S120_ERR_IMAGE_TOO_MESSY:  return "Image too messy";
    case S120_ERR_FEW_FEATURES:     return "Too few feature points";
    case S120_ERR_MISMATCH:         return "Fingerprints do not match";
    case S120_ERR_NOT_FOUND:        return "Fingerprint not found";
    case S120_ERR_MERGE_FAILED:     return "Failed to merge template";
    case S120_ERR_ID_OUT_OF_RANGE:  return "ID out of range";
    case S120_ERR_TEMPLATE_INVALID: return "Template invalid";
    case S120_ERR_UPLOAD_FEATURES:  return "Failed to upload features";
    case S120_ERR_RECEIVE_DATA:     return "Failed to receive data";
    case S120_ERR_UPLOAD_IMAGE:     return "Failed to upload image";
    case S120_ERR_DELETE_FAILED:    return "Failed to delete template";
    case S120_ERR_CLEAR_FAILED:     return "Failed to clear database";
    case S120_ERR_LOW_POWER_FAILED: return "Low power command failed";
    case S120_ERR_NO_VALID_IMAGE:   return "No valid image in buffer";
    case S120_ERR_RESIDUAL_FINGER:  return "Residual print / no finger movement";
    case S120_ERR_FLASH:            return "Flash read/write error";
    case S120_ERR_DB_FULL:          return "Fingerprint database full";
    case S120_ERR_TEMPLATE_NOT_EMPTY: return "ID already has a template";
    case S120_ERR_TEMPLATE_EMPTY:   return "Template empty";
    case S120_ERR_DB_EMPTY:         return "Database empty";
    case S120_ERR_BAD_ENROLL_TIMES: return "Invalid enrollment sample count";
    case S120_ERR_MODULE_TIMEOUT:   return "Module timeout";
    case S120_ERR_DUPLICATE:        return "Duplicate fingerprint";
    case S120_ERR_SENSOR:           return "Sensor operation failed";
    case S120_ERR_INVALID_DATA:     return "Invalid data";
    case S120_ERR_NO_RESPONSE:      return "No response from module (UART)";
    case S120_ERR_CHECKSUM:         return "Reply checksum error";
    case S120_ERR_BAD_ARGUMENT:     return "Invalid argument";
    case S120_ERR_ABORTED:          return "Aborted by application";
    case S120_ERR_TOUCH_TIMEOUT:    return "Touch (INT) timeout";
    case S120_ERR_REMOVAL_TIMEOUT:  return "Finger removal timeout";
    case S120_ERR_NOT_BEGUN:        return "begin() not called or failed";
    default:                        return "Other / unknown module code";
  }
}

const char* S120Fingerprint::enrollStageName(S120EnrollStage s) {
  switch (s) {
    case S120_STAGE_VALIDITY:   return "Validity check";
    case S120_STAGE_IMAGE:      return "Image acquisition";
    case S120_STAGE_FEATURES:   return "Feature generation";
    case S120_STAGE_FINGER_OFF: return "Waiting for finger to leave";
    case S120_STAGE_MERGE:      return "Merging template";
    case S120_STAGE_DUP_CHECK:  return "Duplicate check";
    case S120_STAGE_STORE:      return "Storing template";
    default:                    return "Unknown stage";
  }
}

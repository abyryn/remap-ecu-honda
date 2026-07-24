// ============================================================
// kline.cpp - Honda K-Line Driver Implementation
// Protocol: ISO 9141-2 / KWP2000 / Honda HDS (10400 baud)
// ============================================================
#include "include/kline.h"
#include "include/config.h"
#include "include/logger.h"

KLineDriver KLine;

// ---- Constructor ----
KLineDriver::KLineDriver()
    : _serial(nullptr), _txPin(KLINE_TX_PIN), _rxPin(KLINE_RX_PIN),
      _baud(KLINE_BAUD), _invert(false), _echoCancel(true),
      _initialized(false), _retryMax(KLINE_RETRY_MAX) {}

// ---- begin ----
void KLineDriver::begin(uint8_t txPin, uint8_t rxPin, uint32_t baud, bool invert) {
    _txPin  = txPin;
    _rxPin  = rxPin;
    _baud   = baud;
    _invert = invert;
    _serial = &Serial2;
    _serial->begin(baud, SERIAL_8N1, rxPin, txPin, invert);
    _flush();
    Logger.log(LOG_INFO, "KLine", "UART init TX=%d RX=%d baud=%d invert=%s", 
               txPin, rxPin, baud, invert ? "true" : "false");
}

// ---- end ----
void KLineDriver::end() {
    if (_serial) _serial->end();
    _initialized = false;
}

// ---- Drive K-Line Pin Level (for wakeup pulses & bit-bang) ----
// For optocoupler (4N35): GPIO HIGH -> LED ON -> K-Line LOW
//                         GPIO LOW  -> LED OFF -> K-Line HIGH (idle)
// So when _invert = true: _driveLine(true) = GPIO LOW = K-Line HIGH
//                         _driveLine(false) = GPIO HIGH = K-Line LOW
void KLineDriver::_driveLine(bool lineHigh) {
    bool pinValue = _invert ? !lineHigh : lineHigh;
    digitalWrite(_txPin, pinValue ? HIGH : LOW);
}

// ---- init (auto detect) ----
KLineResult KLineDriver::init(KLineInitMode mode) {
    KLineResult res = KLINE_ERR_GENERAL;

    for (uint8_t attempt = 0; attempt < _retryMax; attempt++) {
        Logger.log(LOG_INFO, "KLine", "Init attempt %d/%d mode=%d invert=%s echo=%s",
                   attempt + 1, _retryMax, mode,
                   _invert ? "true" : "false",
                   _echoCancel ? "true" : "false");

        if (mode == KLINE_FAST_INIT || mode == KLINE_AUTO_DETECT) {
            res = _fastInit();
            if (res == KLINE_OK) {
                _initialized = true;
                Logger.log(LOG_INFO, "KLine", "Fast Init OK");
                return KLINE_OK;
            }
        }

        if (mode == KLINE_5BAUD_INIT || mode == KLINE_AUTO_DETECT) {
            res = _5baudInit();
            if (res == KLINE_OK) {
                _initialized = true;
                Logger.log(LOG_INFO, "KLine", "5-Baud Init OK");
                return KLINE_OK;
            }
        }

        delay(500); // Longer inter-attempt delay for ECU recovery
    }

    Logger.log(LOG_ERROR, "KLine", "Init failed after %d retries", _retryMax);
    return KLINE_ERR_RETRY;
}

// ---- Send frame with inter-byte delay (Honda PGM-FI requires ~5ms gap) ----
void KLineDriver::_sendFrameSlow(const uint8_t* data, size_t len, uint8_t interByteMs) {
    if (!_serial || !data || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        _serial->write(data[i]);
        _serial->flush(); // Wait until byte is physically sent
        if (i < len - 1 && interByteMs > 0) {
            delay(interByteMs);
        }
    }
}

// ============================================================
// Fast Init — Honda PGM-FI Motorcycle (CB150R / Beat / Vario etc.)
//
// Strategy order optimized for Honda CB150R 2014:
//   1. 70ms wakeup + Honda PGM-FI frame (72 05 71 00 18)
//   2. 70ms wakeup + Honda HDS frame (FE 04 72 8C)
//   3. Direct Honda PGM-FI frame (no wakeup, for already-awake ECU)
//   4. ISO 14230 Fast Init (25ms/25ms wakeup)
// ============================================================
KLineResult KLineDriver::_fastInit() {
    uint8_t resp[64];
    size_t  respLen = 0;
    KLineResult r;

    // ========================================================
    // Strategy 1: 70ms Wakeup + Honda PGM-FI Init
    //   This is the PRIMARY strategy for Honda motorcycle ECUs
    //   Wakeup: K-Line LOW 70ms, then HIGH 130ms
    //   Frame: 72 05 71 00 18 (Init session request)
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 1: 70ms Wakeup + PGM-FI Init");

    // Stop UART, take manual control of TX pin for wakeup pulse
    if (_serial) _serial->end();
    delay(5); // Let UART peripheral fully release the pin
    pinMode(_txPin, OUTPUT);

    // Ensure K-Line is idle HIGH before wakeup
    _driveLine(true);  // K-Line HIGH (idle)
    delay(300);         // ECU needs idle time before wakeup

    // Wakeup pulse: K-Line LOW for 70ms
    _driveLine(false);
    delay(70);

    // Return to idle: K-Line HIGH for 130ms
    _driveLine(true);
    delay(130);

    // Re-initialize UART for data communication
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // Send Honda PGM-FI init frame with inter-byte timing
    uint8_t pgmReq[] = {0x72, 0x05, 0x71, 0x00, 0x18};
    Logger.logHex(LOG_INFO, "KLine TX (PGM-FI Init)", pgmReq, 5);
    _sendFrameSlow(pgmReq, 5, 5);

    // Cancel TX echo (single-wire K-Line echoes back what we send)
    if (_echoCancel) _drainEcho(pgmReq, 5, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (PGM-FI Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 1 SUCCESS");
            return KLINE_OK;
        }
        // Even if checksum fails, ECU responded — log it
        Logger.log(LOG_WARN, "KLine", "Strategy 1: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 1: no response (timeout)");
    }

    // ========================================================
    // Strategy 2: 70ms Wakeup + Honda HDS Init Frame
    //   Some Honda ECUs use HDS format: FE 04 72 8C
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 2: 70ms Wakeup + HDS Init");

    if (_serial) _serial->end();
    delay(5);
    pinMode(_txPin, OUTPUT);

    _driveLine(true);
    delay(300);

    _driveLine(false);
    delay(70);

    _driveLine(true);
    delay(130);

    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    uint8_t hdsReq[] = {0xFE, 0x04, 0x72, 0x8C};
    Logger.logHex(LOG_INFO, "KLine TX (HDS Init)", hdsReq, 4);
    _sendFrameSlow(hdsReq, 4, 5);
    if (_echoCancel) _drainEcho(hdsReq, 4, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (HDS Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 2 SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 2: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 2: no response (timeout)");
    }

    // ========================================================
    // Strategy 3: Direct PGM-FI Frame (no wakeup pulse)
    //   For ECUs that are already awake / listening
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 3: Direct PGM-FI (no wakeup)");

    // UART should already be initialized from Strategy 2
    delay(50);
    _flush();

    Logger.logHex(LOG_INFO, "KLine TX (Direct PGM-FI)", pgmReq, 5);
    _sendFrameSlow(pgmReq, 5, 5);
    if (_echoCancel) _drainEcho(pgmReq, 5, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 3 SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 3: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 3: no response (timeout)");
    }

    // ========================================================
    // Strategy 4: ISO 14230 Fast Init (25ms LOW, 25ms HIGH)
    //   Standard KWP2000 fast init — fallback for non-Honda ECUs
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 4: ISO 14230 Fast Init");

    if (_serial) _serial->end();
    delay(5);
    pinMode(_txPin, OUTPUT);

    _driveLine(true);
    delay(300);

    _driveLine(false);
    delay(25);

    _driveLine(true);
    delay(25);

    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(15);
    _flush();

    // ISO 14230 StartCommunication: C1 33 F1 81 + checksum
    uint8_t isoReq[] = {0xC1, 0x33, 0xF1, 0x81};
    uint8_t chk      = calcChecksum(isoReq, 4);
    uint8_t isoFrame[5];
    memcpy(isoFrame, isoReq, 4);
    isoFrame[4] = chk;

    Logger.logHex(LOG_INFO, "KLine TX (ISO 14230 Init)", isoFrame, 5);
    _sendFrameSlow(isoFrame, 5, 5);
    if (_echoCancel) _drainEcho(isoFrame, 5, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (ISO Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 4 SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 4: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 4: no response (timeout)");
    }

    Logger.log(LOG_WARN, "KLine", "Fast Init failed: no valid response from any strategy");
    return KLINE_ERR_TIMEOUT;
}

// ---- 5-Baud Init (ISO 9141) ----
KLineResult KLineDriver::_5baudInit() {
    Logger.log(LOG_INFO, "KLine", "Starting 5-Baud Init (address 0x33)");

    _flush();
    if (_serial) _serial->end();
    delay(5);

    pinMode(_txPin, OUTPUT);
    _driveLine(true); // idle HIGH
    delay(500);        // Longer idle before 5-baud init (ISO 9141 requires >= 300ms)

    // Bit-bang 0x33 (address for ECU broadcast) at 5 baud = 200ms/bit
    // Total time: start(200ms) + 8 data bits(1600ms) + stop(200ms) = 2000ms
    Logger.log(LOG_INFO, "KLine", "Bit-bang 0x33 at 5 baud (2 seconds)...");
    _bitBangByte(0x33, 5);

    // Wait W1 (200-300ms) before re-initializing UART
    delay(200);

    // Re-init UART at 10400 with inversion configuration
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // Wait for sync byte 0x55 from ECU (W2 timeout: up to 300ms per ISO 9141)
    // We use 2000ms for safety since bit-bang may have timing jitter
    uint32_t t = millis();
    bool gotSync = false;

    Logger.log(LOG_INFO, "KLine", "Waiting for sync byte 0x55...");
    while (millis() - t < 2000) {
        if (_serial->available()) {
            uint8_t b = _serial->read();
            Logger.log(LOG_DEBUG, "KLine", "5Baud RX byte: 0x%02X", b);

            // Skip noise 0x00 before sync byte (but NOT 0xFF — could be valid)
            if (b == 0x00) continue;

            if (b == 0x55) {
                Logger.log(LOG_INFO, "KLine", "Sync byte 0x55 received!");
                gotSync = true;
                break;
            } else {
                // Unexpected byte — log but continue waiting
                Logger.log(LOG_WARN, "KLine", "Unexpected byte 0x%02X (expected 0x55)", b);
            }
        }
        yield();
    }

    if (!gotSync) {
        Logger.log(LOG_WARN, "KLine", "5-Baud: no sync byte after 2s");
        return KLINE_ERR_TIMEOUT;
    }

    // Wait W3 (5-20ms) then read keyword bytes (KW1, KW2)
    delay(10);
    uint8_t kw[3] = {0};
    size_t  kwLen = 0;
    receiveRaw(kw, kwLen, 200);

    if (kwLen >= 2) {
        Logger.log(LOG_INFO, "KLine", "Keywords: KW1=0x%02X KW2=0x%02X", kw[0], kw[1]);
        // W4 delay: 25-50ms before sending inverted KW2
        delay(30);
        uint8_t ackByte = ~kw[1];
        Logger.log(LOG_INFO, "KLine", "Sending ACK: 0x%02X (~KW2)", ackByte);
        sendRaw(&ackByte, 1);

        // Wait for ECU complement of address byte (W5)
        delay(50);
        uint8_t ecuAck[2] = {0};
        size_t ackLen = 0;
        receiveRaw(ecuAck, ackLen, 300);
        if (ackLen >= 1) {
            Logger.log(LOG_INFO, "KLine", "ECU ACK: 0x%02X", ecuAck[0]);
        }
    } else {
        Logger.log(LOG_WARN, "KLine", "5-Baud: no keyword bytes received (got %d bytes)", kwLen);
        return KLINE_ERR_TIMEOUT;
    }

    return KLINE_OK;
}

// ---- Bit-bang a single byte at given baud ----
void KLineDriver::_bitBangByte(uint8_t byte, uint32_t baud) {
    uint32_t bitTimeUs = 1000000UL / baud;

    // Start bit (LOW on K-Line)
    _driveLine(false);
    delayMicroseconds(bitTimeUs);

    // Data bits LSB first
    for (int i = 0; i < 8; i++) {
        _driveLine((byte >> i) & 0x01);
        delayMicroseconds(bitTimeUs);
    }

    // Stop bit (HIGH on K-Line = idle)
    _driveLine(true);
    delayMicroseconds(bitTimeUs);
}

// ---- Drain TX echo on single-wire K-Line ----
// On single-wire K-Line, the TX signal is echoed back on RX.
// We need to drain these echo bytes before reading the ECU response.
void KLineDriver::_drainEcho(const uint8_t* sentData, size_t len, uint32_t timeoutMs) {
    if (!_serial || !sentData || len == 0) return;

    // Small delay to let echo bytes arrive in UART buffer
    delay(5);

    uint32_t start = millis();
    size_t drained = 0;

    while (drained < len && (millis() - start < timeoutMs)) {
        if (_serial->available()) {
            uint8_t b = _serial->read();

            // Compare incoming byte with expected transmitted echo byte
            if (b == sentData[drained]) {
                drained++;
            } else {
                // Byte does not match echo — this could be ECU response!
                // Push it back... but we can't on HardwareSerial.
                // Log it so we know what happened.
                Logger.log(LOG_WARN, "KLine", "Echo drain: byte %d mismatch (got 0x%02X, expected 0x%02X)",
                           drained, b, sentData[drained]);
                break;
            }
        }
        yield();
    }

    if (drained > 0) {
        Logger.log(LOG_DEBUG, "KLine", "Echo drained %d/%d bytes", drained, len);
    }
}

// ---- sendRaw ----
KLineResult KLineDriver::sendRaw(const uint8_t* data, size_t len) {
    if (!_serial) return KLINE_ERR_GENERAL;
    _flush();
    Logger.logHex(LOG_DEBUG, "KLine TX", data, len);
    _serial->write(data, len);
    _serial->flush();

    // Drain TX Echo
    if (_echoCancel) {
        _drainEcho(data, len, 80);
    }
    return KLINE_OK;
}

// ---- receiveRaw ----
// Read bytes from K-Line with timeout.
// Only filters leading 0x00 as noise (break condition).
// Does NOT filter 0xFF — it can be a valid ECU response byte.
KLineResult KLineDriver::receiveRaw(uint8_t* buf, size_t& len, uint32_t timeoutMs) {
    len = 0;
    if (!_serial) return KLINE_ERR_GENERAL;

    uint32_t start = millis();
    bool receivingStarted = false;

    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            uint8_t b = _serial->read();

            // Only skip leading 0x00 (break condition noise)
            // Do NOT skip 0xFF — it's a valid byte in many protocols
            if (!receivingStarted && b == 0x00) {
                Logger.log(LOG_DEBUG, "KLine", "Skipping leading 0x00 noise");
                continue;
            }

            receivingStarted = true;
            buf[len++] = b;

            // Reset timeout on each received byte (inter-byte gap detection)
            start = millis();
            if (len >= 255) break;
        }
        yield();
    }

    if (len == 0) return KLINE_ERR_TIMEOUT;
    Logger.logHex(LOG_INFO, "KLine RX Raw", buf, len);
    return KLINE_OK;
}

// ---- request (send + receive with checksum validation) ----
KLineResult KLineDriver::request(const uint8_t* req, size_t reqLen,
                                  uint8_t* resp, size_t& respLen,
                                  uint32_t timeoutMs) {
    if (!_initialized) return KLINE_ERR_NOINIT;

    for (uint8_t attempt = 0; attempt < _retryMax; attempt++) {
        _flush();

        // Send with inter-byte delay for Honda protocol compliance
        Logger.logHex(LOG_DEBUG, "KLine TX (request)", req, reqLen);
        _sendFrameSlow(req, reqLen, 5);

        // Drain echo
        if (_echoCancel) {
            _drainEcho(req, reqLen, 80);
        }

        respLen = 0;
        KLineResult r = receiveRaw(resp, respLen, timeoutMs);
        if (r != KLINE_OK) {
            Logger.log(LOG_WARN, "KLine", "Request timeout (attempt %d)", attempt + 1);
            delay(50); // Small delay before retry
            continue;
        }

        if (!validateChecksum(resp, respLen)) {
            Logger.log(LOG_WARN, "KLine", "CRC error (attempt %d)", attempt + 1);
            delay(50);
            continue;
        }

        return KLINE_OK;
    }

    return KLINE_ERR_RETRY;
}

// ---- calcChecksum (sum mod 256) ----
uint8_t KLineDriver::calcChecksum(const uint8_t* data, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

// ---- validateChecksum ----
bool KLineDriver::validateChecksum(const uint8_t* data, size_t len) {
    if (len < 2) return false;

    // 1. Standard sum mod 256
    uint8_t expectedSum = calcChecksum(data, len - 1);
    if (expectedSum == data[len - 1]) return true;

    // 2. Honda 2's complement sum (sum of all frame bytes mod 256 == 0)
    uint16_t totalSum = 0;
    for (size_t i = 0; i < len; i++) totalSum += data[i];
    if ((totalSum & 0xFF) == 0) return true;

    return false;
}

// ---- flush ----
void KLineDriver::_flush() {
    if (_serial) {
        while (_serial->available()) _serial->read();
    }
}

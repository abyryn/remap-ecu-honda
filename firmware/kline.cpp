// ============================================================
// kline.cpp - Honda K-Line Driver Implementation
// Protocol: ISO 9141-2 / KWP2000 / Honda HDS (10400 baud)
// ============================================================
#include "include/kline.h"
#include "include/config.h"
#include "include/logger.h"
#include <esp_task_wdt.h>

KLineDriver KLine;

// ---- Constructor ----
KLineDriver::KLineDriver()
    : _serial(nullptr), _txPin(KLINE_TX_PIN), _rxPin(KLINE_RX_PIN),
      _baud(KLINE_BAUD), _invert(true), _echoCancel(true),
      _initialized(false), _retryMax(KLINE_RETRY_MAX) {}

// ---- begin ----
void KLineDriver::begin(uint8_t txPin, uint8_t rxPin, uint32_t baud, bool invert) {
    _txPin  = txPin;
    _rxPin  = rxPin;
    _baud   = baud;
    _invert = invert;
    _serial = &Serial2;

#ifdef KLINE_DTR_PIN
    pinMode(KLINE_DTR_PIN, OUTPUT);
    digitalWrite(KLINE_DTR_PIN, HIGH); // DTR Gate ON (GPIO 19)
#endif
#ifdef KLINE_CTS_PIN
    pinMode(KLINE_CTS_PIN, INPUT_PULLUP); // CTS / CTR Pin (GPIO 18)
#endif

    _serial->begin(baud, SERIAL_8N1, rxPin, txPin, invert);
    _flush();
    Logger.log(LOG_INFO, "KLine", "UART init TX=%d RX=%d DTR=%d CTS=%d baud=%d invert=%s", 
               txPin, rxPin, KLINE_DTR_PIN, KLINE_CTS_PIN, baud, invert ? "true" : "false");
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
    bool originalInvert = _invert;

    // Retry loop with current invert setting, then try opposite invert setting if AUTO_DETECT
    for (uint8_t pass = 0; pass < (mode == KLINE_AUTO_DETECT ? 2 : 1); pass++) {
        if (pass == 1) {
            _invert = !_invert;
            Logger.log(LOG_INFO, "KLine", "Auto-detect retrying with toggled invert=%s",
                       _invert ? "true" : "false");
            if (_serial) {
                _serial->end();
                _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
            }
        }

        for (uint8_t attempt = 0; attempt < _retryMax; attempt++) {
            Logger.log(LOG_INFO, "KLine", "Init attempt %d/%d mode=%d invert=%s echo=%s",
                       attempt + 1, _retryMax, mode,
                       _invert ? "true" : "false",
                       _echoCancel ? "true" : "false");

            if (mode == KLINE_FAST_INIT || mode == KLINE_AUTO_DETECT) {
                res = _fastInit();
                if (res == KLINE_OK) {
                    _initialized = true;
                    Logger.log(LOG_INFO, "KLine", "Fast Init OK (invert=%s)", _invert ? "true" : "false");
                    return KLINE_OK;
                }
            }

            if (mode == KLINE_5BAUD_INIT || mode == KLINE_AUTO_DETECT) {
                res = _5baudInit();
                if (res == KLINE_OK) {
                    _initialized = true;
                    Logger.log(LOG_INFO, "KLine", "5-Baud Init OK (invert=%s)", _invert ? "true" : "false");
                    return KLINE_OK;
                }
            }

            delay(300);
        }
    }

    _invert = originalInvert; // restore original if failed
    Logger.log(LOG_ERROR, "KLine", "Init failed after all retries");
    return KLINE_ERR_RETRY;
}

// ---- Send frame with inter-byte delay (Honda PGM-FI requires ~5ms gap) ----
void KLineDriver::_sendFrameSlow(const uint8_t* data, size_t len, uint8_t interByteMs) {
    if (!_serial || !data || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        esp_task_wdt_reset();
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
// ---- Send K-Line Wakeup Pulse (LOW for lowMs, HIGH for highMs) ----
void KLineDriver::_sendWakeupPulse(uint32_t lowMs, uint32_t highMs) {
    esp_task_wdt_reset();
    pinMode(_txPin, OUTPUT);
    _driveLine(true);  // K-Line HIGH (idle)
    delay(200);        // Idle time before pulse

    esp_task_wdt_reset();
    _driveLine(false); // K-Line LOW pulse
    delay(lowMs);

    esp_task_wdt_reset();
    _driveLine(true);  // K-Line HIGH (idle)
    delay(highMs);

    // Re-attach TX pin to ESP32 UART peripheral matrix cleanly
    if (_serial) {
        _serial->end();
        _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    }
    delay(20);
    _flush();
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

    uint8_t pgmReq[] = {0x72, 0x05, 0x71, 0x00, 0x18};
    uint8_t hdsReq[] = {0xFE, 0x04, 0x72, 0x8C};

    // ========================================================
    // Strategy 1: Direct PGM-FI Frame (no wakeup pulse)
    // Most Honda motorcycle ECUs (CB150R) when key is ON are already awake!
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 1: Direct PGM-FI (no wakeup pulse)");
    _flush();

    Logger.logHex(LOG_INFO, "KLine TX (Direct PGM-FI)", pgmReq, 5);
    _sendFrameSlow(pgmReq, 5, 2);
    if (_echoCancel) _drainEcho(pgmReq, 5, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 300);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 1 (Direct) SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 1: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 1: no response");
    }

    // ========================================================
    // Strategy 2: 70ms Wakeup + Honda PGM-FI Init
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 2: 70ms Wakeup + PGM-FI Init");

    _sendWakeupPulse(70, 130);

    Logger.logHex(LOG_INFO, "KLine TX (PGM-FI Init)", pgmReq, 5);
    _sendFrameSlow(pgmReq, 5, 2);
    if (_echoCancel) _drainEcho(pgmReq, 5, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (PGM-FI Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 2 (70ms Wakeup) SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 2: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 2: no response");
    }

    // ========================================================
    // Strategy 3: 70ms Wakeup + Honda HDS Init Frame
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 3: 70ms Wakeup + HDS Init");

    _sendWakeupPulse(70, 130);

    Logger.logHex(LOG_INFO, "KLine TX (HDS Init)", hdsReq, 4);
    _sendFrameSlow(hdsReq, 4, 2);
    if (_echoCancel) _drainEcho(hdsReq, 4, 80);

    respLen = 0;
    r = receiveRaw(resp, respLen, 400);
    if (r == KLINE_OK && respLen >= 2) {
        Logger.logHex(LOG_INFO, "KLine RX (HDS Response)", resp, respLen);
        if (validateChecksum(resp, respLen)) {
            Logger.log(LOG_INFO, "KLine", "Strategy 3 (HDS Init) SUCCESS");
            return KLINE_OK;
        }
        Logger.log(LOG_WARN, "KLine", "Strategy 3: ECU responded but checksum mismatch");
    } else {
        Logger.log(LOG_WARN, "KLine", "Strategy 3: no response");
    }

    // ========================================================
    // Strategy 4: ISO 14230 Fast Init (25ms LOW, 25ms HIGH)
    // ========================================================
    Logger.log(LOG_INFO, "KLine", "Strategy 4: ISO 14230 Fast Init");

    _sendWakeupPulse(25, 25);

    uint8_t isoReq[] = {0xC1, 0x33, 0xF1, 0x81};
    uint8_t chk      = calcHondaChecksum(isoReq, 4);
    uint8_t isoFrame[5];
    memcpy(isoFrame, isoReq, 4);
    isoFrame[4] = chk;

    Logger.logHex(LOG_INFO, "KLine TX (ISO 14230 Init)", isoFrame, 5);
    _sendFrameSlow(isoFrame, 5, 2);
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
        Logger.log(LOG_WARN, "KLine", "Strategy 4: no response");
    }

    Logger.log(LOG_WARN, "KLine", "Fast Init failed: no valid response from any strategy");
    return KLINE_ERR_TIMEOUT;
}
    return KLINE_ERR_TIMEOUT;
}

// ---- 5-Baud Init (ISO 9141) ----
KLineResult KLineDriver::_5baudInit() {
    Logger.log(LOG_INFO, "KLine", "Starting 5-Baud Init (address 0x33)");

    _flush();

    pinMode(_txPin, OUTPUT);
    _driveLine(true); // idle HIGH
    delay(500);        // Longer idle before 5-baud init (ISO 9141 requires >= 300ms)

    // Bit-bang 0x33 (address for ECU broadcast) at 5 baud = 200ms/bit
    // Total time: start(200ms) + 8 data bits(1600ms) + stop(200ms) = 2000ms
    Logger.log(LOG_INFO, "KLine", "Bit-bang 0x33 at 5 baud (2 seconds)...");
    _bitBangByte(0x33, 5);

    // Wait W1 (200-300ms) before re-initializing UART
    delay(200);

    // Re-attach UART pins
    _serial->setPins(_rxPin, _txPin);
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

    uint32_t start = millis();
    size_t drained = 0;

    while (drained < len && (millis() - start < timeoutMs)) {
        if (_serial->available()) {
            uint8_t b = (uint8_t)_serial->peek();

            if (b == sentData[drained]) {
                _serial->read(); // Confirm and remove verified echo byte
                drained++;
                start = millis();
            } else {
                Logger.log(LOG_DEBUG, "KLine", "Echo drain stopped at byte %d (got 0x%02X, expected 0x%02X)",
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
    uint32_t lastByteTime = millis();
    bool receivingStarted = false;

    while (millis() - start < timeoutMs) {
        esp_task_wdt_reset();
        if (_serial->available()) {
            uint8_t b = _serial->read();

            if (!receivingStarted && b == 0x00) {
                Logger.log(LOG_DEBUG, "KLine", "Skipping leading 0x00 noise");
                continue;
            }

            receivingStarted = true;
            buf[len++] = b;

            lastByteTime = millis();
            if (len >= 255) break;
        } else {
            if (receivingStarted && (millis() - lastByteTime > 35)) {
                // Inter-byte gap > 35ms means ECU frame is complete
                break;
            }
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
        esp_task_wdt_reset();
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

// ---- calcChecksum (Honda 2's complement) ----
uint8_t KLineDriver::calcChecksum(const uint8_t* data, size_t len) {
    return calcHondaChecksum(data, len);
}

// ---- calcHondaChecksum ----
uint8_t KLineDriver::calcHondaChecksum(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
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

// ---- testHardware ----
void KLineDriver::testHardware() {
    Logger.log(LOG_INFO, "Diag", "=== START HARDWARE DIAGNOSTIC ===");

    if (_serial) _serial->end();

    pinMode(_txPin, OUTPUT);
    pinMode(_rxPin, INPUT);

#ifdef KLINE_DTR_PIN
    pinMode(KLINE_DTR_PIN, OUTPUT);
    digitalWrite(KLINE_DTR_PIN, HIGH);
#endif

    // Test TX HIGH
    digitalWrite(_txPin, HIGH);
    delay(20);
    int rxState1 = digitalRead(_rxPin);

    // Test TX LOW
    digitalWrite(_txPin, LOW);
    delay(20);
    int rxState2 = digitalRead(_rxPin);

    Logger.log(LOG_INFO, "Diag", "TX Pin %d HIGH -> RX Pin %d reads %s", _txPin, _rxPin, rxState1 ? "HIGH" : "LOW");
    Logger.log(LOG_INFO, "Diag", "TX Pin %d LOW  -> RX Pin %d reads %s", _txPin, _rxPin, rxState2 ? "HIGH" : "LOW");

    if (rxState1 == rxState2) {
        Logger.log(LOG_ERROR, "Diag", ">>> HARDWARE FAILURE: RX Pin %d is STUCK at %s! <<<", _rxPin, rxState1 ? "HIGH" : "LOW");
        Logger.log(LOG_ERROR, "Diag", "  1. Check Pull-up Resistor (4.7k) on 4N35 Pin 5 Collector");
        Logger.log(LOG_ERROR, "Diag", "  2. Check GND Wire Connection between ESP32 and Bike/OBD");
        Logger.log(LOG_ERROR, "Diag", "  3. Check 4N35 Pin 6 is DISCONNECTED (NC)");
    } else {
        Logger.log(LOG_INFO, "Diag", ">>> HARDWARE PASSED: Signal toggles OK (Inverted=%s) <<<",
                   (rxState1 == LOW && rxState2 == HIGH) ? "YES" : "NO");
    }

    if (_serial) {
        _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    }
    Logger.log(LOG_INFO, "Diag", "=== END HARDWARE DIAGNOSTIC ===");
}

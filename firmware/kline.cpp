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

// ---- Drive K-Line Pin Level ----
void KLineDriver::_driveLine(bool lineHigh) {
    // If _invert is true (inverting optocoupler circuit like 4N35/4N25):
    // Line HIGH (5V/12V idle) is produced by GPIO LOW (LED off).
    // Line LOW (0V active pulse) is produced by GPIO HIGH (LED on).
    bool pinValue = _invert ? !lineHigh : lineHigh;
    digitalWrite(_txPin, pinValue ? HIGH : LOW);
}

// ---- init (auto detect) ----
KLineResult KLineDriver::init(KLineInitMode mode) {
    KLineResult res = KLINE_ERR_GENERAL;

    for (uint8_t attempt = 0; attempt < _retryMax; attempt++) {
        Logger.log(LOG_INFO, "KLine", "Init attempt %d mode=%d", attempt + 1, mode);

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

        delay(300);
    }

    Logger.log(LOG_ERROR, "KLine", "Init failed after %d retries", _retryMax);
    return KLINE_ERR_RETRY;
}

// ---- Fast Init (Honda HDS / ISO 14230) ----
KLineResult KLineDriver::_fastInit() {
    _flush();

    uint8_t resp[64];
    size_t  respLen = 0;
    KLineResult r;

    // --- Strategy 1: Direct Honda HDS Init (No Wakeup Pulse) ---
    // Many Honda motorcycle ECUs (Beat, Vario, Scoopy, CBR) are already powered and listening at 10400 bps.
    // Sending a 70ms LOW pulse can cause a break condition that resets their UART receiver.
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    uint8_t hdsReq[] = {0xFE, 0x04, 0x72, 0x8C}; // FE=Header, 04=Len, 72=Init, 8C=Chk
    Logger.logHex(LOG_INFO, "KLine TX (Direct HDS Init)", hdsReq, 4);
    _serial->write(hdsReq, 4);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsReq, 4, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct HDS Response)", resp, respLen);
        return KLINE_OK;
    }

    // --- Strategy 2: Honda PGM-FI Motorcycle Direct Frame (72 05 71 00 18) ---
    uint8_t hdsAltReq[] = {0x72, 0x05, 0x71, 0x00, 0x18};
    _flush();
    Logger.logHex(LOG_INFO, "KLine TX (Direct PGM-FI Init)", hdsAltReq, 5);
    _serial->write(hdsAltReq, 5);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsAltReq, 5, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct PGM-FI Response)", resp, respLen);
        return KLINE_OK;
    }

    // --- Strategy 3: Honda HDS Init with 70ms Wakeup Pulse ---
    if (_serial) _serial->end();
    pinMode(_txPin, OUTPUT);

    _driveLine(false); // K-Line LOW
    delay(70);
    _driveLine(true);  // K-Line HIGH (Idle)
    delay(130);

    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    Logger.logHex(LOG_INFO, "KLine TX (HDS 70ms Wakeup Init)", hdsReq, 4);
    _serial->write(hdsReq, 4);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsReq, 4, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (70ms Wakeup Response)", resp, respLen);
        return KLINE_OK;
    }

    // --- Strategy 4: ISO 14230 Fast Init (25ms LOW, 25ms HIGH) ---
    _flush();
    if (_serial) _serial->end();
    pinMode(_txPin, OUTPUT);
    _driveLine(false); delay(25);
    _driveLine(true);  delay(25);
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(15);
    _flush();

    uint8_t isoReq[] = {0xC1, 0x33, 0xF1, 0x81};
    uint8_t chk      = calcChecksum(isoReq, 4);
    uint8_t isoFrame[5];
    memcpy(isoFrame, isoReq, 4);
    isoFrame[4] = chk;

    Logger.logHex(LOG_INFO, "KLine TX (ISO 14230 Init)", isoFrame, 5);
    _serial->write(isoFrame, 5);
    _serial->flush();
    if (_echoCancel) _drainEcho(isoFrame, 5, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (ISO Response)", resp, respLen);
        return KLINE_OK;
    }

    Logger.log(LOG_WARN, "KLine", "Fast Init failed: no valid response from ECU");
    return KLINE_ERR_TIMEOUT;
}

// ---- 5-Baud Init (ISO 9141) ----
KLineResult KLineDriver::_5baudInit() {
    _flush();
    if (_serial) _serial->end();

    pinMode(_txPin, OUTPUT);
    _driveLine(true); // idle HIGH
    delay(300);

    // Bit-bang 0x33 (address for ECU broadcast) at 5 baud = 200ms/bit
    _bitBangByte(0x33, 5);

    // Re-init UART at 10400 with inversion configuration
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // Drain TX echo from bit-bang if enabled
    uint8_t addressByte[] = {0x33};
    if (_echoCancel) {
        _drainEcho(addressByte, 1, 50);
    }

    // Wait for sync byte 0x55 from ECU (up to 500ms)
    uint32_t t = millis();
    while (millis() - t < 500) {
        if (_serial->available()) {
            uint8_t b = _serial->read();
            // Skip noise 0x00 or 0xFF before sync byte
            if (b == 0x00 || b == 0xFF) continue;

            Logger.log(LOG_DEBUG, "KLine", "5Baud sync byte: 0x%02X", b);
            if (b == 0x55) {
                delay(10);
                uint8_t kw[3] = {0};
                size_t  kwLen = 0;
                receiveRaw(kw, kwLen, 200);
                if (kwLen >= 2) {
                    uint8_t ackByte = ~kw[1];
                    sendRaw(&ackByte, 1);
                }
                return KLINE_OK;
            }
        }
        yield();
    }

    Logger.log(LOG_WARN, "KLine", "5-Baud: no sync byte");
    return KLINE_ERR_TIMEOUT;
}

// ---- Bit-bang a single byte at given baud ----
void KLineDriver::_bitBangByte(uint8_t byte, uint32_t baud) {
    uint32_t bitTimeUs = 1000000UL / baud;

    // Start bit (LOW)
    _driveLine(false);
    delayMicroseconds(bitTimeUs);

    // Data bits LSB first
    for (int i = 0; i < 8; i++) {
        _driveLine((byte >> i) & 0x01);
        delayMicroseconds(bitTimeUs);
    }

    // Stop bit (HIGH)
    _driveLine(true);
    delayMicroseconds(bitTimeUs);
}

// ---- Drain TX echo on single-wire K-Line (Smart Drain) ----
void KLineDriver::_drainEcho(const uint8_t* sentData, size_t len, uint32_t timeoutMs) {
    if (!_serial || !sentData || len == 0) return;
    uint32_t start = millis();
    size_t drained = 0;

    while (drained < len && (millis() - start < timeoutMs)) {
        if (_serial->available()) {
            uint8_t peekByte = _serial->peek();

            // Compare incoming byte with expected transmitted echo byte
            if (peekByte == sentData[drained]) {
                _serial->read(); // Remove matching echo byte
                drained++;
            } else {
                // Byte does not match sent echo! Echo is absent or done; this byte is ECU response.
                Logger.log(LOG_DEBUG, "KLine", "Echo drain stopped at idx %d (byte 0x%02X != 0x%02X)",
                           drained, peekByte, sentData[drained]);
                break;
            }
        }
        yield();
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
        _drainEcho(data, len, 50);
    }
    return KLINE_OK;
}

// ---- receiveRaw ----
KLineResult KLineDriver::receiveRaw(uint8_t* buf, size_t& len, uint32_t timeoutMs) {
    len = 0;
    if (!_serial) return KLINE_ERR_GENERAL;

    uint32_t start = millis();
    bool receivingStarted = false;

    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            uint8_t b = _serial->read();

            // Ignore leading noise / break condition bytes (0x00 or 0xFF) before frame starts
            if (!receivingStarted && _echoCancel && (b == 0x00 || b == 0xFF)) {
                Logger.log(LOG_DEBUG, "KLine", "Ignoring leading noise byte: 0x%02X", b);
                continue;
            }

            receivingStarted = true;
            buf[len++] = b;
            // Reset timeout on each byte received
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
        KLineResult r = sendRaw(req, reqLen);
        if (r != KLINE_OK) continue;

        r = receiveRaw(resp, respLen, timeoutMs);
        if (r != KLINE_OK) {
            Logger.log(LOG_WARN, "KLine", "Request timeout (attempt %d)", attempt + 1);
            continue;
        }

        if (!validateChecksum(resp, respLen)) {
            Logger.log(LOG_WARN, "KLine", "CRC error (attempt %d)", attempt + 1);
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

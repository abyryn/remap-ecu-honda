// ============================================================
// kline.cpp - Honda K-Line Driver Implementation
// Protocol: ISO 9141-2 / KWP2000 (10400 baud)
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

        delay(500);
    }

    Logger.log(LOG_ERROR, "KLine", "Init failed after %d retries", _retryMax);
    return KLINE_ERR_RETRY;
}

// ---- Fast Init (Honda HDS / ISO 14230) ----
KLineResult KLineDriver::_fastInit() {
    _flush();

    // Temporarily take over TX pin for bit-bang wake up pulse
    if (_serial) _serial->end();
    pinMode(_txPin, OUTPUT);

    // --- WAKE UP PATTERN ---
    // Honda Keihin/Shindengen motorcycle ECUs: 70ms LOW, 130ms HIGH
    digitalWrite(_txPin, LOW);
    delay(70);
    digitalWrite(_txPin, HIGH);
    delay(130);

    // Re-init UART with inversion configuration preserved
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush(); // Clear break condition noise (0x00 / 0xFF)

    uint8_t resp[32];
    size_t  respLen = 0;

    // Attempt 1: Honda HDS Init Frame (FE 04 72 8C)
    uint8_t hdsReq[] = {0xFE, 0x04, 0x72, 0x8C};
    Logger.logHex(LOG_DEBUG, "KLine TX (Honda HDS Init)", hdsReq, 4);
    _serial->write(hdsReq, 4);
    _serial->flush();
    if (_echoCancel) _drainEcho(4, 50);

    KLineResult r = receiveRaw(resp, respLen, 300);
    if (r == KLINE_OK && respLen >= 3 && resp[0] != 0x00 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_DEBUG, "KLine RX (HDS Response)", resp, respLen);
        return KLINE_OK;
    }

    Logger.log(LOG_WARN, "KLine", "HDS Init: no valid response, trying ISO 14230 (C1 33 F1 81)");

    // Attempt 2: ISO 14230 Start Communication (C1 33 F1 81 66)
    _flush();
    uint8_t isoReq[] = {0xC1, 0x33, 0xF1, 0x81};
    uint8_t chk      = calcChecksum(isoReq, 4);
    uint8_t frame[5];
    memcpy(frame, isoReq, 4);
    frame[4] = chk;

    Logger.logHex(LOG_DEBUG, "KLine TX (ISO Init)", frame, 5);
    _serial->write(frame, 5);
    _serial->flush();
    if (_echoCancel) _drainEcho(5, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 300);
    if (r == KLINE_OK && respLen >= 3 && resp[0] != 0x00 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_DEBUG, "KLine RX (ISO Response)", resp, respLen);
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
    digitalWrite(_txPin, HIGH);
    delay(300);  // idle

    // Bit-bang 0x33 (address for ECU broadcast) at 5 baud = 200ms/bit
    _bitBangByte(0x33, 5);

    // Re-init UART at 10400 with inversion configuration
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // Drain TX echo from bit-bang
    if (_echoCancel) {
        _drainEcho(1, 50);
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
                // Read keyword bytes (W1, W2) and inverted W2
                delay(10);
                uint8_t kw[3] = {0};
                size_t  kwLen = 0;
                receiveRaw(kw, kwLen, 200);
                // Send ~W2
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
    digitalWrite(_txPin, LOW);
    delayMicroseconds(bitTimeUs);

    // Data bits LSB first
    for (int i = 0; i < 8; i++) {
        digitalWrite(_txPin, (byte >> i) & 0x01 ? HIGH : LOW);
        delayMicroseconds(bitTimeUs);
    }

    // Stop bit (HIGH)
    digitalWrite(_txPin, HIGH);
    delayMicroseconds(bitTimeUs);
}

// ---- Drain TX echo on single-wire K-Line ----
void KLineDriver::_drainEcho(size_t echoLen, uint32_t timeoutMs) {
    if (!_serial) return;
    uint32_t start = millis();
    size_t drained = 0;
    while (drained < echoLen && (millis() - start < timeoutMs)) {
        if (_serial->available()) {
            _serial->read();
            drained++;
        }
        yield();
    }
    // Small delay to allow trailing echo/noise to settle, then clear remaining buffer
    delay(5);
    while (_serial->available()) {
        _serial->read();
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
        _drainEcho(len, 50);
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
            if (!receivingStarted && (b == 0x00 || b == 0xFF)) {
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
    Logger.logHex(LOG_DEBUG, "KLine RX", buf, len);
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
    uint8_t expected = calcChecksum(data, len - 1);
    return (expected == data[len - 1]);
}

// ---- flush ----
void KLineDriver::_flush() {
    if (_serial) {
        while (_serial->available()) _serial->read();
    }
}

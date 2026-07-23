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
      _initialized(false), _retryMax(KLINE_RETRY_MAX),
      _dtrPin(-1), _ctsPin(-1),
      _dtrActive(KLINE_DTR_ACTIVE), _ctsBusyLevel(KLINE_CTS_BUSY),
      _busBusyCount(0), _collisionCount(0) {}

// ---- begin ----
void KLineDriver::begin(uint8_t txPin, uint8_t rxPin, uint32_t baud,
                        bool invert, int8_t dtrPin, int8_t ctsPin) {
    _txPin  = txPin;
    _rxPin  = rxPin;
    _baud   = baud;
    _invert = invert;
    _serial = &Serial2;
    _serial->begin(baud, SERIAL_8N1, rxPin, txPin, invert);
    _flush();

    // --- DTR: TX Enable Gate ---
    _dtrPin = dtrPin;
    if (_dtrPin >= 0) {
        pinMode(_dtrPin, OUTPUT);
        // Default: DTR LOW = gate off (idle, tidak ada TX)
        digitalWrite(_dtrPin, _dtrActive ? LOW : HIGH);
        Logger.log(LOG_INFO, "KLine", "DTR enabled on GPIO%d (active=%s)",
                   _dtrPin, _dtrActive ? "HIGH" : "LOW");
    }

    // --- CTS: Bus Busy Detect ---
    _ctsPin = ctsPin;
    if (_ctsPin >= 0) {
        // Pull-up agar idle HIGH; K-Line LOW akan menarik CTS ke LOW
        pinMode(_ctsPin, INPUT_PULLUP);
        Logger.log(LOG_INFO, "KLine", "CTS enabled on GPIO%d (busy=%s)",
                   _ctsPin, _ctsBusyLevel == LOW ? "LOW" : "HIGH");
    }

    Logger.log(LOG_INFO, "KLine", "UART init TX=%d RX=%d baud=%d invert=%s DTR=%d CTS=%d",
               txPin, rxPin, baud, invert ? "true" : "false", _dtrPin, _ctsPin);
}

// ---- end ----
void KLineDriver::end() {
    if (_serial) _serial->end();
    // DTR LOW saat shutdown: matikan gerbang TX
    if (_dtrPin >= 0) {
        digitalWrite(_dtrPin, _dtrActive ? LOW : HIGH);
    }
    _initialized = false;
}

// ---- Drive K-Line Pin Level ----
void KLineDriver::_driveLine(bool lineHigh) {
    // Jika _invert = true (inverting optocoupler seperti 4N35/4N25):
    // Line HIGH (5V/12V idle) dihasilkan dengan GPIO LOW (LED off).
    // Line LOW  (0V pulse aktif) dihasilkan dengan GPIO HIGH (LED on).
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
    // Banyak ECU Honda (Beat, Vario, Scoopy, CBR) sudah power-up dan siap di 10400 bps.
    // DTR diaktifkan untuk membuka gerbang TX optocoupler sebelum mengirim.

    // [DTR] Buka gerbang TX
    enableDTR(true);

    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // [CTS] Cek bus idle sebelum TX
    if (!waitCTSClear()) {
        Logger.log(LOG_WARN, "KLine", "Fast Init Strat1: K-Line bus busy, menunggu idle...");
    }

    uint8_t hdsReq[] = {0xFE, 0x04, 0x72, 0x8C}; // FE=Header, 04=Len, 72=Init, 8C=Chk
    Logger.logHex(LOG_INFO, "KLine TX (Direct HDS Init)", hdsReq, 4);
    _serial->write(hdsReq, 4);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsReq, 4, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct HDS Response)", resp, respLen);
        enableDTR(false); // Tutup gerbang TX setelah selesai
        return KLINE_OK;
    }

    // --- Strategy 2: Honda PGM-FI Motorcycle Direct Frame (72 05 71 00 18) ---
    uint8_t hdsAltReq[] = {0x72, 0x05, 0x71, 0x00, 0x18};
    _flush();

    // [CTS] Cek lagi bus idle
    if (!waitCTSClear()) {
        Logger.log(LOG_WARN, "KLine", "Fast Init Strat2: bus masih busy, lanjut...");
    }

    Logger.logHex(LOG_INFO, "KLine TX (Direct PGM-FI Init)", hdsAltReq, 5);
    _serial->write(hdsAltReq, 5);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsAltReq, 5, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (Direct PGM-FI Response)", resp, respLen);
        enableDTR(false);
        return KLINE_OK;
    }

    // --- Strategy 3: Honda HDS Init with 70ms Wakeup Pulse ---
    if (_serial) _serial->end();
    pinMode(_txPin, OUTPUT);

    // [DTR] Pastikan gerbang aktif selama pulse
    enableDTR(true);
    _driveLine(false); // K-Line LOW
    delay(70);
    _driveLine(true);  // K-Line HIGH (Idle)
    delay(130);

    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // [CTS] Tunggu bus idle setelah pulse
    waitCTSClear(200);

    Logger.logHex(LOG_INFO, "KLine TX (HDS 70ms Wakeup Init)", hdsReq, 4);
    _serial->write(hdsReq, 4);
    _serial->flush();
    if (_echoCancel) _drainEcho(hdsReq, 4, 50);

    respLen = 0;
    r = receiveRaw(resp, respLen, 350);
    if (r == KLINE_OK && respLen >= 2 && validateChecksum(resp, respLen)) {
        Logger.logHex(LOG_INFO, "KLine RX (70ms Wakeup Response)", resp, respLen);
        enableDTR(false);
        return KLINE_OK;
    }

    // --- Strategy 4: ISO 14230 Fast Init (25ms LOW, 25ms HIGH) ---
    _flush();
    if (_serial) _serial->end();
    pinMode(_txPin, OUTPUT);
    enableDTR(true);
    _driveLine(false); delay(25);
    _driveLine(true);  delay(25);
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(15);
    _flush();
    waitCTSClear(100); // Tunggu bus idle setelah ISO pulse

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
        enableDTR(false); // Tutup gerbang setelah selesai
        return KLINE_OK;
    }

    // Semua strategi gagal — nonaktifkan DTR
    enableDTR(false);
    Logger.log(LOG_WARN, "KLine", "Fast Init failed: no valid response from ECU");
    return KLINE_ERR_TIMEOUT;
}

// ---- 5-Baud Init (ISO 9141) ----
KLineResult KLineDriver::_5baudInit() {
    _flush();
    if (_serial) _serial->end();

    // [DTR] Aktifkan gerbang TX sebelum bit-bang
    enableDTR(true);

    pinMode(_txPin, OUTPUT);
    _driveLine(true); // idle HIGH
    delay(300);

    // [CTS] Pastikan bus idle sebelum mulai 5-Baud sequence
    if (_ctsPin >= 0 && isCTSBusy()) {
        Logger.log(LOG_WARN, "KLine", "5-Baud: bus masih busy sebelum bit-bang, tunggu...");
        waitCTSClear(500);
    }

    // Bit-bang 0x33 (alamat broadcast ECU) pada 5 baud = 200ms/bit
    _bitBangByte(0x33, 5);

    // Re-init UART pada 10400 bps dengan konfigurasi inversi
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin, _invert);
    delay(20);
    _flush();

    // Drain TX echo dari bit-bang jika aktif
    uint8_t addressByte[] = {0x33};
    if (_echoCancel) {
        _drainEcho(addressByte, 1, 50);
    }

    // Tunggu sync byte 0x55 dari ECU (hingga 500ms)
    uint32_t t = millis();
    while (millis() - t < 500) {
        if (_serial->available()) {
            uint8_t b = _serial->read();
            // Skip noise 0x00 atau 0xFF sebelum sync byte
            if (b == 0x00 || b == 0xFF) continue;

            Logger.log(LOG_DEBUG, "KLine", "5Baud sync byte: 0x%02X", b);
            if (b == 0x55) {
                delay(10);
                uint8_t kw[3] = {0};
                size_t  kwLen = 0;
                receiveRaw(kw, kwLen, 200);
                if (kwLen >= 2) {
                    uint8_t ackByte = ~kw[1];
                    // [CTS] Cek bus sebelum kirim ACK
                    waitCTSClear(50);
                    sendRaw(&ackByte, 1);
                }
                enableDTR(false); // Tutup gerbang setelah handshake
                return KLINE_OK;
            }
        }
        yield();
    }

    enableDTR(false);
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
        // [CTS] Cek collision di tengah bit-bang: jika CTS busy padahal kita yang harusnya TX,
        // bisa jadi ada glitch atau ECU yang memutus — catat sebagai collision
        if (_ctsPin >= 0 && isCTSBusy()) {
            bool weAreHigh = (_invert ? !((byte >> i) & 0x01) : ((byte >> i) & 0x01));
            if (weAreHigh) {
                // Bus ditarik LOW padahal kita TX HIGH: kemungkinan collision
                _collisionCount++;
                Logger.log(LOG_WARN, "KLine", "Bit-bang collision detected at bit %d (0x%02X)", i, byte);
            }
        }
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

    // [CTS] Cek bus idle sebelum mulai TX — cegah collision
    if (_ctsPin >= 0 && isCTSBusy()) {
        _busBusyCount++;
        Logger.log(LOG_DEBUG, "KLine", "CTS busy: menunggu bus idle sebelum TX... (total busy=%lu)", _busBusyCount);
        if (!waitCTSClear()) {
            // Tetap lanjut jika timeout — hindari deadlock
            Logger.log(LOG_WARN, "KLine", "CTS timeout: bus masih busy, TX dipaksakan");
        }
    }

    // [DTR] Aktifkan gerbang TX
    enableDTR(true);
    delayMicroseconds(KLINE_DTR_SETTLE_US);

    Logger.logHex(LOG_DEBUG, "KLine TX", data, len);
    _serial->write(data, len);
    _serial->flush();

    // [DTR] Tutup gerbang TX setelah selesai kirim
    enableDTR(false);

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

// ============================================================
// DTR / CTS Flow Control Methods
// ============================================================

// ---- enableDTR: buka/tutup gerbang TX optocoupler via DTR ----
// DTR = Data Terminal Ready digunakan sebagai "TX Enable Gate":
//   enableDTR(true)  → aktifkan gate → K-Line TX bisa mengirim
//   enableDTR(false) → matikan gate → K-Line TX terisolasi (idle/aman)
void KLineDriver::enableDTR(bool on) {
    if (_dtrPin < 0) return; // DTR tidak dikonfigurasi
    // Jika _dtrActive = HIGH: on → HIGH, off → LOW
    // Jika _dtrActive = LOW:  on → LOW,  off → HIGH
    bool pinLevel = on ? _dtrActive : !_dtrActive;
    digitalWrite(_dtrPin, pinLevel ? HIGH : LOW);
}

// ---- isCTSBusy: baca apakah K-Line bus sedang sibuk ----
// CTS = Clear To Send digunakan sebagai "Bus Busy Detector":
//   Jika K-Line ditarik ke LOW oleh ECU (sedang TX), pin CTS akan ikut LOW
//   (karena terhubung ke bus melalui pull-up) → kembalikan true (busy)
bool KLineDriver::isCTSBusy() {
    if (_ctsPin < 0) return false; // CTS tidak dikonfigurasi → anggap tidak busy
    int val = digitalRead(_ctsPin);
    return (val == _ctsBusyLevel);
}

// ---- waitCTSClear: tunggu bus idle dengan timeout ----
// Polling CTS sampai bus bebas (tidak busy) atau timeout tercapai.
// Mengembalikan true jika bus berhasil idle sebelum timeout.
bool KLineDriver::waitCTSClear(uint32_t timeoutMs) {
    if (_ctsPin < 0) return true; // CTS tidak dikonfigurasi → langsung return true
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (!isCTSBusy()) return true; // Bus sudah idle
        yield();
        delayMicroseconds(100); // polling tiap 100us
    }
    // Timeout: bus masih busy
    _busBusyCount++;
    Logger.log(LOG_WARN, "KLine", "waitCTSClear timeout %lums (busyCount=%lu)", timeoutMs, _busBusyCount);
    return false;
}

// ---- _waitBusIdle: internal alias untuk waitCTSClear ----
bool KLineDriver::_waitBusIdle(uint32_t timeoutMs) {
    return waitCTSClear(timeoutMs);
}


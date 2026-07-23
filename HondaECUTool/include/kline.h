#pragma once
// ============================================================
// kline.h - Honda K-Line Driver (ISO 9141 / KWP2000 subset)
// Hardware: ESP32 UART2 via 4N25 / 4N35 optocoupler or L9637D
// ============================================================
#include <Arduino.h>
#include <HardwareSerial.h>
#include <vector>
#include "config.h"


// --- Init mode ---
enum KLineInitMode {
    KLINE_FAST_INIT,
    KLINE_5BAUD_INIT,
    KLINE_AUTO_DETECT
};

// --- Result codes ---
enum KLineResult {
    KLINE_OK          = 0,
    KLINE_ERR_TIMEOUT = 1,
    KLINE_ERR_CRC     = 2,
    KLINE_ERR_NOINIT  = 3,
    KLINE_ERR_NOECHO  = 4,
    KLINE_ERR_RETRY   = 5,
    KLINE_ERR_GENERAL = 6,
    KLINE_ERR_BUS_BUSY = 7   // CTS: K-Line bus masih busy saat akan TX
};

// --- Frame structure ---
struct KLineFrame {
    uint8_t             header[3];   // Format, Target, Source
    std::vector<uint8_t> data;
    uint8_t             checksum;
    uint32_t            timestamp_ms;
};

class KLineDriver {
public:
    KLineDriver();

    /**
     * @brief Initialize UART and optocoupler interface
     * @param txPin   TX GPIO pin
     * @param rxPin   RX GPIO pin
     * @param baud    Baud rate (default 10400)
     * @param invert  Invert UART signal polarity for inverting optocoupler circuits (default false)
     * @param dtrPin  DTR GPIO output pin — TX Enable Gate (set -1 to disable)
     * @param ctsPin  CTS GPIO input pin  — Bus Busy Detect (set -1 to disable)
     */
    void begin(uint8_t txPin, uint8_t rxPin, uint32_t baud = 10400,
               bool invert = false, int8_t dtrPin = -1, int8_t ctsPin = -1);

    /**
     * @brief Perform K-Line initialization sequence
     * @param mode  Fast Init / 5-Baud Init / Auto
     * @return KLineResult
     */
    KLineResult init(KLineInitMode mode = KLINE_AUTO_DETECT);

    /**
     * @brief Send raw bytes over K-Line
     */
    KLineResult sendRaw(const uint8_t* data, size_t len);

    /**
     * @brief Receive raw bytes with timeout
     */
    KLineResult receiveRaw(uint8_t* buf, size_t& len, uint32_t timeoutMs = 1000);

    /**
     * @brief Send a framed request and receive framed response
     */
    KLineResult request(const uint8_t* req, size_t reqLen,
                        uint8_t* resp, size_t& respLen,
                        uint32_t timeoutMs = 1000);

    /**
     * @brief Calculate K-Line checksum (sum of all bytes mod 256)
     */
    static uint8_t calcChecksum(const uint8_t* data, size_t len);

    /**
     * @brief Validate checksum of received frame (supports sum mod 256 and Honda 2's complement)
     */
    static bool validateChecksum(const uint8_t* data, size_t len);

    bool     isInitialized() const { return _initialized; }
    void     setRetryCount(uint8_t n) { _retryMax = n; }
    void     setInvert(bool inv) { _invert = inv; }
    bool     getInvert() const { return _invert; }
    void     setEchoCancel(bool enable) { _echoCancel = enable; }
    bool     getEchoCancel() const { return _echoCancel; }

    // --- DTR / CTS Flow Control ---

    /**
     * @brief Aktifkan atau nonaktifkan jalur TX via pin DTR (TX Enable Gate).
     * @param on  true = aktifkan optocoupler TX gate, false = matikan
     */
    void     enableDTR(bool on);

    /**
     * @brief Baca status bus K-Line dari pin CTS.
     * @return true jika bus sedang BUSY (ECU sedang TX / collision terdeteksi)
     */
    bool     isCTSBusy();

    /**
     * @brief Tunggu K-Line bus idle (CTS clear) dengan timeout.
     * @param timeoutMs  Maksimal waktu tunggu dalam ms
     * @return true jika bus idle sebelum timeout, false jika timeout
     */
    bool     waitCTSClear(uint32_t timeoutMs = KLINE_CTS_TIMEOUT_MS);

    /**
     * @brief Set pin DTR secara manual (override dari begin())
     */
    void     setDTRPin(int8_t pin) { _dtrPin = pin; if (pin >= 0) pinMode(pin, OUTPUT); }

    /**
     * @brief Set pin CTS secara manual (override dari begin())
     */
    void     setCTSPin(int8_t pin) { _ctsPin = pin; if (pin >= 0) pinMode(pin, INPUT); }

    bool     hasDTR()  const { return _dtrPin >= 0; }
    bool     hasCTS()  const { return _ctsPin >= 0; }
    uint32_t getBusBusyCount()   const { return _busBusyCount; }
    uint32_t getCollisionCount() const { return _collisionCount; }

    void     end();

private:
    HardwareSerial* _serial;
    uint8_t   _txPin;
    uint8_t   _rxPin;
    uint32_t  _baud;
    bool      _invert;
    bool      _echoCancel;
    bool      _initialized;
    uint8_t   _retryMax;

    // --- DTR / CTS ---
    int8_t    _dtrPin;          // GPIO output: TX Enable Gate (-1 = tidak digunakan)
    int8_t    _ctsPin;          // GPIO input:  Bus Busy Detect (-1 = tidak digunakan)
    bool      _dtrActive;       // Level aktif DTR (KLINE_DTR_ACTIVE)
    uint8_t   _ctsBusyLevel;    // Level CTS saat bus busy (KLINE_CTS_BUSY)
    uint32_t  _busBusyCount;    // Statistik: berapa kali CTS busy terdeteksi
    uint32_t  _collisionCount;  // Statistik: berapa kali TX dibatalkan karena busy

    KLineResult _fastInit();
    KLineResult _5baudInit();
    void        _bitBangByte(uint8_t byte, uint32_t baud);
    void        _drainEcho(const uint8_t* sentData, size_t len, uint32_t timeoutMs = 50);
    void        _driveLine(bool lineHigh);
    void        _flush();
    bool        _waitBusIdle(uint32_t timeoutMs = KLINE_CTS_TIMEOUT_MS); // internal alias
};

// Singleton accessor
extern KLineDriver KLine;

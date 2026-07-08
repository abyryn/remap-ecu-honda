#pragma once
// ============================================================
// ota.h - OTA Firmware Update
// ============================================================
#include <Arduino.h>
#include <Update.h>
#include <functional>

enum OTAState {
    OTA_IDLE = 0,
    OTA_UPLOADING,
    OTA_VERIFYING,
    OTA_DONE,
    OTA_ERROR
};

class OTAManager {
public:
    OTAManager();

    /**
     * @brief Begin an OTA update session
     * @param totalSize  Total firmware size in bytes
     */
    bool begin(size_t totalSize);

    /**
     * @brief Write a chunk of firmware data
     */
    bool write(const uint8_t* data, size_t len);

    /**
     * @brief Finalize update and reboot
     */
    bool end();

    /**
     * @brief Abort an in-progress update
     */
    void abort();

    /**
     * @brief Get current OTA state as JSON
     */
    String statusJson();

    OTAState getState()    const { return _state; }
    int      getProgress() const { return _progress; }
    String   getError()    const { return _error; }

    // Firmware version helpers
    static String currentVersion();
    static String buildDate();

private:
    OTAState _state;
    int      _progress;
    size_t   _totalSize;
    size_t   _written;
    String   _error;
};

extern OTAManager OTA;

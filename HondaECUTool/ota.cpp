// ============================================================
// ota.cpp - OTA Firmware Update
// ============================================================
#include "include/ota.h"
#include "include/config.h"
#include "include/logger.h"
#include <ArduinoJson.h>

OTAManager OTA;

OTAManager::OTAManager()
    : _state(OTA_IDLE), _progress(0), _totalSize(0), _written(0) {}

bool OTAManager::begin(size_t totalSize) {
    if (!Update.begin(totalSize)) {
        _error = Update.errorString();
        _state = OTA_ERROR;
        Logger.log(LOG_ERROR, "OTA", "begin failed: %s", _error.c_str());
        return false;
    }
    _totalSize = totalSize;
    _written   = 0;
    _progress  = 0;
    _state     = OTA_UPLOADING;
    Logger.log(LOG_INFO, "OTA", "Update begin, size=%d", totalSize);
    return true;
}

bool OTAManager::write(const uint8_t* data, size_t len) {
    if (_state != OTA_UPLOADING) return false;

    size_t written = Update.write(data, len);
    if (written != len) {
        _error = Update.errorString();
        _state = OTA_ERROR;
        Logger.log(LOG_ERROR, "OTA", "Write error: %s", _error.c_str());
        return false;
    }
    _written += written;
    if (_totalSize > 0) {
        _progress = (int)(_written * 100 / _totalSize);
    }
    return true;
}

bool OTAManager::end() {
    if (!Update.end(true)) {
        _error = Update.errorString();
        _state = OTA_ERROR;
        Logger.log(LOG_ERROR, "OTA", "end failed: %s", _error.c_str());
        return false;
    }
    _state    = OTA_DONE;
    _progress = 100;
    Logger.log(LOG_INFO, "OTA", "Update complete. Rebooting...");
    delay(1000);
    ESP.restart();
    return true;
}

void OTAManager::abort() {
    Update.abort();
    _state = OTA_IDLE;
    Logger.log(LOG_WARN, "OTA", "Update aborted");
}

String OTAManager::statusJson() {
    StaticJsonDocument<128> doc;
    doc["state"]    = (int)_state;
    doc["progress"] = _progress;
    doc["written"]  = _written;
    doc["total"]    = _totalSize;
    doc["error"]    = _error;
    doc["version"]  = currentVersion();
    String out;
    serializeJson(doc, out);
    return out;
}

String OTAManager::currentVersion() {
    return String(FW_VERSION);
}

String OTAManager::buildDate() {
    return String(FW_BUILD_DATE);
}

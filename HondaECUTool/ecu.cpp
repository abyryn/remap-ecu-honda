// ============================================================
// ecu.cpp - Honda ECU Manager Implementation
// Protocol: Honda Proprietary over K-Line (KWP2000-like)
// ============================================================
#include "include/ecu.h"
#include "include/kline.h"
#include "include/logger.h"
#include <ArduinoJson.h>

ECUManager ECU;

// ============================================================
// Honda K-Line Service IDs
// ============================================================
#define SVC_READ_DATA_LOCAL   0x21
#define SVC_READ_ID           0x1A
#define SVC_READ_DTC          0x03
#define SVC_CLEAR_DTC         0x04
#define SVC_START_SESSION     0x10
#define SVC_STOP_SESSION      0x82
#define SVC_READ_EEPROM       0x23

// Common PIDs (Honda PGM-FI)
#define PID_RPM               0x01
#define PID_TPS               0x02
#define PID_MAP               0x03
#define PID_IAT               0x04
#define PID_ECT               0x05
#define PID_BATT              0x06
#define PID_INJ_PW            0x07
#define PID_IGN_TIMING        0x08
#define PID_SPEED             0x09
#define PID_ENGINE_LOAD       0x0A
#define PID_IDLE_SW           0x0B
#define PID_O2                0x0C
#define PID_FUEL_TRIM         0x0D

// ============================================================
ECUManager::ECUManager()
    : _state(ECU_DISCONNECTED), _model(HONDA_UNKNOWN) {
    memset(&_live, 0, sizeof(_live));
    memset(&_info, 0, sizeof(_info));
}

// ---- connect ----
bool ECUManager::connect() {
    _state = ECU_CONNECTING;
    Logger.log(LOG_INFO, "ECU", "Connecting...");

    KLineResult r = KLine.init(KLINE_AUTO_DETECT);
    if (r != KLINE_OK) {
        _state = ECU_ERROR;
        Logger.log(LOG_ERROR, "ECU", "K-Line init failed: %d", r);
        return false;
    }

    // Start diagnostic session (service 0x10, mode 0x81 = default)
    uint8_t req[]  = {0x02, 0x10, 0x81, 0x00}; // len, SID, mode, chk placeholder
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[32];
    size_t  respLen = 0;
    r = KLine.request(req, 4, resp, respLen, 1000);

    if (r == KLINE_OK && respLen >= 2 && resp[1] == (0x40 | SVC_START_SESSION)) {
        _state = ECU_CONNECTED;
        Logger.log(LOG_INFO, "ECU", "Session started OK");
        return true;
    }

    // Some Honda ECUs respond without start session — try reading ID directly
    if (readIdentification()) {
        _state = ECU_CONNECTED;
        return true;
    }

    _state = ECU_ERROR;
    return false;
}

// ---- disconnect ----
void ECUManager::disconnect() {
    if (_state == ECU_CONNECTED) {
        // Send stop communication
        uint8_t req[] = {0x01, SVC_STOP_SESSION, 0x00};
        req[2] = KLineDriver::calcChecksum(req, 2);
        uint8_t resp[8];
        size_t  len = 0;
        KLine.sendRaw(req, 3);
        KLine.receiveRaw(resp, len, 300);
    }
    KLine.end();
    _state = ECU_DISCONNECTED;
    Logger.log(LOG_INFO, "ECU", "Disconnected");
}

// ---- readIdentification ----
bool ECUManager::readIdentification() {
    uint8_t req[] = {0x02, SVC_READ_ID, 0x01, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[64];
    size_t  len = 0;

    if (!_sendService(SVC_READ_ID, req + 2, 1, resp, len)) {
        Logger.log(LOG_WARN, "ECU", "Read ID failed");
        return false;
    }

    // Parse response (Honda format)
    if (len >= 6) {
        char buf[32];
        // Part number: bytes 3..12
        snprintf(buf, sizeof(buf), "%02X%02X-%02X%02X",
                 resp[3], resp[4], resp[5], resp[6]);
        _info.partNumber = String(buf);
        _info.firmwareVersion = String("1.") + String(resp[7]);
        _info.hardwareVersion = String("HW") + String(resp[8]);
        _info.manufacturer    = "Honda";
        _info.protocol        = "K-Line ISO9141";
        _info.eepromSize      = 256;
        _info.flashSize       = 0;
        _info.checksum        = resp[len - 1];
        Logger.log(LOG_INFO, "ECU", "ID: %s FW: %s",
                   _info.partNumber.c_str(), _info.firmwareVersion.c_str());
        return true;
    }

    return false;
}

// ---- readLiveData ----
bool ECUManager::readLiveData() {
    if (!isConnected()) return false;

    uint8_t resp[16];
    size_t  len = 0;

    auto readPID = [&](uint8_t pid) -> bool {
        return _readDataByLocalId(pid, resp, len);
    };

    // RPM
    if (readPID(PID_RPM) && len >= 4)
        _live.rpm = ((uint16_t)resp[2] << 8 | resp[3]) * 50 / 6; // Honda scale

    // TPS
    if (readPID(PID_TPS) && len >= 3)
        _live.tps = resp[2] * 100.0f / 255.0f;

    // MAP
    if (readPID(PID_MAP) && len >= 3)
        _live.map = resp[2] * 1.0f;  // kPa

    // IAT
    if (readPID(PID_IAT) && len >= 3)
        _live.iat = (float)resp[2] - 40.0f;  // °C offset

    // ECT
    if (readPID(PID_ECT) && len >= 3)
        _live.ect = (float)resp[2] - 40.0f;

    // Battery Voltage
    if (readPID(PID_BATT) && len >= 3)
        _live.battVoltage = resp[2] * 0.0625f;

    // Injector PW
    if (readPID(PID_INJ_PW) && len >= 4)
        _live.injPulseWidth = ((uint16_t)resp[2] << 8 | resp[3]) * 0.001f;

    // Ignition Timing
    if (readPID(PID_IGN_TIMING) && len >= 3)
        _live.ignTiming = (float)resp[2] - 64.0f;

    // Vehicle Speed
    if (readPID(PID_SPEED) && len >= 3)
        _live.vehicleSpeed = resp[2];

    // Engine Load
    if (readPID(PID_ENGINE_LOAD) && len >= 3)
        _live.engineLoad = resp[2] * 100.0f / 255.0f;

    // Idle Switch
    if (readPID(PID_IDLE_SW) && len >= 3)
        _live.idleSwitch = (resp[2] & 0x01) != 0;

    // O2 Sensor
    if (readPID(PID_O2) && len >= 3)
        _live.o2Sensor = resp[2] * 4.882f;  // mV

    // Fuel Trim
    if (readPID(PID_FUEL_TRIM) && len >= 3)
        _live.fuelTrim = ((float)resp[2] - 128.0f) * 100.0f / 128.0f;

    _live.closedLoop  = (_live.o2Sensor > 100.0f && _live.o2Sensor < 900.0f);
    _live.timestamp   = millis();

    return true;
}

// ---- readDTC ----
bool ECUManager::readDTC() {
    if (!isConnected()) return false;
    _dtcs.clear();

    uint8_t req[] = {0x01, SVC_READ_DTC, 0x00};
    req[2] = KLineDriver::calcChecksum(req, 2);

    uint8_t resp[64];
    size_t  len = 0;

    if (!_sendService(SVC_READ_DTC, nullptr, 0, resp, len)) return false;

    // Parse DTC pairs
    for (size_t i = 2; i + 1 < len - 1; i += 2) {
        DTCEntry dtc;
        dtc.code        = ((uint16_t)resp[i] << 8) | resp[i + 1];
        dtc.description = _dtcDescription(dtc.code);
        dtc.pending     = false;
        dtc.milOn       = (resp[i] & 0x80) != 0;
        if (dtc.code != 0x0000) {
            _dtcs.push_back(dtc);
            Logger.log(LOG_INFO, "ECU", "DTC: P%04X - %s",
                       dtc.code, dtc.description.c_str());
        }
    }

    Logger.log(LOG_INFO, "ECU", "Found %d DTC(s)", _dtcs.size());
    return true;
}

// ---- clearDTC ----
bool ECUManager::clearDTC() {
    if (!isConnected()) return false;

    uint8_t req[] = {0x01, SVC_CLEAR_DTC, 0x00};
    req[2] = KLineDriver::calcChecksum(req, 2);

    uint8_t resp[8];
    size_t  len = 0;

    bool ok = _sendService(SVC_CLEAR_DTC, nullptr, 0, resp, len);
    if (ok) {
        _dtcs.clear();
        Logger.log(LOG_INFO, "ECU", "DTC cleared");
    }
    return ok;
}

// ---- readEEPROM ----
bool ECUManager::readEEPROM(uint8_t* buf, size_t size,
                              std::function<void(int)> progress) {
    if (!isConnected()) return false;

    const size_t CHUNK = 16;
    size_t offset = 0;

    while (offset < size) {
        size_t toRead = min(CHUNK, size - offset);

        uint8_t req[6];
        req[0] = 0x04;
        req[1] = SVC_READ_EEPROM;
        req[2] = (uint8_t)(offset >> 8);
        req[3] = (uint8_t)(offset & 0xFF);
        req[4] = (uint8_t)toRead;
        req[5] = KLineDriver::calcChecksum(req, 5);

        uint8_t resp[32];
        size_t  respLen = 0;
        KLineResult r = KLine.request(req, 6, resp, respLen, 2000);

        if (r != KLINE_OK || respLen < toRead + 3) {
            Logger.log(LOG_ERROR, "ECU", "EEPROM read fail at offset %d", offset);
            return false;
        }

        memcpy(buf + offset, resp + 2, toRead);
        offset += toRead;

        if (progress) {
            progress((int)(offset * 100 / size));
        }

        delay(10);
    }

    Logger.log(LOG_INFO, "ECU", "EEPROM read complete (%d bytes)", size);
    return true;
}

// ---- liveDataToJson ----
String ECUManager::liveDataToJson() {
    StaticJsonDocument<512> doc;
    doc["rpm"]           = _live.rpm;
    doc["tps"]           = serialized(String(_live.tps, 1));
    doc["map"]           = serialized(String(_live.map, 1));
    doc["iat"]           = serialized(String(_live.iat, 1));
    doc["ect"]           = serialized(String(_live.ect, 1));
    doc["battVoltage"]   = serialized(String(_live.battVoltage, 2));
    doc["injPW"]         = serialized(String(_live.injPulseWidth, 3));
    doc["ignTiming"]     = serialized(String(_live.ignTiming, 1));
    doc["speed"]         = _live.vehicleSpeed;
    doc["engineLoad"]    = serialized(String(_live.engineLoad, 1));
    doc["idleSwitch"]    = _live.idleSwitch;
    doc["o2"]            = serialized(String(_live.o2Sensor, 1));
    doc["closedLoop"]    = _live.closedLoop;
    doc["fuelTrim"]      = serialized(String(_live.fuelTrim, 1));
    doc["ts"]            = _live.timestamp;
    String out;
    serializeJson(doc, out);
    return out;
}

// ---- ecuInfoToJson ----
String ECUManager::ecuInfoToJson() {
    StaticJsonDocument<256> doc;
    doc["manufacturer"]  = _info.manufacturer;
    doc["partNumber"]    = _info.partNumber;
    doc["fwVersion"]     = _info.firmwareVersion;
    doc["hwVersion"]     = _info.hardwareVersion;
    doc["protocol"]      = _info.protocol;
    doc["eepromSize"]    = _info.eepromSize;
    doc["flashSize"]     = _info.flashSize;
    doc["vin"]           = _info.vin;
    doc["calibrationId"] = _info.calibrationId;
    doc["checksum"]      = _info.checksum;
    doc["model"]         = modelName(_model);
    String out;
    serializeJson(doc, out);
    return out;
}

// ---- dtcToJson ----
String ECUManager::dtcToJson() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("dtcs");
    for (auto& d : _dtcs) {
        JsonObject o = arr.createNestedObject();
        char code[8];
        snprintf(code, sizeof(code), "P%04X", d.code);
        o["code"]        = code;
        o["description"] = d.description;
        o["pending"]     = d.pending;
        o["milOn"]       = d.milOn;
    }
    doc["count"] = _dtcs.size();
    String out;
    serializeJson(doc, out);
    return out;
}

// ---- setModel ----
void ECUManager::setModel(HondaModel model) {
    _model = model;
    Logger.log(LOG_INFO, "ECU", "Model set: %s", modelName(model).c_str());
}

// ---- modelName ----
String ECUManager::modelName(HondaModel model) {
    switch (model) {
        case HONDA_BEAT:    return "Honda Beat";
        case HONDA_SCOOPY:  return "Honda Scoopy";
        case HONDA_GENIO:   return "Honda Genio";
        case HONDA_VARIO:   return "Honda Vario";
        case HONDA_PCX:     return "Honda PCX";
        case HONDA_ADV:     return "Honda ADV";
        case HONDA_SUPRA:   return "Honda Supra";
        case HONDA_SONIC:   return "Honda Sonic";
        case HONDA_VERZA:   return "Honda Verza";
        case HONDA_CB150R:  return "Honda CB150R";
        case HONDA_CBR150R: return "Honda CBR150R";
        case HONDA_CRF150L: return "Honda CRF150L";
        case HONDA_STYLO:   return "Honda Stylo";
        case HONDA_EM1:     return "Honda EM1";
        case HONDA_MEGAPRO: return "Honda MegaPro FI";
        case HONDA_CBR250RR:return "Honda CBR250RR";
        case HONDA_REVO:    return "Honda Revo";
        default:            return "Unknown";
    }
}

// ---- _sendService ----
bool ECUManager::_sendService(uint8_t service, const uint8_t* params, size_t pLen,
                               uint8_t* resp, size_t& respLen) {
    uint8_t req[32];
    size_t  reqLen = 0;

    req[reqLen++] = (uint8_t)(pLen + 1);  // length byte
    req[reqLen++] = service;
    if (params && pLen > 0) {
        memcpy(req + reqLen, params, pLen);
        reqLen += pLen;
    }
    req[reqLen] = KLineDriver::calcChecksum(req, reqLen);
    reqLen++;

    KLineResult r = KLine.request(req, reqLen, resp, respLen, KLINE_TIMEOUT_MS);
    return (r == KLINE_OK && respLen >= 2);
}

// ---- _readDataByLocalId ----
bool ECUManager::_readDataByLocalId(uint8_t pid, uint8_t* resp, size_t& len) {
    uint8_t params[] = {pid};
    return _sendService(SVC_READ_DATA_LOCAL, params, 1, resp, len);
}

// ---- _dtcDescription ----
String ECUManager::_dtcDescription(uint16_t code) {
    switch (code) {
        case 0x0101: return "O2 Sensor Heater";
        case 0x0105: return "MAP Sensor";
        case 0x0107: return "MAP Sensor Low";
        case 0x0108: return "MAP Sensor High";
        case 0x0111: return "TPS Sensor";
        case 0x0112: return "TPS Low";
        case 0x0113: return "TPS High";
        case 0x0116: return "ECT Sensor";
        case 0x0117: return "ECT Low";
        case 0x0118: return "ECT High";
        case 0x011B: return "IAT Sensor";
        case 0x011C: return "IAT Low";
        case 0x011D: return "IAT High";
        case 0x0122: return "MAP Sensor Range";
        case 0x0131: return "Fuel System Too Lean";
        case 0x0132: return "Fuel System Too Rich";
        case 0x0201: return "Injector Circuit";
        case 0x0301: return "Cylinder 1 Misfire";
        case 0x0351: return "Ignition Coil";
        case 0x0500: return "Vehicle Speed Sensor";
        case 0x1102: return "IACV Hose Off";
        case 0x1706: return "ECT Sensor Range";
        default: {
            char buf[20];
            snprintf(buf, sizeof(buf), "Unknown DTC %04X", code);
            return String(buf);
        }
    }
}

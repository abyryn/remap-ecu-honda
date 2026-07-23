// ============================================================
// logger.cpp - Session Logger Implementation (Serial + Bluetooth)
// ============================================================
#include "include/logger.h"
#include "include/filesystem.h"
#include "include/kline.h"
#include "include/ecu.h"
#include <ArduinoJson.h>
#include <stdarg.h>

#if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
BluetoothSerial SerialBT;
#endif

LoggerClass Logger;

LoggerClass::LoggerClass()
    : _minLevel(LOG_DEBUG), _fileLogging(false), _btEnabled(false) {}

void LoggerClass::begin(LogLevel level, bool enableBT, const char* btDeviceName) {
    _minLevel = level;
    Serial.begin(115200);
    Serial.println("[Logger] Serial started");

    #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
    if (enableBT) {
        if (SerialBT.begin(btDeviceName)) {
            _btEnabled = true;
            Serial.printf("[Logger] Bluetooth Serial started: %s\n", btDeviceName);
        } else {
            Serial.println("[Logger] Bluetooth Serial failed to start");
        }
    }
    #endif
}

bool LoggerClass::isBTConnected() const {
    #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
    return _btEnabled && SerialBT.hasClient();
    #else
    return false;
    #endif
}

void LoggerClass::log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (level < _minLevel) return;

    char msgBuf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    uint32_t ts = millis();

    // 1. USB Serial output
    Serial.printf("[%7lu][%s][%s] %s\n", ts, _levelStr(level), tag, msgBuf);

    // 2. Bluetooth Serial output (to HP)
    #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
    if (_btEnabled && SerialBT.hasClient()) {
        SerialBT.printf("[%7lu][%s][%s] %s\r\n", ts, _levelStr(level), tag, msgBuf);
    }
    #endif

    // 3. Store in ring buffer
    if (_buffer.size() >= MAX_BUFFER) _buffer.erase(_buffer.begin());
    LogEntry entry;
    entry.timestamp = ts;
    entry.level     = level;
    entry.tag       = String(tag);
    entry.message   = String(msgBuf);
    _buffer.push_back(entry);

    // 4. Write to file if active
    if (_fileLogging) {
        String line = String(ts) + "," + _levelStr(level) + "," +
                      tag + "," + msgBuf;
        _writeToFile(line);
    }
}

void LoggerClass::logHex(LogLevel level, const char* tag,
                          const uint8_t* data, size_t len) {
    if (level < _minLevel) return;
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", data[i]);
        hex += b;
    }
    log(level, tag, "%s", hex.c_str());
}

void LoggerClass::update() {
    #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
    if (_btEnabled && SerialBT.hasClient() && SerialBT.available()) {
        String input = SerialBT.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            _handleBTCommand(input);
        }
    }
    #endif
}

void LoggerClass::_handleBTCommand(const String& cmd) {
    log(LOG_INFO, "BT_CMD", "Received: %s", cmd.c_str());

    String lowerCmd = cmd;
    lowerCmd.toLowerCase();

    if (lowerCmd == "help" || lowerCmd == "?") {
        #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
        SerialBT.println("\r\n=== Available Bluetooth Commands ===");
        SerialBT.println("  connect    : Connect to Honda ECU");
        SerialBT.println("  disconnect : Disconnect from ECU");
        SerialBT.println("  dtc        : Read Diagnostic Trouble Codes");
        SerialBT.println("  cleardtc   : Clear DTCs");
        SerialBT.println("  live       : Read Realtime Sensor Data");
        SerialBT.println("  invert     : Toggle K-Line Invert logic");
        SerialBT.println("  hex XX XX  : Send raw hex bytes over K-Line");
        SerialBT.println("=====================================\r\n");
        #endif
    } else if (lowerCmd == "connect") {
        ECU.connect();
    } else if (lowerCmd == "disconnect") {
        ECU.disconnect();
    } else if (lowerCmd == "dtc") {
        ECU.readDTC();
    } else if (lowerCmd == "cleardtc") {
        ECU.clearDTC();
    } else if (lowerCmd == "live") {
        ECU.readLiveData();
        #if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
        SerialBT.println(ECU.liveDataToJson());
        #endif
    } else if (lowerCmd == "invert") {
        bool inv = !KLine.getInvert();
        KLine.setInvert(inv);
        log(LOG_INFO, "BT_CMD", "KLine Invert set to: %s", inv ? "true" : "false");
    } else if (lowerCmd.startsWith("hex ")) {
        String hexStr = cmd.substring(4);
        hexStr.trim();
        std::vector<uint8_t> bytes;
        int idx = 0;
        while (idx < hexStr.length()) {
            while (idx < hexStr.length() && hexStr[idx] == ' ') idx++;
            if (idx >= hexStr.length()) break;
            String byteStr = hexStr.substring(idx, idx + 2);
            bytes.push_back((uint8_t)strtol(byteStr.c_str(), NULL, 16));
            idx += 2;
        }
        if (!bytes.empty()) {
            KLine.sendRaw(bytes.data(), bytes.size());
        }
    }
}

bool LoggerClass::startFileLog(const String& path) {
    _logPath    = path;
    _fileLogging = true;
    String header = "timestamp,level,tag,message";
    FS_Mgr.appendLine(path, header);
    log(LOG_INFO, "Logger", "File log started: %s", path.c_str());
    return true;
}

void LoggerClass::stopFileLog() {
    _fileLogging = false;
    log(LOG_INFO, "Logger", "File log stopped");
}

String LoggerClass::getLogsJson(size_t count) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("logs");

    size_t start = (_buffer.size() > count) ? _buffer.size() - count : 0;
    for (size_t i = start; i < _buffer.size(); i++) {
        JsonObject o = arr.createNestedObject();
        o["ts"]      = _buffer[i].timestamp;
        o["level"]   = _levelStr(_buffer[i].level);
        o["tag"]     = _buffer[i].tag;
        o["msg"]     = _buffer[i].message;
    }
    doc["count"] = arr.size();
    String out;
    serializeJson(doc, out);
    return out;
}

String LoggerClass::exportCSV() {
    String csv = "timestamp,level,tag,message\n";
    for (auto& e : _buffer) {
        csv += String(e.timestamp) + "," +
               _levelStr(e.level)  + "," +
               e.tag + "," +
               e.message + "\n";
    }
    return csv;
}

void LoggerClass::clearBuffer() {
    _buffer.clear();
}

const char* LoggerClass::_levelStr(LogLevel l) {
    switch (l) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "UNK  ";
    }
}

void LoggerClass::_writeToFile(const String& line) {
    if (_fileLogging && _logPath.length() > 0) {
        FS_Mgr.appendLine(_logPath, line);
    }
}

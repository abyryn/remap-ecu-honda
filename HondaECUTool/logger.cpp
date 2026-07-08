// ============================================================
// logger.cpp - Session Logger Implementation
// ============================================================
#include "include/logger.h"
#include "include/filesystem.h"
#include <ArduinoJson.h>
#include <stdarg.h>

LoggerClass Logger;

LoggerClass::LoggerClass()
    : _minLevel(LOG_DEBUG), _fileLogging(false) {}

void LoggerClass::begin(LogLevel level) {
    _minLevel = level;
    Serial.begin(115200);
    Serial.println("[Logger] Started");
}

void LoggerClass::log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (level < _minLevel) return;

    char msgBuf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    uint32_t ts = millis();
    // Serial output
    Serial.printf("[%7lu][%s][%s] %s\n", ts, _levelStr(level), tag, msgBuf);

    // Store in ring buffer
    if (_buffer.size() >= MAX_BUFFER) _buffer.erase(_buffer.begin());
    LogEntry entry;
    entry.timestamp = ts;
    entry.level     = level;
    entry.tag       = String(tag);
    entry.message   = String(msgBuf);
    _buffer.push_back(entry);

    // Write to file if active
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

bool LoggerClass::startFileLog(const String& path) {
    _logPath    = path;
    _fileLogging = true;
    String header = "timestamp,level,tag,message";
    FS_Mgr.appendLine(path, header);
    Logger.log(LOG_INFO, "Logger", "File log started: %s", path.c_str());
    return true;
}

void LoggerClass::stopFileLog() {
    _fileLogging = false;
    Logger.log(LOG_INFO, "Logger", "File log stopped");
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
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        default:        return "UNK";
    }
}

void LoggerClass::_writeToFile(const String& line) {
    FS_Mgr.appendLine(_logPath, line);
}

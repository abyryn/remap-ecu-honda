#pragma once
// ============================================================
// logger.h - Session Logger (Serial + Bluetooth + LittleFS)
// ============================================================
#include <Arduino.h>
#include <vector>
#include "config.h"

#ifndef ENABLE_BLUETOOTH
#define ENABLE_BLUETOOTH 0
#endif

#if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
#include "BluetoothSerial.h"
#endif

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

struct LogEntry {
    uint32_t    timestamp;
    LogLevel    level;
    String      tag;
    String      message;
};

class LoggerClass {
public:
    LoggerClass();

    /**
     * @brief Initialize logger (Serial + Bluetooth + optional file)
     * @param level  Minimum log level to capture
     * @param enableBT Enable Bluetooth Serial Monitor
     * @param btDeviceName Bluetooth Device Name
     */
    void begin(LogLevel level = LOG_DEBUG, bool enableBT = true, const char* btDeviceName = "Honda ECU Tool BT");

    /**
     * @brief Process incoming Bluetooth Serial commands
     */
    void update();

    /**
     * @brief Log a formatted message
     */
    void log(LogLevel level, const char* tag, const char* fmt, ...);

    /**
     * @brief Log a HEX buffer
     */
    void logHex(LogLevel level, const char* tag, const uint8_t* data, size_t len);

    /**
     * @brief Start logging to file
     */
    bool startFileLog(const String& path);

    /**
     * @brief Stop file logging
     */
    void stopFileLog();

    /**
     * @brief Get last N log entries as JSON string
     */
    String getLogsJson(size_t count = 50);

    /**
     * @brief Export log buffer to CSV string
     */
    String exportCSV();

    /**
     * @brief Clear in-memory log buffer
     */
    void clearBuffer();

    bool isFileLogging() const { return _fileLogging; }
    bool isBTConnected() const;

private:
    LogLevel _minLevel;
    bool     _fileLogging;
    bool     _btEnabled;
    String   _logPath;
    std::vector<LogEntry> _buffer;
    static const size_t MAX_BUFFER = 200;

    const char* _levelStr(LogLevel l);
    void        _writeToFile(const String& line);
    void        _handleBTCommand(const String& cmd);
};

#if ENABLE_BLUETOOTH && CONFIG_BT_ENABLED
extern BluetoothSerial SerialBT;
#endif

extern LoggerClass Logger;

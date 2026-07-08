#pragma once
// ============================================================
// logger.h - Session Logger (Serial + LittleFS)
// ============================================================
#include <Arduino.h>
#include <vector>

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
     * @brief Initialize logger (Serial + optional file)
     * @param level  Minimum log level to capture
     */
    void begin(LogLevel level = LOG_DEBUG);

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

private:
    LogLevel _minLevel;
    bool     _fileLogging;
    String   _logPath;
    std::vector<LogEntry> _buffer;
    static const size_t MAX_BUFFER = 200;

    const char* _levelStr(LogLevel l);
    void        _writeToFile(const String& line);
};

extern LoggerClass Logger;

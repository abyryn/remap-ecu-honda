#pragma once
// ============================================================
// settings.h - Persistent Settings (JSON via LittleFS)
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

struct AppSettings {
    // WiFi
    String  wifiSSID;
    String  wifiPassword;
    // K-Line
    uint32_t uartBaud;
    bool     autoReconnect;
    uint32_t timeoutMs;
    // UI
    bool    darkMode;
    String  language;
    // Auth
    String  authUsername;
    String  authPassword;
};

class SettingsManager {
public:
    SettingsManager();

    /**
     * @brief Load settings from LittleFS (falls back to defaults)
     */
    bool load();

    /**
     * @brief Save current settings to LittleFS
     */
    bool save();

    /**
     * @brief Reset all settings to factory defaults
     */
    void reset();

    /**
     * @brief Update settings from JSON string (partial update ok)
     * @return true if any field changed
     */
    bool updateFromJson(const String& json);

    /**
     * @brief Serialize current settings to JSON string
     */
    String toJson();

    AppSettings& get() { return _cfg; }

private:
    AppSettings _cfg;
    void        _applyDefaults();
};

extern SettingsManager Settings;

// ============================================================
// settings.cpp - Persistent Settings
// ============================================================
#include "include/settings.h"
#include "include/filesystem.h"
#include "include/config.h"
#include <ArduinoJson.h>

SettingsManager Settings;

SettingsManager::SettingsManager() {
    _applyDefaults();
}

void SettingsManager::_applyDefaults() {
    _cfg.wifiSSID      = WIFI_SSID;
    _cfg.wifiPassword  = WIFI_PASSWORD;
    _cfg.uartBaud      = KLINE_BAUD;
    _cfg.autoReconnect = true;
    _cfg.timeoutMs     = KLINE_TIMEOUT_MS;
    _cfg.invertKLine   = true;    // Default true for 4N35 optocoupler
    _cfg.echoCancel    = true;    // Default true for single-wire K-Line
    _cfg.darkMode      = true;
    _cfg.language      = "id";
    _cfg.authUsername  = AUTH_USERNAME;
    _cfg.authPassword  = AUTH_PASSWORD;
}

bool SettingsManager::load() {
    if (!FS_Mgr.exists(CONFIG_FILE)) {
        Serial.println("[Settings] No config file, using defaults");
        return false;
    }

    String json = FS_Mgr.readText(CONFIG_FILE);
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Settings] Parse error: %s\n", err.c_str());
        return false;
    }

    if (doc.containsKey("wifiSSID"))      _cfg.wifiSSID      = doc["wifiSSID"].as<String>();
    if (doc.containsKey("wifiPassword"))  _cfg.wifiPassword  = doc["wifiPassword"].as<String>();
    if (doc.containsKey("uartBaud"))      _cfg.uartBaud      = doc["uartBaud"].as<uint32_t>();
    if (doc.containsKey("autoReconnect")) _cfg.autoReconnect = doc["autoReconnect"].as<bool>();
    if (doc.containsKey("timeoutMs"))     _cfg.timeoutMs     = doc["timeoutMs"].as<uint32_t>();
    if (doc.containsKey("invertKLine"))   _cfg.invertKLine   = doc["invertKLine"].as<bool>();
    if (doc.containsKey("echoCancel"))    _cfg.echoCancel    = doc["echoCancel"].as<bool>();
    if (doc.containsKey("darkMode"))      _cfg.darkMode      = doc["darkMode"].as<bool>();
    if (doc.containsKey("language"))      _cfg.language      = doc["language"].as<String>();
    if (doc.containsKey("authUsername"))  _cfg.authUsername  = doc["authUsername"].as<String>();
    if (doc.containsKey("authPassword"))  _cfg.authPassword  = doc["authPassword"].as<String>();

    Serial.println("[Settings] Loaded from file");
    return true;
}

bool SettingsManager::save() {
    StaticJsonDocument<512> doc;
    doc["wifiSSID"]      = _cfg.wifiSSID;
    doc["wifiPassword"]  = _cfg.wifiPassword;
    doc["uartBaud"]      = _cfg.uartBaud;
    doc["autoReconnect"] = _cfg.autoReconnect;
    doc["timeoutMs"]     = _cfg.timeoutMs;
    doc["invertKLine"]   = _cfg.invertKLine;
    doc["echoCancel"]    = _cfg.echoCancel;
    doc["darkMode"]      = _cfg.darkMode;
    doc["language"]      = _cfg.language;
    doc["authUsername"]  = _cfg.authUsername;
    doc["authPassword"]  = _cfg.authPassword;

    String json;
    serializeJson(doc, json);
    bool ok = FS_Mgr.writeText(CONFIG_FILE, json);
    Serial.printf("[Settings] Save %s\n", ok ? "OK" : "FAIL");
    return ok;
}

void SettingsManager::reset() {
    _applyDefaults();
    save();
    Serial.println("[Settings] Reset to defaults");
}

bool SettingsManager::updateFromJson(const String& json) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    bool changed = false;
    auto upd = [&](const char* key, String& field) {
        if (doc.containsKey(key)) { field = doc[key].as<String>(); changed = true; }
    };
    upd("wifiSSID",     _cfg.wifiSSID);
    upd("wifiPassword", _cfg.wifiPassword);
    upd("language",     _cfg.language);
    upd("authUsername", _cfg.authUsername);
    upd("authPassword", _cfg.authPassword);

    if (doc.containsKey("uartBaud"))      { _cfg.uartBaud      = doc["uartBaud"];      changed = true; }
    if (doc.containsKey("autoReconnect")) { _cfg.autoReconnect = doc["autoReconnect"]; changed = true; }
    if (doc.containsKey("timeoutMs"))     { _cfg.timeoutMs     = doc["timeoutMs"];     changed = true; }
    if (doc.containsKey("invertKLine"))   { _cfg.invertKLine   = doc["invertKLine"];   changed = true; }
    if (doc.containsKey("echoCancel"))    { _cfg.echoCancel    = doc["echoCancel"];    changed = true; }
    if (doc.containsKey("darkMode"))      { _cfg.darkMode      = doc["darkMode"];      changed = true; }

    if (changed) save();
    return changed;
}

String SettingsManager::toJson() {
    StaticJsonDocument<512> doc;
    doc["wifiSSID"]      = _cfg.wifiSSID;
    doc["wifiPassword"]  = "********";   // Never expose password
    doc["uartBaud"]      = _cfg.uartBaud;
    doc["autoReconnect"] = _cfg.autoReconnect;
    doc["timeoutMs"]     = _cfg.timeoutMs;
    doc["invertKLine"]   = _cfg.invertKLine;
    doc["echoCancel"]    = _cfg.echoCancel;
    doc["darkMode"]      = _cfg.darkMode;
    doc["language"]      = _cfg.language;
    doc["authUsername"]  = _cfg.authUsername;
    String out;
    serializeJson(doc, out);
    return out;
}

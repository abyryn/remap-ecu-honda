// ============================================================
// wifi.cpp - WiFi Access Point Manager
// ============================================================
#include "include/wifi.h"
#include "include/config.h"
#include "include/logger.h"
#include <ArduinoJson.h>

WiFiManager WiFiAP;

WiFiManager::WiFiManager() : _started(false) {}

bool WiFiManager::startAP(const char* ssid, const char* password, uint8_t channel) {
    WiFi.mode(WIFI_AP);

    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_ip, gateway, subnet);

    bool ok = WiFi.softAP(ssid, password, channel, 0, WIFI_MAX_CLIENTS);
    if (ok) {
        _ssid    = String(ssid);
        _ip      = WiFi.softAPIP().toString();
        _started = true;
        Logger.log(LOG_INFO, "WiFi", "AP started: SSID=%s IP=%s", ssid, _ip.c_str());
    } else {
        Logger.log(LOG_ERROR, "WiFi", "AP start failed");
    }
    return ok;
}

bool WiFiManager::startMDNS(const char* hostname) {
    if (!MDNS.begin(hostname)) {
        Logger.log(LOG_WARN, "WiFi", "mDNS failed");
        return false;
    }
    MDNS.addService("http", "tcp", HTTP_PORT);
    Logger.log(LOG_INFO, "WiFi", "mDNS: http://%s.local", hostname);
    return true;
}

uint8_t WiFiManager::clientCount() {
    return WiFi.softAPgetStationNum();
}

String WiFiManager::getIP() {
    return _ip;
}

String WiFiManager::statusJson() {
    StaticJsonDocument<128> doc;
    doc["ssid"]    = _ssid;
    doc["ip"]      = _ip;
    doc["clients"] = clientCount();
    doc["started"] = _started;
    String out;
    serializeJson(doc, out);
    return out;
}

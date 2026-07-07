#pragma once
// ============================================================
// wifi.h - WiFi Access Point Manager
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

class WiFiManager {
public:
    WiFiManager();

    /**
     * @brief Start WiFi Access Point
     * @param ssid     AP SSID
     * @param password AP Password
     * @param channel  WiFi channel (1-13)
     */
    bool startAP(const char* ssid, const char* password, uint8_t channel = 1);

    /**
     * @brief Start mDNS responder (honda-ecu.local)
     */
    bool startMDNS(const char* hostname = "honda-ecu");

    /**
     * @brief Get number of connected clients
     */
    uint8_t clientCount();

    /**
     * @brief Get current AP IP address string
     */
    String getIP();

    /**
     * @brief Get WiFi signal / AP info JSON
     */
    String statusJson();

    bool isStarted() const { return _started; }

private:
    bool    _started;
    String  _ssid;
    String  _ip;
};

extern WiFiManager WiFiAP;

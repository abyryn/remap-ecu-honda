// ============================================================
// main.cpp - Honda ECU K-Line Diagnostic Tool
// Target: ESP32 DOIT V1
// Framework: Arduino (ESP32 Arduino Core)
// ============================================================
#include <Arduino.h>
#include "include/config.h"
#include "include/logger.h"
#include "include/filesystem.h"
#include "include/settings.h"
#include "include/wifi.h"
#include "include/kline.h"
#include "include/ecu.h"
#include "include/webserver.h"
#include "include/backup.h"
#include "include/ota.h"
#include "include/utils.h"

// --- Watchdog ---
#include <esp_task_wdt.h>
#define WDT_TIMEOUT_SEC 30

// --- LED blink state ---
static uint32_t _ledTimer  = 0;
static bool     _ledState  = false;

// ============================================================
// LED blink pattern
//  - No ECU : fast blink 200ms
//  - ECU OK : slow blink 1000ms
//  - Error  : double-blink
// ============================================================
static void updateLED() {
    uint32_t interval = ECU.isConnected() ? 1000 : 200;
    if (millis() - _ledTimer >= interval) {
        _ledTimer = millis();
        _ledState = !_ledState;
        digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
    }
}

// ============================================================
void setup() {
    // --- Serial (debug) ---
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n=== Honda ECU Tool v" FW_VERSION " ===");

    // --- LED ---
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // --- Watchdog ---
    // Completely disable TWDT because LittleFS format locks the flash cache and starves both CPU cores.
#include <esp_task_wdt.h>
    esp_task_wdt_deinit();

    // --- Logger ---
    Logger.begin(LOG_DEBUG);
    Logger.log(LOG_INFO, "Main", "Booting...");

    // --- LittleFS ---
    if (!FS_Mgr.begin()) {
        Logger.log(LOG_ERROR, "Main", "FS init failed — halting");
        while (true) { delay(1000); }
    }

    // --- Settings ---
    Settings.load();

    // --- WiFi AP ---
    bool wifiOK = WiFiAP.startAP(
        Settings.get().wifiSSID.c_str(),
        Settings.get().wifiPassword.c_str()
    );
    if (!wifiOK) {
        Logger.log(LOG_WARN, "Main", "WiFi AP fallback to default");
        WiFiAP.startAP(WIFI_SSID, WIFI_PASSWORD);
    }
    WiFiAP.startMDNS("honda-ecu");

    // --- K-Line UART ---
    KLine.begin(KLINE_TX_PIN, KLINE_RX_PIN, Settings.get().uartBaud);

    // --- Web Server + WebSocket ---
    WebSrv.begin();

    // --- ADC setup for voltage monitor ---
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
    pinMode(VBAT_PIN, INPUT);

    Logger.log(LOG_INFO, "Main", "Boot complete. IP: %s", WiFiAP.getIP().c_str());
    Logger.log(LOG_INFO, "Main", "Free heap: %d bytes", ESP.getFreeHeap());
    Logger.log(LOG_INFO, "Main", "SSID: %s  Pass: %s",
               Settings.get().wifiSSID.c_str(),
               Settings.get().wifiPassword.c_str());

    // --- Auto-connect to ECU if enabled ---
    if (Settings.get().autoReconnect) {
        Logger.log(LOG_INFO, "Main", "Auto-connect ECU...");
        if (ECU.connect()) {
            ECU.readIdentification();
        }
    }

    digitalWrite(LED_PIN, LOW);
}

// ============================================================
void loop() {
    // Bluetooth Serial command processor & monitor update
    Logger.update();

    // LED status indicator
    updateLED();

    // WebSocket live data broadcast + client cleanup
    WebSrv.loop();

    // Yield to system tasks (prevents watchdog on heavy loops)
    yield();
    delay(10);
}

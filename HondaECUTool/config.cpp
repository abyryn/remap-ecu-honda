// ============================================================
// config.cpp - Runtime config load/save via LittleFS JSON
// ============================================================
#include "include/config.h"
#include "include/settings.h"
#include <Arduino.h>

// Runtime defaults — overridden by settings.json on boot
const char* DEFAULT_SSID     = WIFI_SSID;
const char* DEFAULT_PASSWORD = WIFI_PASSWORD;
const int   DEFAULT_BAUD     = KLINE_BAUD;

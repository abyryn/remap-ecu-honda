// ============================================================
// utils.cpp - Utility Functions
// ============================================================
#include "include/utils.h"
#include <ArduinoJson.h>
#include <esp_system.h>

namespace Utils {

float readVoltage(uint8_t pin, float dividerRatio, float vref, int adcMax) {
    int raw    = analogRead(pin);
    float volt = ((float)raw / adcMax) * vref * dividerRatio;
    return volt;
}

String toHexString(const uint8_t* data, size_t len) {
    String out = "";
    out.reserve(len * 3);
    for (size_t i = 0; i < len; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", data[i]);
        out += b;
    }
    out.trim();
    return out;
}

size_t fromHexString(const String& hex, uint8_t* buf, size_t maxLen) {
    String s = hex;
    s.replace(" ", "");
    s.replace("0x", "");
    s.replace("0X", "");
    size_t count = 0;
    for (size_t i = 0; i + 1 < s.length() && count < maxLen; i += 2) {
        buf[count++] = (uint8_t)strtol(s.substring(i, i + 2).c_str(), nullptr, 16);
    }
    return count;
}

String timestamp() {
    uint32_t ms  = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    char buf[24];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu",
             hr % 24, min % 60, sec % 60, ms % 1000);
    return String(buf);
}

String makeFilename(const String& prefix, const String& ext) {
    uint32_t ms = millis();
    char buf[32];
    snprintf(buf, sizeof(buf), "%s_%lu%s", prefix.c_str(), ms, ext.c_str());
    return String(buf);
}

String heapJson() {
    StaticJsonDocument<128> doc;
    doc["freeHeap"]  = ESP.getFreeHeap();
    doc["totalHeap"] = ESP.getHeapSize();
    doc["minFree"]   = ESP.getMinFreeHeap();
    String out;
    serializeJson(doc, out);
    return out;
}

float cpuTemperature() {
    // Uses the modern ESP32 Arduino Core function
    return temperatureRead();
}

float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
    return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin);
}

bool isValidJson(const String& json) {
    StaticJsonDocument<512> doc;
    return (deserializeJson(doc, json) == DeserializationError::Ok);
}

} // namespace Utils

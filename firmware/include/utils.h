#pragma once
// ============================================================
// utils.h - Utility Functions
// ============================================================
#include <Arduino.h>

namespace Utils {
    /**
     * @brief Read battery voltage from ADC pin
     */
    float readVoltage(uint8_t pin, float dividerRatio = 4.0f,
                      float vref = 3.3f, int adcMax = 4095);

    /**
     * @brief Format bytes as HEX string "AA BB CC ..."
     */
    String toHexString(const uint8_t* data, size_t len);

    /**
     * @brief Parse HEX string "AA BB CC" into byte array
     * @return number of bytes parsed
     */
    size_t fromHexString(const String& hex, uint8_t* buf, size_t maxLen);

    /**
     * @brief Get ISO8601 timestamp string (millis-based)
     */
    String timestamp();

    /**
     * @brief Generate a safe filename from timestamp
     */
    String makeFilename(const String& prefix, const String& ext = ".bin");

    /**
     * @brief ESP32 free heap as JSON snippet
     */
    String heapJson();

    /**
     * @brief Get CPU temperature (internal sensor, approximate)
     */
    float cpuTemperature();

    /**
     * @brief Map a float value from one range to another
     */
    float mapFloat(float x, float inMin, float inMax, float outMin, float outMax);

    /**
     * @brief Validate JSON string
     */
    bool isValidJson(const String& json);
}

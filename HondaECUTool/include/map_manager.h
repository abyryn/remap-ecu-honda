#pragma once
// ============================================================
// map_manager.h - ECU Map Manager
// Handles extraction, modification, and serialization of maps
// ============================================================
#include <Arduino.h>
#include <vector>
#include "ecu.h"

// --- Map Data Types ---
enum MapDataType {
    MAP_TYPE_UINT8 = 0,
    MAP_TYPE_INT8,
    MAP_TYPE_UINT16_BE, // Big Endian
    MAP_TYPE_UINT16_LE, // Little Endian
    MAP_TYPE_INT16_BE,
    MAP_TYPE_INT16_LE
};

// --- Map Axis Definition ---
struct MapAxis {
    String      name;
    String      units;
    uint32_t    address;
    uint16_t    length;
    MapDataType dataType;
    float       scale;
    float       offset;
};

// --- Map Definition ---
struct MapDefinition {
    String      name;
    String      description;
    uint32_t    address;
    uint16_t    rows;
    uint16_t    cols;
    MapDataType dataType;
    float       scale;
    float       offset;
    String      units;
    MapAxis     xAxis;  // usually RPM
    MapAxis     yAxis;  // usually TPS or MAP
};

// --- Known ECU Layouts ---
struct ECULayout {
    HondaModel model;
    String     partNumberPrefix;
    std::vector<MapDefinition> maps;
};

class MapManager {
public:
    MapManager();

    /**
     * @brief Load maps for specific ECU model
     */
    void loadLayout(HondaModel model, const String& partNumber);

    /**
     * @brief Get list of available maps as JSON array
     */
    String getMapListJson() const;

    /**
     * @brief Get specific map data (values + axes) as JSON
     * @param mapName Name of map
     * @param flashBuffer Full flash dump buffer
     */
    String getMapDataJson(const String& mapName, const std::vector<uint8_t>& flashBuffer) const;

    /**
     * @brief Apply modified map data back to flash buffer
     * @param mapName Name of map
     * @param json 2D array of raw (unscaled) integer values
     * @param flashBuffer Full flash dump buffer to modify
     */
    bool updateMapData(const String& mapName, const String& json, std::vector<uint8_t>& flashBuffer) const;

    /**
     * @brief Check if map definitions exist for current ECU
     */
    bool hasLayout() const { return _currentLayout != nullptr; }

    const ECULayout* getCurrentLayout() const { return _currentLayout; }

private:
    std::vector<ECULayout> _layouts;
    const ECULayout* _currentLayout;

    void _initKnownLayouts();
    const MapDefinition* _findMap(const String& name) const;
    
    // Data extraction helpers
    float _readScaledValue(const uint8_t* data, uint32_t offset, MapDataType type, float scale, float b_offset) const;
    int32_t _readRawValue(const uint8_t* data, uint32_t offset, MapDataType type) const;
    void _writeRawValue(uint8_t* data, uint32_t offset, MapDataType type, int32_t value) const;
    
    String _extractAxisJson(const MapAxis& axis, const std::vector<uint8_t>& buffer) const;
};

extern MapManager MapMgr;

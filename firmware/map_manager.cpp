// ============================================================
// map_manager.cpp - ECU Map Manager Implementation
// ============================================================
#include "include/map_manager.h"
#include "include/logger.h"
#include <ArduinoJson.h>

MapManager MapMgr;

MapManager::MapManager() : _currentLayout(nullptr) {
    _initKnownLayouts();
}

void MapManager::_initKnownLayouts() {
    // --------------------------------------------------------
    // Honda Keihin K-Line ECU (Generic Example for Vario 150)
    // --------------------------------------------------------
    ECULayout vario150;
    vario150.model = HONDA_VARIO;
    vario150.partNumberPrefix = "K59";

    // Fuel Map (Injection Duration) - Example Addresses
    MapDefinition fuelMap;
    fuelMap.name = "Fuel Map (VE)";
    fuelMap.description = "Main volumetric efficiency table";
    fuelMap.address = 0x8000;
    fuelMap.rows = 16;
    fuelMap.cols = 16;
    fuelMap.dataType = MAP_TYPE_UINT8;
    fuelMap.scale = 0.5f;
    fuelMap.offset = 0.0f;
    fuelMap.units = "%";
    
    fuelMap.xAxis.name = "RPM";
    fuelMap.xAxis.address = 0x8100;
    fuelMap.xAxis.length = 16;
    fuelMap.xAxis.dataType = MAP_TYPE_UINT16_BE;
    fuelMap.xAxis.scale = 1.0f;
    fuelMap.xAxis.units = "rpm";

    fuelMap.yAxis.name = "TPS";
    fuelMap.yAxis.address = 0x8120;
    fuelMap.yAxis.length = 16;
    fuelMap.yAxis.dataType = MAP_TYPE_UINT8;
    fuelMap.yAxis.scale = 0.4f; // approx 100/255
    fuelMap.yAxis.units = "%";

    vario150.maps.push_back(fuelMap);

    // Ignition Map - Example Addresses
    MapDefinition ignMap;
    ignMap.name = "Ignition Timing";
    ignMap.description = "Base ignition advance table";
    ignMap.address = 0x8200;
    ignMap.rows = 16;
    ignMap.cols = 16;
    ignMap.dataType = MAP_TYPE_UINT8;
    ignMap.scale = 0.25f;
    ignMap.offset = -20.0f; // negative offset for retard
    ignMap.units = "deg BTDC";
    
    ignMap.xAxis = fuelMap.xAxis; // Share same axis definitions for example
    ignMap.yAxis = fuelMap.yAxis;

    vario150.maps.push_back(ignMap);

    // Rev Limiter
    MapDefinition revLimit;
    revLimit.name = "Rev Limiter";
    revLimit.description = "Maximum engine speed";
    revLimit.address = 0x8400;
    revLimit.rows = 1;
    revLimit.cols = 1;
    revLimit.dataType = MAP_TYPE_UINT16_BE;
    revLimit.scale = 1.0f;
    revLimit.offset = 0.0f;
    revLimit.units = "rpm";
    
    vario150.maps.push_back(revLimit);

    _layouts.push_back(vario150);

    // Add more ECU layouts here (Beat, CBR, etc.)
}

void MapManager::loadLayout(HondaModel model, const String& partNumber) {
    _currentLayout = nullptr;
    for (const auto& layout : _layouts) {
        if (layout.model == model || partNumber.startsWith(layout.partNumberPrefix)) {
            _currentLayout = &layout;
            Logger.log(LOG_INFO, "Map", "Loaded layout for model %s (PN %s), %d maps", 
                ECUManager::modelName(model).c_str(), partNumber.c_str(), layout.maps.size());
            return;
        }
    }
    Logger.log(LOG_WARN, "Map", "No map layout found for model %s", ECUManager::modelName(model).c_str());
}

String MapManager::getMapListJson() const {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("maps");

    if (_currentLayout) {
        for (const auto& map : _currentLayout->maps) {
            JsonObject o = arr.createNestedObject();
            o["name"] = map.name;
            o["desc"] = map.description;
            o["rows"] = map.rows;
            o["cols"] = map.cols;
            o["units"] = map.units;
        }
    }
    
    String out;
    serializeJson(doc, out);
    return out;
}

const MapDefinition* MapManager::_findMap(const String& name) const {
    if (!_currentLayout) return nullptr;
    for (const auto& map : _currentLayout->maps) {
        if (map.name == name) return &map;
    }
    return nullptr;
}

String MapManager::getMapDataJson(const String& mapName, const std::vector<uint8_t>& flashBuffer) const {
    if (flashBuffer.empty()) return "{\"error\":\"Flash buffer empty\"}";
    
    const MapDefinition* map = _findMap(mapName);
    if (!map) return "{\"error\":\"Map not found\"}";

    // Calculate required size based on data type
    uint8_t bytesPerCell = 1;
    if (map->dataType >= MAP_TYPE_UINT16_BE) bytesPerCell = 2;
    
    uint32_t expectedSize = map->address + (map->rows * map->cols * bytesPerCell);
    if (flashBuffer.size() < expectedSize) return "{\"error\":\"Flash buffer too small\"}";

    // Use a large document for 2D arrays
    DynamicJsonDocument doc(8192);
    doc["name"] = map->name;
    doc["units"] = map->units;
    doc["rows"] = map->rows;
    doc["cols"] = map->cols;

    // Extract X Axis
    if (map->xAxis.length > 0 && flashBuffer.size() >= map->xAxis.address + (map->xAxis.length * (map->xAxis.dataType >= MAP_TYPE_UINT16_BE ? 2 : 1))) {
        JsonArray xArr = doc.createNestedArray("xAxis");
        doc["xName"] = map->xAxis.name;
        doc["xUnits"] = map->xAxis.units;
        for (uint16_t c = 0; c < map->xAxis.length; c++) {
            uint32_t offset = map->xAxis.address + c * (map->xAxis.dataType >= MAP_TYPE_UINT16_BE ? 2 : 1);
            xArr.add(serialized(String(_readScaledValue(flashBuffer.data(), offset, map->xAxis.dataType, map->xAxis.scale, map->xAxis.offset), 1)));
        }
    }

    // Extract Y Axis
    if (map->yAxis.length > 0 && flashBuffer.size() >= map->yAxis.address + (map->yAxis.length * (map->yAxis.dataType >= MAP_TYPE_UINT16_BE ? 2 : 1))) {
        JsonArray yArr = doc.createNestedArray("yAxis");
        doc["yName"] = map->yAxis.name;
        doc["yUnits"] = map->yAxis.units;
        for (uint16_t r = 0; r < map->yAxis.length; r++) {
            uint32_t offset = map->yAxis.address + r * (map->yAxis.dataType >= MAP_TYPE_UINT16_BE ? 2 : 1);
            yArr.add(serialized(String(_readScaledValue(flashBuffer.data(), offset, map->yAxis.dataType, map->yAxis.scale, map->yAxis.offset), 1)));
        }
    }

    // Extract Z Data (2D array)
    JsonArray zArr = doc.createNestedArray("zData");
    JsonArray rawArr = doc.createNestedArray("rawData"); // Send raw data for modifying
    
    for (uint16_t r = 0; r < map->rows; r++) {
        JsonArray zRow = zArr.createNestedArray();
        JsonArray rawRow = rawArr.createNestedArray();
        for (uint16_t c = 0; c < map->cols; c++) {
            uint32_t offset = map->address + ((r * map->cols) + c) * bytesPerCell;
            
            float scaled = _readScaledValue(flashBuffer.data(), offset, map->dataType, map->scale, map->offset);
            int32_t raw = _readRawValue(flashBuffer.data(), offset, map->dataType);
            
            zRow.add(serialized(String(scaled, 2))); // keep 2 decimal places
            rawRow.add(raw);
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool MapManager::updateMapData(const String& mapName, const String& json, std::vector<uint8_t>& flashBuffer) const {
    if (flashBuffer.empty()) return false;
    
    const MapDefinition* map = _findMap(mapName);
    if (!map) return false;

    // Parse JSON
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Logger.log(LOG_ERROR, "Map", "Failed to parse update JSON");
        return false;
    }

    JsonArray data = doc["data"].as<JsonArray>();
    if (data.isNull()) return false;

    uint8_t bytesPerCell = (map->dataType >= MAP_TYPE_UINT16_BE) ? 2 : 1;

    // If it's a 1x1 map (like rev limiter)
    if (map->rows == 1 && map->cols == 1) {
        int32_t rawVal = data[0].as<int32_t>();
        _writeRawValue(flashBuffer.data(), map->address, map->dataType, rawVal);
        return true;
    }

    // Process 2D array
    if (data.size() != map->rows) return false;

    for (uint16_t r = 0; r < map->rows; r++) {
        JsonArray row = data[r].as<JsonArray>();
        if (row.size() != map->cols) return false;

        for (uint16_t c = 0; c < map->cols; c++) {
            int32_t rawVal = row[c].as<int32_t>();
            uint32_t offset = map->address + ((r * map->cols) + c) * bytesPerCell;
            _writeRawValue(flashBuffer.data(), offset, map->dataType, rawVal);
        }
    }

    Logger.log(LOG_INFO, "Map", "Map %s updated in buffer", mapName.c_str());
    return true;
}

float MapManager::_readScaledValue(const uint8_t* data, uint32_t offset, MapDataType type, float scale, float b_offset) const {
    int32_t raw = _readRawValue(data, offset, type);
    return (raw * scale) + b_offset;
}

int32_t MapManager::_readRawValue(const uint8_t* data, uint32_t offset, MapDataType type) const {
    switch (type) {
        case MAP_TYPE_UINT8: return data[offset];
        case MAP_TYPE_INT8:  return (int8_t)data[offset];
        case MAP_TYPE_UINT16_BE: return (data[offset] << 8) | data[offset+1];
        case MAP_TYPE_UINT16_LE: return (data[offset+1] << 8) | data[offset];
        case MAP_TYPE_INT16_BE: return (int16_t)((data[offset] << 8) | data[offset+1]);
        case MAP_TYPE_INT16_LE: return (int16_t)((data[offset+1] << 8) | data[offset]);
        default: return 0;
    }
}

void MapManager::_writeRawValue(uint8_t* data, uint32_t offset, MapDataType type, int32_t value) const {
    switch (type) {
        case MAP_TYPE_UINT8: 
        case MAP_TYPE_INT8:
            data[offset] = (uint8_t)(value & 0xFF);
            break;
        case MAP_TYPE_UINT16_BE:
        case MAP_TYPE_INT16_BE:
            data[offset] = (uint8_t)((value >> 8) & 0xFF);
            data[offset+1] = (uint8_t)(value & 0xFF);
            break;
        case MAP_TYPE_UINT16_LE:
        case MAP_TYPE_INT16_LE:
            data[offset+1] = (uint8_t)((value >> 8) & 0xFF);
            data[offset] = (uint8_t)(value & 0xFF);
            break;
    }
}

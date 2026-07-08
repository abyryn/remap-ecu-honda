#pragma once
// ============================================================
// ecu.h - Honda ECU Manager
// Handles: identification, live data, DTC, session
// ============================================================
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

// --- ECU Connection State ---
enum ECUState {
    ECU_DISCONNECTED = 0,
    ECU_CONNECTING,
    ECU_CONNECTED,
    ECU_ERROR
};

// --- DTC Structure ---
struct DTCEntry {
    uint16_t code;
    String   description;
    bool     pending;
    bool     milOn;
};

// --- ECU Info ---
struct ECUInfo {
    String manufacturer;
    String partNumber;
    String firmwareVersion;
    String hardwareVersion;
    String protocol;
    uint32_t eepromSize;
    uint32_t flashSize;
    String vin;
    uint8_t checksum;
};

// --- Live Data ---
struct LiveData {
    uint16_t rpm;
    float    tps;          // Throttle Position %
    float    map;          // Manifold Absolute Pressure kPa
    float    iat;          // Intake Air Temp °C
    float    ect;          // Engine Coolant Temp °C
    float    battVoltage;  // Battery V
    float    injPulseWidth;// ms
    float    ignTiming;    // degrees
    uint8_t  vehicleSpeed; // km/h
    float    engineLoad;   // %
    bool     idleSwitch;
    float    o2Sensor;     // mV
    bool     closedLoop;
    float    fuelTrim;     // %
    uint32_t timestamp;
};

// --- Supported Honda Models ---
enum HondaModel {
    HONDA_BEAT = 0,
    HONDA_SCOOPY,
    HONDA_GENIO,
    HONDA_VARIO,
    HONDA_PCX,
    HONDA_ADV,
    HONDA_SUPRA,
    HONDA_SONIC,
    HONDA_VERZA,
    HONDA_CB150R,
    HONDA_CBR150R,
    HONDA_CRF150L,
    HONDA_STYLO,
    HONDA_EM1,
    HONDA_UNKNOWN
};

class ECUManager {
public:
    ECUManager();

    /**
     * @brief Attempt connection to ECU via K-Line
     * @return true if connected
     */
    bool connect();

    /**
     * @brief Disconnect and end session
     */
    void disconnect();

    /**
     * @brief Read ECU identification data
     */
    bool readIdentification();

    /**
     * @brief Read all live sensor data
     */
    bool readLiveData();

    /**
     * @brief Read Diagnostic Trouble Codes
     */
    bool readDTC();

    /**
     * @brief Clear all DTCs
     */
    bool clearDTC();

    /**
     * @brief Read EEPROM content into buffer
     * @param buf    Output buffer
     * @param size   Bytes to read
     * @param progress Progress callback 0-100
     */
    bool readEEPROM(uint8_t* buf, size_t size,
                    std::function<void(int)> progress = nullptr);

    /**
     * @brief Serialize live data to JSON string
     */
    String liveDataToJson();

    /**
     * @brief Serialize ECU info to JSON string
     */
    String ecuInfoToJson();

    /**
     * @brief Serialize DTC list to JSON string
     */
    String dtcToJson();

    /**
     * @brief Set active vehicle model
     */
    void setModel(HondaModel model);

    /**
     * @brief Get model name string
     */
    static String modelName(HondaModel model);

    // Getters
    ECUState  getState()    const { return _state; }
    LiveData& getLiveData()       { return _live; }
    ECUInfo&  getInfo()           { return _info; }
    std::vector<DTCEntry>& getDTCs() { return _dtcs; }
    bool      isConnected() const { return _state == ECU_CONNECTED; }
    HondaModel getModel()   const { return _model; }

private:
    ECUState  _state;
    ECUInfo   _info;
    LiveData  _live;
    std::vector<DTCEntry> _dtcs;
    HondaModel _model;

    // Internal protocol helpers
    bool _sendService(uint8_t service, const uint8_t* params, size_t pLen,
                      uint8_t* resp, size_t& respLen);
    bool _readDataByLocalId(uint8_t pid, uint8_t* resp, size_t& len);
    String _dtcDescription(uint16_t code);
};

extern ECUManager ECU;

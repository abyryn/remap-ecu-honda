#pragma once
// ============================================================
// flash_manager.h - ECU Flash Read/Write Manager
// Handles full flash lifecycle with safety protections
// ============================================================
#include <Arduino.h>
#include <vector>
#include <functional>
#include <ArduinoJson.h>
#include "ecu.h"

// --- Flash Operation State Machine ---
enum FlashState {
    FLASH_IDLE = 0,
    FLASH_AUTH,         // Authenticating / PIN verify
    FLASH_BACKUP,       // Auto-backup before write
    FLASH_READING,      // Reading flash from ECU
    FLASH_ERASING,      // Erasing flash
    FLASH_WRITING,      // Writing flash to ECU
    FLASH_VERIFYING,    // Verifying written data
    FLASH_REBOOTING,    // Rebooting ECU
    FLASH_COMPLETE,     // Operation done
    FLASH_ERROR,        // Error occurred
    FLASH_RECOVERY      // Recovery mode
};

// --- Flash Operation Type ---
enum FlashOpType {
    FLASH_OP_READ_FULL = 0,
    FLASH_OP_READ_EEPROM,
    FLASH_OP_READ_CALIBRATION,
    FLASH_OP_READ_MAPS,
    FLASH_OP_WRITE_CALIBRATION,
    FLASH_OP_WRITE_FULL,
    FLASH_OP_ERASE,
    FLASH_OP_VERIFY
};

// --- Flash Result ---
enum FlashResult {
    FLASH_OK = 0,
    FLASH_ERR_NOT_CONNECTED,
    FLASH_ERR_AUTH_FAILED,
    FLASH_ERR_BACKUP_FAILED,
    FLASH_ERR_READ_FAILED,
    FLASH_ERR_ERASE_FAILED,
    FLASH_ERR_WRITE_FAILED,
    FLASH_ERR_VERIFY_FAILED,
    FLASH_ERR_CHECKSUM,
    FLASH_ERR_TIMEOUT,
    FLASH_ERR_POWER_LOW,
    FLASH_ERR_INVALID_DATA,
    FLASH_ERR_RECOVERY_FAILED,
    FLASH_ERR_ABORTED,
    FLASH_ERR_SIZE_MISMATCH
};

// --- Flash Progress Info ---
struct FlashProgress {
    FlashState  state;
    FlashOpType opType;
    int         percent;       // 0-100
    uint32_t    bytesTotal;
    uint32_t    bytesDone;
    uint32_t    speedBps;      // bytes per second
    uint32_t    etaSeconds;    // estimated time remaining
    String      message;
    uint32_t    blocksDone;
    uint32_t    blocksTotal;
    uint32_t    retries;
};

// --- Checkpoint (for power failure recovery) ---
struct FlashCheckpoint {
    FlashOpType opType;
    uint32_t    lastBlock;
    uint32_t    totalBlocks;
    uint32_t    flashSize;
    uint32_t    crc32;
    String      filename;
    bool        valid;
};

// --- Progress callback ---
typedef std::function<void(const FlashProgress&)> FlashProgressCB;

class FlashManager {
public:
    FlashManager();

    /**
     * @brief Read full flash from ECU into buffer
     * @param flashSize  Total bytes to read (auto-detect if 0)
     * @param onProgress Progress callback
     * @return FlashResult
     */
    FlashResult readFullFlash(size_t flashSize = 0, FlashProgressCB onProgress = nullptr);

    /**
     * @brief Read EEPROM from ECU
     */
    FlashResult readEEPROM(FlashProgressCB onProgress = nullptr);

    /**
     * @brief Read calibration area only
     */
    FlashResult readCalibration(FlashProgressCB onProgress = nullptr);

    /**
     * @brief Write calibration data to ECU
     * @param data  Calibration buffer
     * @param size  Buffer size
     * @param autoBackup  Auto-backup before write
     * @param onProgress  Progress callback
     */
    FlashResult writeCalibration(const uint8_t* data, size_t size,
                                  bool autoBackup = true,
                                  FlashProgressCB onProgress = nullptr);

    /**
     * @brief Write full flash to ECU
     */
    FlashResult writeFullFlash(const uint8_t* data, size_t size,
                                bool autoBackup = true,
                                FlashProgressCB onProgress = nullptr);

    /**
     * @brief Erase ECU flash
     */
    FlashResult eraseFlash(FlashProgressCB onProgress = nullptr);

    /**
     * @brief Verify ECU flash against buffer
     */
    FlashResult verifyFlash(const uint8_t* expected, size_t size,
                             FlashProgressCB onProgress = nullptr);

    /**
     * @brief Start recovery mode after failed flash
     */
    FlashResult startRecovery(FlashProgressCB onProgress = nullptr);

    /**
     * @brief Resume interrupted flash from checkpoint
     */
    FlashResult resumeFlash(FlashProgressCB onProgress = nullptr);

    /**
     * @brief Abort current operation
     */
    void abort();

    /**
     * @brief Save current read buffer to file
     */
    bool saveToFile(const String& filename);

    /**
     * @brief Load file into write buffer
     */
    bool loadFromFile(const String& filename);

    // Getters
    FlashState           getState()     const { return _state; }
    const FlashProgress& getProgress()  const { return _progress; }
    const std::vector<uint8_t>& getBuffer() const { return _buffer; }
    std::vector<uint8_t>& getBufferRef()    { return _buffer; }
    size_t               getBufferSize() const { return _buffer.size(); }
    bool                 isOperationActive() const;
    String               stateToString(FlashState s) const;
    String               resultToString(FlashResult r) const;
    String               progressToJson() const;

private:
    FlashState    _state;
    FlashProgress _progress;
    std::vector<uint8_t> _buffer;
    FlashCheckpoint _checkpoint;
    bool          _aborted;
    uint32_t      _opStartTime;

    // Internal operations
    FlashResult _readBlocks(uint32_t startAddr, size_t size,
                             FlashOpType opType, FlashProgressCB cb);
    FlashResult _writeBlocks(const uint8_t* data, uint32_t startAddr, size_t size,
                              FlashOpType opType, FlashProgressCB cb);
    FlashResult _eraseBlocks(FlashProgressCB cb);
    FlashResult _verifyBlocks(const uint8_t* expected, size_t size, FlashProgressCB cb);

    // KWP2000 flash services
    bool _requestDownload(uint32_t addr, size_t size);
    bool _requestUpload(uint32_t addr, size_t size);
    bool _transferDataRead(uint8_t blockNum, uint8_t* data, size_t& len);
    bool _transferDataWrite(uint8_t blockNum, const uint8_t* data, size_t len);
    bool _requestTransferExit();
    bool _routineControlErase(uint32_t addr, size_t size);
    bool _securityAccess();
    bool _enterProgrammingSession();
    bool _exitProgrammingSession();
    bool _ecuReset(uint8_t resetType);

    // Safety
    bool _checkVoltage();
    void _keepAlive();
    void _saveCheckpoint();
    void _loadCheckpoint();
    void _clearCheckpoint();

    void _updateProgress(FlashState state, int pct, const String& msg);
    void _calcSpeed();
};

extern FlashManager Flash;

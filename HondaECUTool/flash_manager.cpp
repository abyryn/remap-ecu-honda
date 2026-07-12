// ============================================================
// flash_manager.cpp - ECU Flash Read/Write Manager
// Implements the full flash lifecycle with safety protections
// ============================================================
#include "include/flash_manager.h"
#include "include/config.h"
#include "include/kline.h"
#include "include/ecu.h"
#include "include/logger.h"
#include "include/security.h"
#include "include/backup.h"
#include "include/filesystem.h"
#include "include/utils.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

FlashManager Flash;

// ---- KWP2000 Service IDs for Flash ----
#define SVC_DIAGNOSTIC_SESSION  0x10
#define SVC_SECURITY_ACCESS     0x27
#define SVC_REQUEST_DOWNLOAD    0x34
#define SVC_REQUEST_UPLOAD      0x35
#define SVC_TRANSFER_DATA       0x36
#define SVC_TRANSFER_EXIT       0x37
#define SVC_ROUTINE_CONTROL     0x31
#define SVC_ECU_RESET           0x11

// Session types
#define SESSION_DEFAULT         0x01
#define SESSION_PROGRAMMING     0x02
#define SESSION_EXTENDED        0x03

// Reset types
#define RESET_HARD              0x01
#define RESET_KEY_OFF_ON        0x02
#define RESET_SOFT              0x03

// ============================================================
FlashManager::FlashManager()
    : _state(FLASH_IDLE), _aborted(false), _opStartTime(0) {
    memset(&_progress, 0, sizeof(_progress));
    memset(&_checkpoint, 0, sizeof(_checkpoint));
}

bool FlashManager::isOperationActive() const {
    return _state != FLASH_IDLE && _state != FLASH_COMPLETE && _state != FLASH_ERROR;
}

// ============================================================
// Read Full Flash
// ============================================================
FlashResult FlashManager::readFullFlash(size_t flashSize, FlashProgressCB onProgress) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;
    if (!_checkVoltage()) return FLASH_ERR_POWER_LOW;

    if (flashSize == 0) flashSize = ECU.getInfo().flashSize;
    if (flashSize == 0) flashSize = FLASH_MAX_SIZE;

    Logger.log(LOG_INFO, "Flash", "Reading full flash: %d bytes", flashSize);
    _opStartTime = millis();
    _aborted = false;

    return _readBlocks(0, flashSize, FLASH_OP_READ_FULL, onProgress);
}

// ============================================================
// Read EEPROM
// ============================================================
FlashResult FlashManager::readEEPROM(FlashProgressCB onProgress) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;

    size_t eepromSize = ECU.getInfo().eepromSize;
    if (eepromSize == 0) eepromSize = FLASH_EEPROM_SIZE;

    Logger.log(LOG_INFO, "Flash", "Reading EEPROM: %d bytes", eepromSize);
    _opStartTime = millis();
    _aborted = false;

    return _readBlocks(0, eepromSize, FLASH_OP_READ_EEPROM, onProgress);
}

// ============================================================
// Read Calibration
// ============================================================
FlashResult FlashManager::readCalibration(FlashProgressCB onProgress) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;

    // Calibration area varies by ECU, use first 32KB as default
    size_t calSize = 32768;
    Logger.log(LOG_INFO, "Flash", "Reading calibration: %d bytes", calSize);
    _opStartTime = millis();
    _aborted = false;

    return _readBlocks(0, calSize, FLASH_OP_READ_CALIBRATION, onProgress);
}

// ============================================================
// Write Calibration
// ============================================================
FlashResult FlashManager::writeCalibration(const uint8_t* data, size_t size,
                                            bool autoBackup, FlashProgressCB cb) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;
    if (!_checkVoltage()) return FLASH_ERR_POWER_LOW;
    if (!data || size == 0) return FLASH_ERR_INVALID_DATA;

    // Verify PIN
    if (Security.isPINEnabled() && !Security.isPINVerified()) {
        return FLASH_ERR_AUTH_FAILED;
    }

    _opStartTime = millis();
    _aborted = false;

    // Step 1: Auto-backup
    if (autoBackup && FLASH_AUTO_BACKUP) {
        _updateProgress(FLASH_BACKUP, 0, "Auto-backup before write...");
        if (cb) cb(_progress);

        String backupName = Utils::makeFilename("pre_write", ".bin");
        bool backupOK = Backup.backupEEPROM(backupName, nullptr);
        if (!backupOK) {
            Logger.log(LOG_ERROR, "Flash", "Auto-backup failed, aborting write");
            _state = FLASH_ERROR;
            return FLASH_ERR_BACKUP_FAILED;
        }
        Logger.log(LOG_INFO, "Flash", "Auto-backup saved: %s", backupName.c_str());
    }

    // Step 2: Enter programming session
    _updateProgress(FLASH_AUTH, 5, "Entering programming mode...");
    if (cb) cb(_progress);

    if (!_enterProgrammingSession()) {
        _state = FLASH_ERROR;
        return FLASH_ERR_AUTH_FAILED;
    }

    // Step 3: Security access
    if (!_securityAccess()) {
        Logger.log(LOG_WARN, "Flash", "Security access skipped (may not be required)");
    }

    // Step 4: Erase
    _updateProgress(FLASH_ERASING, 10, "Erasing flash...");
    if (cb) cb(_progress);

    FlashResult er = _eraseBlocks(cb);
    if (er != FLASH_OK) {
        _exitProgrammingSession();
        return er;
    }

    // Step 5: Write
    FlashResult wr = _writeBlocks(data, 0, size, FLASH_OP_WRITE_CALIBRATION, cb);
    if (wr != FLASH_OK) {
        _exitProgrammingSession();
        return wr;
    }

    // Step 6: Verify
    _updateProgress(FLASH_VERIFYING, 85, "Verifying...");
    if (cb) cb(_progress);

    FlashResult vr = _verifyBlocks(data, size, cb);
    if (vr != FLASH_OK) {
        _exitProgrammingSession();
        return vr;
    }

    // Step 7: Reboot ECU
    _updateProgress(FLASH_REBOOTING, 95, "Rebooting ECU...");
    if (cb) cb(_progress);

    _exitProgrammingSession();
    _ecuReset(RESET_HARD);

    _updateProgress(FLASH_COMPLETE, 100, "Write complete!");
    if (cb) cb(_progress);

    _clearCheckpoint();
    Logger.log(LOG_INFO, "Flash", "Calibration write complete: %d bytes", size);
    return FLASH_OK;
}

// ============================================================
// Write Full Flash
// ============================================================
FlashResult FlashManager::writeFullFlash(const uint8_t* data, size_t size,
                                          bool autoBackup, FlashProgressCB cb) {
    // Full flash follows same flow as calibration write
    return writeCalibration(data, size, autoBackup, cb);
}

// ============================================================
// Erase Flash
// ============================================================
FlashResult FlashManager::eraseFlash(FlashProgressCB cb) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;

    _updateProgress(FLASH_AUTH, 0, "Entering programming mode...");
    if (cb) cb(_progress);

    if (!_enterProgrammingSession()) return FLASH_ERR_AUTH_FAILED;
    if (!_securityAccess()) {
        Logger.log(LOG_WARN, "Flash", "Security access skipped");
    }

    FlashResult r = _eraseBlocks(cb);

    _exitProgrammingSession();
    return r;
}

// ============================================================
// Verify Flash
// ============================================================
FlashResult FlashManager::verifyFlash(const uint8_t* expected, size_t size,
                                       FlashProgressCB cb) {
    if (!ECU.isConnected()) return FLASH_ERR_NOT_CONNECTED;
    return _verifyBlocks(expected, size, cb);
}

// ============================================================
// Recovery Mode
// ============================================================
FlashResult FlashManager::startRecovery(FlashProgressCB cb) {
    Logger.log(LOG_INFO, "Flash", "Starting ECU recovery...");
    _state = FLASH_RECOVERY;

    for (int attempt = 1; attempt <= RECOVERY_MAX_ATTEMPTS; attempt++) {
        _updateProgress(FLASH_RECOVERY, attempt * 100 / RECOVERY_MAX_ATTEMPTS,
                        "Recovery attempt " + String(attempt));
        if (cb) cb(_progress);

        // Step 1: Disconnect and re-initialize K-Line
        ECU.disconnect();
        delay(RECOVERY_DELAY_MS);

        KLine.begin(KLINE_TX_PIN, KLINE_RX_PIN, KLINE_BAUD);
        delay(500);

        // Step 2: Try to reconnect
        if (ECU.connect()) {
            Logger.log(LOG_INFO, "Flash", "Recovery: ECU reconnected on attempt %d", attempt);

            // Step 3: Check for checkpoint and resume
            _loadCheckpoint();
            if (_checkpoint.valid) {
                Logger.log(LOG_INFO, "Flash", "Recovery: Found checkpoint, resuming...");
                return resumeFlash(cb);
            }

            _updateProgress(FLASH_COMPLETE, 100, "Recovery successful - ECU reconnected");
            if (cb) cb(_progress);
            return FLASH_OK;
        }

        Logger.log(LOG_WARN, "Flash", "Recovery attempt %d failed", attempt);
        delay(RECOVERY_DELAY_MS);
    }

    Logger.log(LOG_ERROR, "Flash", "Recovery failed after %d attempts", RECOVERY_MAX_ATTEMPTS);
    _state = FLASH_ERROR;
    return FLASH_ERR_RECOVERY_FAILED;
}

// ============================================================
// Resume Flash
// ============================================================
FlashResult FlashManager::resumeFlash(FlashProgressCB cb) {
    _loadCheckpoint();
    if (!_checkpoint.valid) {
        Logger.log(LOG_WARN, "Flash", "No checkpoint to resume from");
        return FLASH_ERR_INVALID_DATA;
    }

    Logger.log(LOG_INFO, "Flash", "Resuming from block %d/%d",
               _checkpoint.lastBlock, _checkpoint.totalBlocks);

    // Load the source file
    if (!loadFromFile(_checkpoint.filename)) {
        return FLASH_ERR_INVALID_DATA;
    }

    // Resume writing from last checkpoint
    uint32_t startAddr = _checkpoint.lastBlock * FLASH_BLOCK_SIZE;
    size_t remaining = _buffer.size() - startAddr;

    if (!_enterProgrammingSession()) return FLASH_ERR_AUTH_FAILED;

    FlashResult r = _writeBlocks(_buffer.data() + startAddr, startAddr, remaining,
                                  _checkpoint.opType, cb);
    if (r != FLASH_OK) {
        _exitProgrammingSession();
        return r;
    }

    // Verify full image
    r = _verifyBlocks(_buffer.data(), _buffer.size(), cb);
    _exitProgrammingSession();

    if (r == FLASH_OK) {
        _ecuReset(RESET_HARD);
        _clearCheckpoint();
        _updateProgress(FLASH_COMPLETE, 100, "Resume complete!");
        if (cb) cb(_progress);
    }
    return r;
}

// ============================================================
// Abort
// ============================================================
void FlashManager::abort() {
    _aborted = true;
    Logger.log(LOG_WARN, "Flash", "Operation aborted by user");
}

// ============================================================
// File I/O
// ============================================================
bool FlashManager::saveToFile(const String& filename) {
    if (_buffer.empty()) return false;
    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    bool ok = FS_Mgr.writeFile(fullPath, _buffer.data(), _buffer.size());
    if (ok) {
        // Write metadata
        StaticJsonDocument<256> meta;
        meta["filename"]  = filename;
        meta["timestamp"] = Utils::timestamp();
        meta["size"]      = _buffer.size();
        meta["crc32"]     = Security.crc32(_buffer.data(), _buffer.size());
        meta["model"]     = ECUManager::modelName(ECU.getModel());
        meta["partNumber"]= ECU.getInfo().partNumber;
        meta["type"]      = "flash_dump";
        String metaJson;
        serializeJson(meta, metaJson);
        FS_Mgr.writeText(fullPath + ".meta", metaJson);
        Logger.log(LOG_INFO, "Flash", "Saved: %s (%d bytes)", filename.c_str(), _buffer.size());
    }
    return ok;
}

bool FlashManager::loadFromFile(const String& filename) {
    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    if (!FS_Mgr.exists(fullPath)) {
        Logger.log(LOG_ERROR, "Flash", "File not found: %s", fullPath.c_str());
        return false;
    }

    // Get file size
    auto files = FS_Mgr.listDir(FS_BACKUP_DIR);
    size_t fileSize = 0;
    for (auto& f : files) {
        if (f.name == filename) { fileSize = f.size; break; }
    }
    if (fileSize == 0) return false;

    _buffer.resize(fileSize);
    size_t readLen = fileSize;
    bool ok = FS_Mgr.readFile(fullPath, _buffer.data(), readLen);
    if (!ok) _buffer.clear();
    return ok;
}

// ============================================================
// Internal: Read Blocks
// ============================================================
FlashResult FlashManager::_readBlocks(uint32_t startAddr, size_t size,
                                       FlashOpType opType, FlashProgressCB cb) {
    _buffer.clear();
    _buffer.resize(size, 0xFF);
    _state = FLASH_READING;
    _progress.opType = opType;

    uint32_t totalBlocks = (size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
    uint32_t blocksDone = 0;
    uint32_t addr = startAddr;
    uint32_t retries = 0;
    uint32_t lastKeepAlive = millis();

    // Enter programming session for flash read
    if (opType == FLASH_OP_READ_FULL || opType == FLASH_OP_READ_CALIBRATION) {
        if (!_enterProgrammingSession()) {
            _state = FLASH_ERROR;
            return FLASH_ERR_AUTH_FAILED;
        }
    }

    // Request download
    if (!_requestDownload(startAddr, size)) {
        Logger.log(LOG_WARN, "Flash", "RequestDownload failed, trying direct read");
    }

    while (blocksDone < totalBlocks) {
        if (_aborted) {
            _state = FLASH_ERROR;
            return FLASH_ERR_ABORTED;
        }

        esp_task_wdt_reset();  // Feed watchdog

        size_t chunkSize = min((size_t)FLASH_BLOCK_SIZE, size - blocksDone * FLASH_BLOCK_SIZE);

        // Build read request
        uint8_t req[8];
        req[0] = 0x05;  // length
        req[1] = 0x23;  // readMemoryByAddress
        req[2] = (addr >> 16) & 0xFF;
        req[3] = (addr >> 8)  & 0xFF;
        req[4] = addr & 0xFF;
        req[5] = (uint8_t)chunkSize;
        req[6] = KLineDriver::calcChecksum(req, 6);

        uint8_t resp[128];
        size_t respLen = 0;
        KLineResult kr = KLine.request(req, 7, resp, respLen, FLASH_TIMEOUT_MS);

        if (kr != KLINE_OK || respLen < chunkSize + 3) {
            retries++;
            if (retries > FLASH_RETRY_MAX) {
                Logger.log(LOG_ERROR, "Flash", "Read failed at addr 0x%06X after %d retries",
                           addr, FLASH_RETRY_MAX);
                _state = FLASH_ERROR;
                return FLASH_ERR_READ_FAILED;
            }
            Logger.log(LOG_WARN, "Flash", "Read retry at 0x%06X (%d)", addr, retries);
            delay(100);
            continue;
        }

        // Copy data (skip header bytes)
        memcpy(_buffer.data() + blocksDone * FLASH_BLOCK_SIZE, resp + 2, chunkSize);
        blocksDone++;
        addr += chunkSize;
        retries = 0;

        // Update progress
        int pct = (int)(blocksDone * 100 / totalBlocks);
        _progress.blocksDone  = blocksDone;
        _progress.blocksTotal = totalBlocks;
        _progress.bytesDone   = blocksDone * FLASH_BLOCK_SIZE;
        _progress.bytesTotal  = size;
        _progress.retries     = retries;
        _calcSpeed();
        _updateProgress(FLASH_READING, pct, "Reading block " + String(blocksDone) + "/" + String(totalBlocks));
        if (cb) cb(_progress);

        // Keep-alive
        if (millis() - lastKeepAlive > FLASH_KEEPALIVE_MS) {
            _keepAlive();
            lastKeepAlive = millis();
        }

        // Checkpoint
        if (blocksDone % FLASH_CHECKPOINT_INTERVAL == 0) {
            _checkpoint.opType     = opType;
            _checkpoint.lastBlock  = blocksDone;
            _checkpoint.totalBlocks= totalBlocks;
            _checkpoint.flashSize  = size;
            _checkpoint.valid      = true;
        }

        delay(5);  // small delay between blocks
    }

    _requestTransferExit();

    if (opType == FLASH_OP_READ_FULL || opType == FLASH_OP_READ_CALIBRATION) {
        _exitProgrammingSession();
    }

    _updateProgress(FLASH_COMPLETE, 100, "Read complete!");
    if (cb) cb(_progress);

    Logger.log(LOG_INFO, "Flash", "Read complete: %d bytes, CRC32=0x%08X",
               size, Security.crc32(_buffer.data(), _buffer.size()));
    return FLASH_OK;
}

// ============================================================
// Internal: Write Blocks
// ============================================================
FlashResult FlashManager::_writeBlocks(const uint8_t* data, uint32_t startAddr, size_t size,
                                        FlashOpType opType, FlashProgressCB cb) {
    _state = FLASH_WRITING;
    _progress.opType = opType;

    uint32_t totalBlocks = (size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
    uint32_t blocksDone = 0;
    uint32_t addr = startAddr;
    uint32_t retries = 0;
    uint32_t lastKeepAlive = millis();

    // Request upload
    if (!_requestUpload(startAddr, size)) {
        Logger.log(LOG_WARN, "Flash", "RequestUpload failed, trying direct write");
    }

    while (blocksDone < totalBlocks) {
        if (_aborted) {
            _saveCheckpoint();
            _state = FLASH_ERROR;
            return FLASH_ERR_ABORTED;
        }

        if (!_checkVoltage()) {
            _saveCheckpoint();
            _state = FLASH_ERROR;
            return FLASH_ERR_POWER_LOW;
        }

        esp_task_wdt_reset();

        size_t chunkSize = min((size_t)FLASH_BLOCK_SIZE, size - blocksDone * FLASH_BLOCK_SIZE);

        // Build write request
        uint8_t req[FLASH_BLOCK_SIZE + 8];
        req[0] = (uint8_t)(chunkSize + 4);  // length
        req[1] = 0x3D;   // writeMemoryByAddress
        req[2] = (addr >> 16) & 0xFF;
        req[3] = (addr >> 8)  & 0xFF;
        req[4] = addr & 0xFF;
        memcpy(req + 5, data + blocksDone * FLASH_BLOCK_SIZE, chunkSize);
        req[5 + chunkSize] = KLineDriver::calcChecksum(req, 5 + chunkSize);

        uint8_t resp[16];
        size_t respLen = 0;
        KLineResult kr = KLine.request(req, 6 + chunkSize, resp, respLen, FLASH_TIMEOUT_MS);

        if (kr != KLINE_OK || respLen < 2) {
            retries++;
            if (retries > FLASH_RETRY_MAX) {
                Logger.log(LOG_ERROR, "Flash", "Write failed at addr 0x%06X", addr);
                _saveCheckpoint();
                _state = FLASH_ERROR;
                return FLASH_ERR_WRITE_FAILED;
            }
            Logger.log(LOG_WARN, "Flash", "Write retry at 0x%06X (%d)", addr, retries);
            delay(100);
            continue;
        }

        blocksDone++;
        addr += chunkSize;
        retries = 0;

        // Progress 20-80% range for write phase
        int pct = 20 + (int)(blocksDone * 60 / totalBlocks);
        _progress.blocksDone  = blocksDone;
        _progress.blocksTotal = totalBlocks;
        _progress.bytesDone   = blocksDone * FLASH_BLOCK_SIZE;
        _progress.bytesTotal  = size;
        _calcSpeed();
        _updateProgress(FLASH_WRITING, pct, "Writing block " + String(blocksDone) + "/" + String(totalBlocks));
        if (cb) cb(_progress);

        // Keep-alive
        if (millis() - lastKeepAlive > FLASH_KEEPALIVE_MS) {
            _keepAlive();
            lastKeepAlive = millis();
        }

        // Checkpoint
        if (blocksDone % FLASH_CHECKPOINT_INTERVAL == 0) {
            _checkpoint.opType     = opType;
            _checkpoint.lastBlock  = blocksDone;
            _checkpoint.totalBlocks= totalBlocks;
            _checkpoint.flashSize  = size;
            _checkpoint.valid      = true;
            _saveCheckpoint();
        }

        delay(10);  // inter-block delay for ECU processing
    }

    _requestTransferExit();
    Logger.log(LOG_INFO, "Flash", "Write complete: %d bytes (%d blocks)", size, totalBlocks);
    return FLASH_OK;
}

// ============================================================
// Internal: Erase
// ============================================================
FlashResult FlashManager::_eraseBlocks(FlashProgressCB cb) {
    _state = FLASH_ERASING;
    _updateProgress(FLASH_ERASING, 10, "Erasing ECU flash...");
    if (cb) cb(_progress);

    if (!_routineControlErase(0x000000, FLASH_MAX_SIZE)) {
        Logger.log(LOG_ERROR, "Flash", "Erase command failed");
        _state = FLASH_ERROR;
        return FLASH_ERR_ERASE_FAILED;
    }

    // Wait for erase to complete (can take several seconds)
    for (int i = 0; i < 30; i++) {
        esp_task_wdt_reset();
        delay(500);
        _updateProgress(FLASH_ERASING, 10 + i * 3, "Erasing... " + String(i + 1) + "s");
        if (cb) cb(_progress);
    }

    Logger.log(LOG_INFO, "Flash", "Erase complete");
    return FLASH_OK;
}

// ============================================================
// Internal: Verify
// ============================================================
FlashResult FlashManager::_verifyBlocks(const uint8_t* expected, size_t size, FlashProgressCB cb) {
    _state = FLASH_VERIFYING;

    // Read back and compare
    std::vector<uint8_t> readback(size);
    uint32_t totalBlocks = (size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
    uint32_t addr = 0;

    for (uint32_t b = 0; b < totalBlocks; b++) {
        esp_task_wdt_reset();

        size_t chunkSize = min((size_t)FLASH_BLOCK_SIZE, size - b * FLASH_BLOCK_SIZE);

        uint8_t req[8];
        req[0] = 0x05;
        req[1] = 0x23;
        req[2] = (addr >> 16) & 0xFF;
        req[3] = (addr >> 8)  & 0xFF;
        req[4] = addr & 0xFF;
        req[5] = (uint8_t)chunkSize;
        req[6] = KLineDriver::calcChecksum(req, 6);

        uint8_t resp[128];
        size_t respLen = 0;
        KLineResult kr = KLine.request(req, 7, resp, respLen, FLASH_TIMEOUT_MS);

        if (kr == KLINE_OK && respLen >= chunkSize + 2) {
            memcpy(readback.data() + b * FLASH_BLOCK_SIZE, resp + 2, chunkSize);
        }

        int pct = 85 + (int)(b * 10 / totalBlocks);
        _updateProgress(FLASH_VERIFYING, pct, "Verifying block " + String(b + 1) + "/" + String(totalBlocks));
        if (cb) cb(_progress);

        addr += chunkSize;
        delay(5);
    }

    // Compare CRC
    uint32_t expectedCRC = Security.crc32(expected, size);
    uint32_t actualCRC   = Security.crc32(readback.data(), size);

    if (expectedCRC != actualCRC) {
        Logger.log(LOG_ERROR, "Flash", "Verify FAILED! Expected CRC=0x%08X Got=0x%08X",
                   expectedCRC, actualCRC);
        _state = FLASH_ERROR;
        return FLASH_ERR_VERIFY_FAILED;
    }

    Logger.log(LOG_INFO, "Flash", "Verify OK! CRC=0x%08X", actualCRC);
    return FLASH_OK;
}

// ============================================================
// KWP2000 Services
// ============================================================
bool FlashManager::_enterProgrammingSession() {
    uint8_t req[] = {0x02, SVC_DIAGNOSTIC_SESSION, SESSION_PROGRAMMING, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[16];
    size_t len = 0;
    KLineResult r = KLine.request(req, 4, resp, len, 2000);

    bool ok = (r == KLINE_OK && len >= 2 && resp[1] == 0x50);
    Logger.log(ok ? LOG_INFO : LOG_WARN, "Flash",
               "Programming session: %s", ok ? "OK" : "failed");
    return ok;
}

bool FlashManager::_exitProgrammingSession() {
    uint8_t req[] = {0x02, SVC_DIAGNOSTIC_SESSION, SESSION_DEFAULT, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[16];
    size_t len = 0;
    KLine.request(req, 4, resp, len, 1000);
    return true;
}

bool FlashManager::_securityAccess() {
    // Seed request
    uint8_t req[] = {0x02, SVC_SECURITY_ACCESS, 0x01, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[16];
    size_t len = 0;
    KLineResult r = KLine.request(req, 4, resp, len, 2000);

    if (r != KLINE_OK || len < 4) {
        Logger.log(LOG_WARN, "Flash", "Security seed request failed");
        return false;
    }

    // Calculate key from seed (Honda algorithm: XOR with constant)
    uint16_t seed = ((uint16_t)resp[2] << 8) | resp[3];
    uint16_t key  = seed ^ 0x9281;  // Honda-specific constant

    uint8_t keyReq[] = {0x04, SVC_SECURITY_ACCESS, 0x02,
                        (uint8_t)(key >> 8), (uint8_t)(key & 0xFF), 0x00};
    keyReq[5] = KLineDriver::calcChecksum(keyReq, 5);

    r = KLine.request(keyReq, 6, resp, len, 2000);
    bool ok = (r == KLINE_OK && len >= 2 && resp[1] == 0x67);
    Logger.log(ok ? LOG_INFO : LOG_WARN, "Flash",
               "Security access: %s", ok ? "OK" : "failed");
    return ok;
}

bool FlashManager::_requestDownload(uint32_t addr, size_t size) {
    uint8_t req[8];
    req[0] = 0x07;
    req[1] = SVC_REQUEST_DOWNLOAD;
    req[2] = 0x00;  // dataFormatIdentifier
    req[3] = 0x44;  // addressAndLengthFormatIdentifier
    req[4] = (addr >> 8) & 0xFF;
    req[5] = addr & 0xFF;
    req[6] = (size >> 8) & 0xFF;
    req[7] = size & 0xFF;
    // No checksum appended here - request() handles framing

    uint8_t resp[16];
    size_t len = 0;
    KLineResult r = KLine.request(req, 8, resp, len, 3000);
    return (r == KLINE_OK && len >= 2 && resp[1] == 0x74);
}

bool FlashManager::_requestUpload(uint32_t addr, size_t size) {
    uint8_t req[8];
    req[0] = 0x07;
    req[1] = SVC_REQUEST_UPLOAD;
    req[2] = 0x00;
    req[3] = 0x44;
    req[4] = (addr >> 8) & 0xFF;
    req[5] = addr & 0xFF;
    req[6] = (size >> 8) & 0xFF;
    req[7] = size & 0xFF;

    uint8_t resp[16];
    size_t len = 0;
    KLineResult r = KLine.request(req, 8, resp, len, 3000);
    return (r == KLINE_OK && len >= 2 && resp[1] == 0x75);
}

bool FlashManager::_transferDataRead(uint8_t blockNum, uint8_t* data, size_t& len) {
    uint8_t req[] = {0x02, SVC_TRANSFER_DATA, blockNum, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[128];
    size_t respLen = 0;
    KLineResult r = KLine.request(req, 4, resp, respLen, FLASH_TIMEOUT_MS);

    if (r == KLINE_OK && respLen > 2) {
        len = respLen - 2;
        memcpy(data, resp + 2, len);
        return true;
    }
    return false;
}

bool FlashManager::_transferDataWrite(uint8_t blockNum, const uint8_t* data, size_t len) {
    uint8_t req[FLASH_BLOCK_SIZE + 4];
    req[0] = (uint8_t)(len + 2);
    req[1] = SVC_TRANSFER_DATA;
    req[2] = blockNum;
    memcpy(req + 3, data, len);
    req[3 + len] = KLineDriver::calcChecksum(req, 3 + len);

    uint8_t resp[16];
    size_t respLen = 0;
    KLineResult r = KLine.request(req, 4 + len, resp, respLen, FLASH_TIMEOUT_MS);
    return (r == KLINE_OK && respLen >= 2 && resp[1] == 0x76);
}

bool FlashManager::_requestTransferExit() {
    uint8_t req[] = {0x01, SVC_TRANSFER_EXIT, 0x00};
    req[2] = KLineDriver::calcChecksum(req, 2);

    uint8_t resp[16];
    size_t len = 0;
    KLine.request(req, 3, resp, len, 1000);
    return true;
}

bool FlashManager::_routineControlErase(uint32_t addr, size_t size) {
    uint8_t req[10];
    req[0] = 0x08;
    req[1] = SVC_ROUTINE_CONTROL;
    req[2] = 0x01;  // startRoutine
    req[3] = 0xFF;  // eraseFlash routineID high
    req[4] = 0x00;  // eraseFlash routineID low
    req[5] = (addr >> 16) & 0xFF;
    req[6] = (addr >> 8)  & 0xFF;
    req[7] = addr & 0xFF;
    req[8] = KLineDriver::calcChecksum(req, 8);

    uint8_t resp[16];
    size_t len = 0;
    KLineResult r = KLine.request(req, 9, resp, len, 30000);  // long timeout for erase
    return (r == KLINE_OK && len >= 2 && resp[1] == 0x71);
}

bool FlashManager::_ecuReset(uint8_t resetType) {
    uint8_t req[] = {0x02, SVC_ECU_RESET, resetType, 0x00};
    req[3] = KLineDriver::calcChecksum(req, 3);

    uint8_t resp[8];
    size_t len = 0;
    KLine.request(req, 4, resp, len, 2000);
    delay(3000);  // wait for ECU to reboot
    return true;
}

// ============================================================
// Safety
// ============================================================
bool FlashManager::_checkVoltage() {
    float v = Utils::readVoltage(VBAT_PIN);
    if (v < VBAT_LOW_WARN) {
        Logger.log(LOG_ERROR, "Flash", "Battery voltage low: %.2fV (min %.1fV)", v, VBAT_LOW_WARN);
        return false;
    }
    return true;
}

void FlashManager::_keepAlive() {
    // Send TesterPresent to keep ECU session alive
    uint8_t req[] = {0x01, 0x3E, 0x00};
    req[2] = KLineDriver::calcChecksum(req, 2);
    uint8_t resp[8];
    size_t len = 0;
    KLine.request(req, 3, resp, len, 500);
}

void FlashManager::_saveCheckpoint() {
    StaticJsonDocument<256> doc;
    doc["opType"]     = (int)_checkpoint.opType;
    doc["lastBlock"]  = _checkpoint.lastBlock;
    doc["totalBlocks"]= _checkpoint.totalBlocks;
    doc["flashSize"]  = _checkpoint.flashSize;
    doc["filename"]   = _checkpoint.filename;
    doc["valid"]      = true;

    String json;
    serializeJson(doc, json);
    FS_Mgr.writeText(String(FS_TEMP_DIR) + "/checkpoint.json", json);
    Logger.log(LOG_INFO, "Flash", "Checkpoint saved: block %d/%d",
               _checkpoint.lastBlock, _checkpoint.totalBlocks);
}

void FlashManager::_loadCheckpoint() {
    String path = String(FS_TEMP_DIR) + "/checkpoint.json";
    if (!FS_Mgr.exists(path)) {
        _checkpoint.valid = false;
        return;
    }

    String json = FS_Mgr.readText(path);
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        _checkpoint.valid = false;
        return;
    }

    _checkpoint.opType     = (FlashOpType)doc["opType"].as<int>();
    _checkpoint.lastBlock  = doc["lastBlock"];
    _checkpoint.totalBlocks= doc["totalBlocks"];
    _checkpoint.flashSize  = doc["flashSize"];
    _checkpoint.filename   = doc["filename"].as<String>();
    _checkpoint.valid      = doc["valid"] | false;
}

void FlashManager::_clearCheckpoint() {
    FS_Mgr.deleteFile(String(FS_TEMP_DIR) + "/checkpoint.json");
    _checkpoint.valid = false;
}

// ============================================================
// Progress & JSON
// ============================================================
void FlashManager::_updateProgress(FlashState state, int pct, const String& msg) {
    _state = state;
    _progress.state   = state;
    _progress.percent = pct;
    _progress.message = msg;
}

void FlashManager::_calcSpeed() {
    uint32_t elapsed = millis() - _opStartTime;
    if (elapsed > 0) {
        _progress.speedBps = (uint32_t)(_progress.bytesDone * 1000UL / elapsed);
        if (_progress.speedBps > 0) {
            uint32_t remaining = _progress.bytesTotal - _progress.bytesDone;
            _progress.etaSeconds = remaining / _progress.speedBps;
        }
    }
}

String FlashManager::stateToString(FlashState s) const {
    const char* names[] = {
        "idle", "auth", "backup", "reading", "erasing",
        "writing", "verifying", "rebooting", "complete", "error", "recovery"
    };
    return (s <= FLASH_RECOVERY) ? names[s] : "unknown";
}

String FlashManager::resultToString(FlashResult r) const {
    const char* names[] = {
        "OK", "Not connected", "Auth failed", "Backup failed",
        "Read failed", "Erase failed", "Write failed", "Verify failed",
        "Checksum error", "Timeout", "Power low", "Invalid data",
        "Recovery failed", "Aborted", "Size mismatch"
    };
    return (r <= FLASH_ERR_SIZE_MISMATCH) ? names[r] : "Unknown error";
}

String FlashManager::progressToJson() const {
    StaticJsonDocument<384> doc;
    doc["state"]      = stateToString(_progress.state);
    doc["stateCode"]  = (int)_progress.state;
    doc["percent"]    = _progress.percent;
    doc["bytesTotal"] = _progress.bytesTotal;
    doc["bytesDone"]  = _progress.bytesDone;
    doc["speedBps"]   = _progress.speedBps;
    doc["eta"]        = _progress.etaSeconds;
    doc["message"]    = _progress.message;
    doc["blocksDone"] = _progress.blocksDone;
    doc["blocksTotal"]= _progress.blocksTotal;
    doc["retries"]    = _progress.retries;
    String out;
    serializeJson(doc, out);
    return out;
}

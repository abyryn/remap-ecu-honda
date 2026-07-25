// ============================================================
// backup.cpp - EEPROM / Flash Backup Utility
// ============================================================
#include "include/backup.h"
#include "include/ecu.h"
#include "include/filesystem.h"
#include "include/logger.h"
#include "include/utils.h"
#include "include/security.h"
#include <ArduinoJson.h>

BackupManager Backup;

BackupManager::BackupManager() : _state(BACKUP_IDLE), _progress(0) {}

bool BackupManager::backupEEPROM(const String& filename,
                                  std::function<void(int, const String&)> onProgress) {
    if (!ECU.isConnected()) {
        Logger.log(LOG_ERROR, "Backup", "ECU not connected");
        return false;
    }

    _state    = BACKUP_READING;
    _progress = 0;

    size_t eepromSize = ECU.getInfo().eepromSize;
    if (eepromSize == 0) eepromSize = 256;

    std::vector<uint8_t> buf(eepromSize);
    Logger.log(LOG_INFO, "Backup", "Reading EEPROM %d bytes...", eepromSize);

    bool ok = ECU.readEEPROM(buf.data(), eepromSize, [&](int pct) {
        _progress = pct / 2;  // reading = 0-50%
        if (onProgress) onProgress(_progress, "Reading EEPROM...");
    });

    if (!ok) {
        _state = BACKUP_ERROR;
        Logger.log(LOG_ERROR, "Backup", "EEPROM read failed");
        return false;
    }

    _state    = BACKUP_SAVING;
    _progress = 50;
    if (onProgress) onProgress(50, "Saving to storage...");

    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    ok = FS_Mgr.writeFile(fullPath, buf.data(), eepromSize);

    if (!ok) {
        _state = BACKUP_ERROR;
        Logger.log(LOG_ERROR, "Backup", "File write failed: %s", fullPath.c_str());
        return false;
    }

    // Write metadata sidecar JSON
    StaticJsonDocument<512> meta;
    meta["filename"]   = filename;
    meta["timestamp"]  = Utils::timestamp();
    meta["size"]       = eepromSize;
    meta["checksum"]   = _calcFileCRC(fullPath);
    meta["model"]      = ECUManager::modelName(ECU.getModel());
    meta["partNumber"] = ECU.getInfo().partNumber;
    meta["calibrationId"] = ECU.getInfo().calibrationId;
    meta["vin"]        = ECU.getInfo().vin;
    String metaJson;
    serializeJson(meta, metaJson);
    FS_Mgr.writeText(String(FS_BACKUP_DIR) + "/" + filename + ".meta", metaJson);

    _state    = BACKUP_DONE;
    _progress = 100;
    if (onProgress) onProgress(100, "Backup complete");

    Logger.log(LOG_INFO, "Backup", "Saved: %s (%d bytes)", fullPath.c_str(), eepromSize);
    return true;
}

bool BackupManager::backupFullFlash(const String& filename,
                                    std::function<void(int, const String&)> onProgress) {
    if (!ECU.isConnected()) {
        Logger.log(LOG_ERROR, "Backup", "ECU not connected");
        return false;
    }

    _state    = BACKUP_READING;
    _progress = 0;

    size_t flashSize = ECU.getInfo().flashSize;
    if (flashSize == 0) flashSize = 262144; // Default to 256KB for backup

    std::vector<uint8_t> buf(flashSize);
    Logger.log(LOG_INFO, "Backup", "Reading Full Flash %d bytes...", flashSize);
    
    // We can use the ECU manager or Flash manager to read it here.
    // For simplicity, we assume we use a similar block read logic to readEEPROM
    bool ok = ECU.readEEPROM(buf.data(), flashSize, [&](int pct) {
        _progress = pct / 2;
        if (onProgress) onProgress(_progress, "Reading Flash...");
    });

    if (!ok) {
        _state = BACKUP_ERROR;
        return false;
    }

    _state    = BACKUP_SAVING;
    _progress = 50;
    if (onProgress) onProgress(50, "Saving to storage...");

    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    ok = FS_Mgr.writeFile(fullPath, buf.data(), flashSize);

    if (!ok) {
        _state = BACKUP_ERROR;
        return false;
    }

    // Write metadata sidecar JSON
    StaticJsonDocument<512> meta;
    meta["filename"]   = filename;
    meta["timestamp"]  = Utils::timestamp();
    meta["size"]       = flashSize;
    meta["checksum"]   = _calcFileCRC(fullPath);
    meta["model"]      = ECUManager::modelName(ECU.getModel());
    meta["partNumber"] = ECU.getInfo().partNumber;
    meta["calibrationId"] = ECU.getInfo().calibrationId;
    meta["vin"]        = ECU.getInfo().vin;
    String metaJson;
    serializeJson(meta, metaJson);
    FS_Mgr.writeText(String(FS_BACKUP_DIR) + "/" + filename + ".meta", metaJson);

    _state    = BACKUP_DONE;
    _progress = 100;
    if (onProgress) onProgress(100, "Full Flash Backup complete");
    return true;
}

bool BackupManager::verifyFile(const String& filename) {
    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    if (!FS_Mgr.exists(fullPath)) {
        Logger.log(LOG_ERROR, "Backup", "File not found: %s", fullPath.c_str());
        return false;
    }

    String metaPath = fullPath + ".meta";
    if (!FS_Mgr.exists(metaPath)) return true;  // no meta = can't verify

    String metaJson = FS_Mgr.readText(metaPath);
    StaticJsonDocument<256> meta;
    deserializeJson(meta, metaJson);

    uint32_t storedCRC  = meta["checksum"].as<uint32_t>();
    uint32_t actualCRC  = _calcFileCRC(fullPath);

    bool ok = (storedCRC == actualCRC);
    Logger.log(ok ? LOG_INFO : LOG_WARN, "Backup",
               "Verify %s: stored=0x%08X actual=0x%08X %s",
               filename.c_str(), storedCRC, actualCRC, ok ? "OK" : "MISMATCH");
    return ok;
}

String BackupManager::listBackupsJson() {
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.createNestedArray("backups");

    auto files = FS_Mgr.listDir(FS_BACKUP_DIR);
    for (auto& f : files) {
        if (f.name.endsWith(".bin")) {
            JsonObject o = arr.createNestedObject();
            o["filename"] = f.name;
            o["size"]     = f.size;
            o["path"]     = f.path;

            // Try to read meta
            String metaPath = f.path + ".meta";
            if (FS_Mgr.exists(metaPath)) {
                String metaJson = FS_Mgr.readText(metaPath);
                StaticJsonDocument<256> meta;
                if (deserializeJson(meta, metaJson) == DeserializationError::Ok) {
                    o["timestamp"]  = meta["timestamp"];
                    o["model"]      = meta["model"];
                    o["partNumber"] = meta["partNumber"];
                    o["checksum"]   = meta["checksum"];
                }
            }
        }
    }
    doc["count"] = arr.size();
    String out;
    serializeJson(doc, out);
    return out;
}

BackupMeta BackupManager::getMetadata(const String& filename) {
    BackupMeta m;
    m.filename = filename;
    String metaPath = String(FS_BACKUP_DIR) + "/" + filename + ".meta";
    if (!FS_Mgr.exists(metaPath)) return m;

    String metaJson = FS_Mgr.readText(metaPath);
    StaticJsonDocument<256> doc;
    deserializeJson(doc, metaJson);
    m.timestamp  = doc["timestamp"].as<String>();
    m.size       = doc["size"].as<size_t>();
    m.checksum   = doc["checksum"].as<uint32_t>();
    m.model      = doc["model"].as<String>();
    m.partNumber = doc["partNumber"].as<String>();
    m.calibrationId = doc["calibrationId"].as<String>();
    m.vin        = doc["vin"].as<String>();
    return m;
}

bool BackupManager::deleteBackup(const String& filename) {
    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    FS_Mgr.deleteFile(fullPath + ".meta");
    return FS_Mgr.deleteFile(fullPath);
}

bool BackupManager::simulateRestore(const String& filename,
                                     std::function<void(int, const String&)> onProgress) {
    if (!ECU.isConnected()) return false;

    String fullPath = String(FS_BACKUP_DIR) + "/" + filename;
    if (!FS_Mgr.exists(fullPath)) return false;

    Logger.log(LOG_INFO, "Backup", "Simulating restore from: %s", filename.c_str());
    if (onProgress) onProgress(0, "Loading file...");

    // Read BIN into buffer
    uint8_t  buf[256];
    size_t   len = sizeof(buf);
    FS_Mgr.readFile(fullPath, buf, len);

    if (onProgress) onProgress(30, "Reading ECU EEPROM...");

    // Read current EEPROM
    uint8_t eeprom[256];
    ECU.readEEPROM(eeprom, len, [&](int pct) {
        if (onProgress) onProgress(30 + pct * 0.5f, "Comparing...");
    });

    // Compare
    int mismatches = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != eeprom[i]) mismatches++;
    }

    Logger.log(LOG_INFO, "Backup", "Restore sim: %d/%d bytes differ", mismatches, len);
    if (onProgress) onProgress(100, String("Done: ") + mismatches + " differences found");
    return true;
}

uint32_t BackupManager::_calcFileCRC(const String& path) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    
    // Read file in chunks and compute CRC32
    size_t totalSize = f.size();
    uint8_t buf[256];
    size_t offset = 0;
    
    // Collect all data for CRC calculation
    // For small files (EEPROM ≤256 bytes), this fits in buf directly
    if (totalSize <= sizeof(buf)) {
        f.read(buf, totalSize);
        f.close();
        return CRC32::calculate(buf, totalSize);
    }
    
    // For larger files, read in chunks into a temporary vector
    std::vector<uint8_t> fileData(totalSize);
    f.read(fileData.data(), totalSize);
    f.close();
    return CRC32::calculate(fileData.data(), totalSize);
}

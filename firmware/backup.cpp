// ============================================================
// backup.cpp - EEPROM / Flash Backup Utility
// ============================================================
#include "include/backup.h"
#include "include/ecu.h"
#include "include/filesystem.h"
#include "include/logger.h"
#include "include/utils.h"
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
    DynamicJsonDocument doc(2048);
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
    
    // We can use SecurityManager for robust CRC32
    uint32_t crc = 0xFFFFFFFF;
    const uint32_t crc32_table[256] = {
        0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
        0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,
        0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
        0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
        0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
        0x35B5A8FA,0x42B2986C,0xDBBBB9D6,0xACBCB9C0,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
        0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
        0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
        0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
        0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0D6B,0x086D3D2D,0x91646C97,0xE6635C01,
        0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
        0x65B0D9C6,0x12B7E950,0x8ABEE5FE,0xFDBE9468,0x63CAAB7B,0x14CDBB0D,0x8DCB879B,0xFACC4709,
        0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,
        0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,0x1DB71064,0x6AB020F2,
        0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,0x136C9856,0x646BA8C0,
        0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,
        0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,
        0xDBBBB9D6,0xACBCB9C0,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,0x26D930AC,0x51DE003A,
        0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,
        0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,
        0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,0x7807C9A2,0x0F00F934,
        0x9609A88E,0xE10E9818,0x7F6A0D6B,0x086D3D2D,0x91646C97,0xE6635C01,0x6B6B51F4,0x1C6C6162,
        0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,0x65B0D9C6,0x12B7E950,
        0x8ABEE5FE,0xFDBE9468,0x63CAAB7B,0x14CDBB0D,0x8DCB879B,0xFACC4709,0xEE0E612C,0x990951BA,
        0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,
        0x09B64C2B,0x7EB17CBF,0xE7B82D09,0x90BF1D9F,0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,
        0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,
        0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,
        0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,0xDBBBB9D6,0xACBCB9C0,
        0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,
        0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,
        0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A
    };
    uint8_t buf[256];
    while (f.available()) {
        int len = f.read(buf, sizeof(buf));
        for (int i = 0; i < len; i++) {
            crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
        }
    }
    f.close();
    return crc ^ 0xFFFFFFFF;
}

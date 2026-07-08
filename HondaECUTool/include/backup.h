#pragma once
// ============================================================
// backup.h - EEPROM / Flash Backup Utility
// ============================================================
#include <Arduino.h>
#include <functional>

enum BackupState {
    BACKUP_IDLE = 0,
    BACKUP_READING,
    BACKUP_SAVING,
    BACKUP_DONE,
    BACKUP_ERROR
};

struct BackupMeta {
    String   filename;
    String   timestamp;
    size_t   size;
    uint8_t  checksum;
    String   model;
    String   partNumber;
};

class BackupManager {
public:
    BackupManager();

    /**
     * @brief Start EEPROM backup — reads from ECU, saves BIN to LittleFS
     * @param filename   Destination filename under /backup/
     * @param onProgress Progress callback 0-100
     */
    bool backupEEPROM(const String& filename,
                      std::function<void(int, const String&)> onProgress = nullptr);

    /**
     * @brief Verify a saved BIN file checksum
     */
    bool verifyFile(const String& filename);

    /**
     * @brief List all backup files as JSON
     */
    String listBackupsJson();

    /**
     * @brief Get metadata for a specific backup file
     */
    BackupMeta getMetadata(const String& filename);

    /**
     * @brief Delete a backup file
     */
    bool deleteBackup(const String& filename);

    /**
     * @brief Simulate restore (compare file vs ECU, no write)
     * @param filename  BIN file to compare
     * @param onProgress Progress callback
     */
    bool simulateRestore(const String& filename,
                         std::function<void(int, const String&)> onProgress = nullptr);

    BackupState getState()    const { return _state; }
    int         getProgress() const { return _progress; }

private:
    BackupState _state;
    int         _progress;

    uint8_t _calcFileCRC(const String& path);
};

extern BackupManager Backup;

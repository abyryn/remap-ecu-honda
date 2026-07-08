#pragma once
// ============================================================
// filesystem.h - LittleFS Abstraction Layer
// ============================================================
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

struct FileInfo {
    String name;
    size_t size;
    String path;
};

class FileSystemClass {
public:
    FileSystemClass();

    /**
     * @brief Mount LittleFS and create required directories
     */
    bool begin();

    /**
     * @brief Write bytes to a file (create or overwrite)
     */
    bool writeFile(const String& path, const uint8_t* data, size_t len);

    /**
     * @brief Write a String to a file
     */
    bool writeText(const String& path, const String& content);

    /**
     * @brief Read a file into a buffer
     */
    bool readFile(const String& path, uint8_t* buf, size_t& len);

    /**
     * @brief Read a text file into a String
     */
    String readText(const String& path);

    /**
     * @brief Append bytes to an existing file
     */
    bool appendFile(const String& path, const uint8_t* data, size_t len);

    /**
     * @brief Append a line of text
     */
    bool appendLine(const String& path, const String& line);

    /**
     * @brief Delete a file
     */
    bool deleteFile(const String& path);

    /**
     * @brief Check if a file exists
     */
    bool exists(const String& path);

    /**
     * @brief List files in a directory
     */
    std::vector<FileInfo> listDir(const String& path);

    /**
     * @brief Get list of files as JSON
     */
    String listDirJson(const String& path);

    /**
     * @brief Get total and used space in bytes
     */
    void getSpaceInfo(size_t& total, size_t& used);

    /**
     * @brief Format filesystem (destructive!)
     */
    bool format();

    bool isMounted() const { return _mounted; }

private:
    bool _mounted;
    void _ensureDir(const String& path);
};

extern FileSystemClass FS_Mgr;

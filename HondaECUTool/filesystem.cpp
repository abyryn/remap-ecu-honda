// ============================================================
// filesystem.cpp - LittleFS Abstraction
// ============================================================
#include "include/filesystem.h"
#include "include/config.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

FileSystemClass FS_Mgr;

FileSystemClass::FileSystemClass() : _mounted(false) {}

bool FileSystemClass::begin() {
    bool res = LittleFS.begin(true); // true = format on fail

    if (!res) {
        Serial.println("[FS] LittleFS mount failed");
        return false;
    }
    _mounted = true;
    // Ensure required directories exist by creating placeholder files
    _ensureDir(FS_BACKUP_DIR);
    _ensureDir(FS_LOG_DIR);
    _ensureDir(FS_CONFIG_DIR);
    _ensureDir(FS_TEMP_DIR);
    Serial.println("[FS] LittleFS mounted OK");
    return true;
}

bool FileSystemClass::writeFile(const String& path, const uint8_t* data, size_t len) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.write(data, len);
    f.close();
    return true;
}

bool FileSystemClass::writeText(const String& path, const String& content) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

bool FileSystemClass::readFile(const String& path, uint8_t* buf, size_t& len) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    len = f.read(buf, len);
    f.close();
    return true;
}

String FileSystemClass::readText(const String& path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

bool FileSystemClass::appendFile(const String& path, const uint8_t* data, size_t len) {
    File f = LittleFS.open(path, "a");
    if (!f) return false;
    f.write(data, len);
    f.close();
    return true;
}

bool FileSystemClass::appendLine(const String& path, const String& line) {
    File f = LittleFS.open(path, "a");
    if (!f) return false;
    f.println(line);
    f.close();
    return true;
}

bool FileSystemClass::deleteFile(const String& path) {
    return LittleFS.remove(path);
}

bool FileSystemClass::exists(const String& path) {
    return LittleFS.exists(path);
}

std::vector<FileInfo> FileSystemClass::listDir(const String& path) {
    std::vector<FileInfo> files;
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) return files;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            FileInfo fi;
            fi.name = String(entry.name());
            fi.size = entry.size();
            fi.path = path + "/" + fi.name;
            files.push_back(fi);
        }
        entry = dir.openNextFile();
    }
    return files;
}

String FileSystemClass::listDirJson(const String& path) {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("files");

    auto files = listDir(path);
    for (auto& f : files) {
        JsonObject o = arr.createNestedObject();
        o["name"] = f.name;
        o["size"] = f.size;
        o["path"] = f.path;
    }
    doc["count"] = files.size();
    doc["path"]  = path;
    String out;
    serializeJson(doc, out);
    return out;
}

void FileSystemClass::getSpaceInfo(size_t& total, size_t& used) {
    total = LittleFS.totalBytes();
    used  = LittleFS.usedBytes();
}

bool FileSystemClass::format() {
    _mounted = false;
    return LittleFS.format();
}

void FileSystemClass::_ensureDir(const String& path) {
    // LittleFS is flat; create a .keep file to represent the directory
    String keepFile = path + "/.keep";
    if (!LittleFS.exists(keepFile)) {
        File f = LittleFS.open(keepFile, "w");
        if (f) f.close();
    }
}

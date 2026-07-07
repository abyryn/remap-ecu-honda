#pragma once
// ============================================================
// api.h - REST API Handler
// ============================================================
#include <ESPAsyncWebServer.h>

class APIHandler {
public:
    /**
     * @brief Register all REST API routes on the given server
     */
    void registerRoutes(AsyncWebServer& server);

private:
    // --- GET handlers ---
    void handleStatus(AsyncWebServerRequest* req);
    void handleInfo(AsyncWebServerRequest* req);
    void handleLive(AsyncWebServerRequest* req);
    void handleDTC(AsyncWebServerRequest* req);
    void handleLog(AsyncWebServerRequest* req);
    void handleFiles(AsyncWebServerRequest* req);
    void handleSettings(AsyncWebServerRequest* req);

    // --- POST handlers ---
    void handleConnect(AsyncWebServerRequest* req);
    void handleDisconnect(AsyncWebServerRequest* req);
    void handleReadId(AsyncWebServerRequest* req);
    void handleReadDTC(AsyncWebServerRequest* req);
    void handleClearDTC(AsyncWebServerRequest* req);
    void handleStartLog(AsyncWebServerRequest* req);
    void handleStopLog(AsyncWebServerRequest* req);
    void handleBackup(AsyncWebServerRequest* req,
                      uint8_t* data, size_t len, size_t index, size_t total);
    void handleRestore(AsyncWebServerRequest* req,
                       uint8_t* data, size_t len, size_t index, size_t total);
    void handleReboot(AsyncWebServerRequest* req);
    void handleUpdateSettings(AsyncWebServerRequest* req,
                              uint8_t* data, size_t len, size_t index, size_t total);
    void handleSendKLine(AsyncWebServerRequest* req,
                         uint8_t* data, size_t len, size_t index, size_t total);
    void handleSetModel(AsyncWebServerRequest* req,
                        uint8_t* data, size_t len, size_t index, size_t total);
    void handleDeleteBackup(AsyncWebServerRequest* req);
    void handleExportLog(AsyncWebServerRequest* req);

    // OTA upload
    void handleOTAUpload(AsyncWebServerRequest* req,
                         const String& filename, size_t index,
                         uint8_t* data, size_t len, bool final);

    // Auth check
    bool _checkAuth(AsyncWebServerRequest* req);

    // JSON response helpers
    static void sendOK(AsyncWebServerRequest* req, const String& json = "{}");
    static void sendError(AsyncWebServerRequest* req, int code, const String& msg);
    static void sendJson(AsyncWebServerRequest* req, int code, const String& json);
};

extern APIHandler API;

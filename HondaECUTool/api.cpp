// ============================================================
// api.cpp - REST API Implementation
// ============================================================
#include "include/api.h"
#include "include/ecu.h"
#include "include/kline.h"
#include "include/logger.h"
#include "include/backup.h"
#include "include/filesystem.h"
#include "include/ota.h"
#include "include/settings.h"
#include "include/utils.h"
#include "include/config.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

APIHandler API;

// ============================================================
// Helpers
// ============================================================
void APIHandler::sendJson(AsyncWebServerRequest* req, int code, const String& json) {
    req->send(code, "application/json", json);
}

void APIHandler::sendOK(AsyncWebServerRequest* req, const String& json) {
    sendJson(req, 200, json);
}

void APIHandler::sendError(AsyncWebServerRequest* req, int code, const String& msg) {
    StaticJsonDocument<128> doc;
    doc["error"] = msg;
    String out;
    serializeJson(doc, out);
    sendJson(req, code, out);
}

bool APIHandler::_checkAuth(AsyncWebServerRequest* req) {
    if (!req->authenticate(Settings.get().authUsername.c_str(),
                           Settings.get().authPassword.c_str())) {
        req->requestAuthentication();
        return false;
    }
    return true;
}

// ============================================================
// registerRoutes
// ============================================================
void APIHandler::registerRoutes(AsyncWebServer& server) {

    // ---- GET /api/status ----
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleStatus(req);
    });

    // ---- GET /api/info ----
    server.on("/api/info", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleInfo(req);
    });

    // ---- GET /api/live ----
    server.on("/api/live", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleLive(req);
    });

    // ---- GET /api/dtc ----
    server.on("/api/dtc", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleDTC(req);
    });

    // ---- GET /api/log ----
    server.on("/api/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleLog(req);
    });

    // ---- GET /api/log/export ----
    server.on("/api/log/export", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleExportLog(req);
    });

    // ---- GET /api/files ----
    server.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleFiles(req);
    });

    // ---- GET /api/settings ----
    server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleSettings(req);
    });

    // ---- POST /api/connect ----
    server.on("/api/connect", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleConnect(req);
    });

    // ---- POST /api/disconnect ----
    server.on("/api/disconnect", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleDisconnect(req);
    });

    // ---- POST /api/read-id ----
    server.on("/api/read-id", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleReadId(req);
    });

    // ---- POST /api/read-dtc ----
    server.on("/api/read-dtc", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleReadDTC(req);
    });

    // ---- POST /api/clear-dtc ----
    server.on("/api/clear-dtc", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (!_checkAuth(req)) return;
        handleClearDTC(req);
    });

    // ---- POST /api/start-log ----
    server.on("/api/start-log", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleStartLog(req);
    });

    // ---- POST /api/stop-log ----
    server.on("/api/stop-log", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleStopLog(req);
    });

    // ---- POST /api/backup ----
    server.on("/api/backup", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            // Final handler — nothing to do here, body handler does the work
        },
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t idx, size_t tot) {
            if (!_checkAuth(req)) return;
            handleBackup(req, data, len, idx, tot);
        }
    );

    // ---- POST /api/restore ----
    server.on("/api/restore", HTTP_POST,
        [this](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t idx, size_t tot) {
            if (!_checkAuth(req)) return;
            handleRestore(req, data, len, idx, tot);
        }
    );

    // ---- POST /api/reboot ----
    server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (!_checkAuth(req)) return;
        handleReboot(req);
    });

    // ---- POST /api/settings ----
    server.on("/api/settings", HTTP_POST,
        [this](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t idx, size_t tot) {
            if (!_checkAuth(req)) return;
            handleUpdateSettings(req, data, len, idx, tot);
        }
    );

    // ---- POST /api/kline-send ----
    server.on("/api/kline-send", HTTP_POST,
        [this](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t idx, size_t tot) {
            handleSendKLine(req, data, len, idx, tot);
        }
    );

    // ---- POST /api/set-model ----
    server.on("/api/set-model", HTTP_POST,
        [this](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t idx, size_t tot) {
            handleSetModel(req, data, len, idx, tot);
        }
    );

    // ---- DELETE /api/backup ----
    server.on("/api/backup", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        if (!_checkAuth(req)) return;
        handleDeleteBackup(req);
    });

    // ---- POST /api/ota ----
    server.on("/api/ota", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            if (OTA.getState() == OTA_DONE || OTA.getState() == OTA_ERROR) {
                sendJson(req, 200, OTA.statusJson());
            }
        },
        [this](AsyncWebServerRequest* req, const String& filename, size_t idx,
               uint8_t* data, size_t len, bool final) {
            if (!_checkAuth(req)) return;
            handleOTAUpload(req, filename, idx, data, len, final);
        }
    );

    Logger.log(LOG_INFO, "API", "All routes registered");
}

// ============================================================
// GET Handlers
// ============================================================
void APIHandler::handleStatus(AsyncWebServerRequest* req) {
    StaticJsonDocument<512> doc;
    doc["device"]      = DEVICE_NAME;
    doc["version"]     = FW_VERSION;
    doc["uptime"]      = millis();
    doc["ecuState"]    = (int)ECU.getState();
    doc["ecuConnected"]= ECU.isConnected();
    doc["battVoltage"] = Utils::readVoltage(VBAT_PIN);
    doc["freeHeap"]    = ESP.getFreeHeap();

    size_t fsTotal = 0, fsUsed = 0;
    FS_Mgr.getSpaceInfo(fsTotal, fsUsed);
    doc["fsTotal"] = fsTotal;
    doc["fsUsed"]  = fsUsed;
    doc["cpuTemp"] = Utils::cpuTemperature();

    String out;
    serializeJson(doc, out);
    sendOK(req, out);
}

void APIHandler::handleInfo(AsyncWebServerRequest* req) {
    if (!ECU.isConnected()) {
        sendError(req, 503, "ECU not connected");
        return;
    }
    sendOK(req, ECU.ecuInfoToJson());
}

void APIHandler::handleLive(AsyncWebServerRequest* req) {
    if (!ECU.isConnected()) {
        sendError(req, 503, "ECU not connected");
        return;
    }
    sendOK(req, ECU.liveDataToJson());
}

void APIHandler::handleDTC(AsyncWebServerRequest* req) {
    sendOK(req, ECU.dtcToJson());
}

void APIHandler::handleLog(AsyncWebServerRequest* req) {
    size_t count = 50;
    if (req->hasParam("count")) {
        count = req->getParam("count")->value().toInt();
    }
    sendOK(req, Logger.getLogsJson(count));
}

void APIHandler::handleExportLog(AsyncWebServerRequest* req) {
    String csv = Logger.exportCSV();
    AsyncWebServerResponse* resp = req->beginResponse(200, "text/csv", csv);
    resp->addHeader("Content-Disposition", "attachment; filename=session_log.csv");
    req->send(resp);
}

void APIHandler::handleFiles(AsyncWebServerRequest* req) {
    String path = "/backup";
    if (req->hasParam("path")) path = req->getParam("path")->value();
    sendOK(req, FS_Mgr.listDirJson(path));
}

void APIHandler::handleSettings(AsyncWebServerRequest* req) {
    sendOK(req, Settings.toJson());
}

// ============================================================
// POST Handlers
// ============================================================
void APIHandler::handleConnect(AsyncWebServerRequest* req) {
    if (ECU.isConnected()) {
        sendOK(req, "{\"status\":\"already_connected\"}");
        return;
    }
    bool ok = ECU.connect();
    if (ok) {
        ECU.readIdentification();
        sendOK(req, "{\"status\":\"connected\"}");
    } else {
        sendError(req, 500, "ECU connection failed");
    }
}

void APIHandler::handleDisconnect(AsyncWebServerRequest* req) {
    ECU.disconnect();
    sendOK(req, "{\"status\":\"disconnected\"}");
}

void APIHandler::handleReadId(AsyncWebServerRequest* req) {
    if (!ECU.isConnected()) { sendError(req, 503, "ECU not connected"); return; }
    bool ok = ECU.readIdentification();
    if (ok) sendOK(req, ECU.ecuInfoToJson());
    else    sendError(req, 500, "Read ID failed");
}

void APIHandler::handleReadDTC(AsyncWebServerRequest* req) {
    if (!ECU.isConnected()) { sendError(req, 503, "ECU not connected"); return; }
    bool ok = ECU.readDTC();
    if (ok) sendOK(req, ECU.dtcToJson());
    else    sendError(req, 500, "Read DTC failed");
}

void APIHandler::handleClearDTC(AsyncWebServerRequest* req) {
    if (!ECU.isConnected()) { sendError(req, 503, "ECU not connected"); return; }
    bool ok = ECU.clearDTC();
    sendOK(req, ok ? "{\"status\":\"cleared\"}" : "{\"status\":\"failed\"}");
}

void APIHandler::handleStartLog(AsyncWebServerRequest* req) {
    String filename = Utils::makeFilename("log", ".csv");
    String path     = String(FS_LOG_DIR) + "/" + filename;
    Logger.startFileLog(path);
    StaticJsonDocument<128> doc;
    doc["status"] = "logging";
    doc["file"]   = filename;
    String out;
    serializeJson(doc, out);
    sendOK(req, out);
}

void APIHandler::handleStopLog(AsyncWebServerRequest* req) {
    Logger.stopFileLog();
    sendOK(req, "{\"status\":\"stopped\"}");
}

void APIHandler::handleBackup(AsyncWebServerRequest* req,
                               uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        // Parse filename from JSON body
        StaticJsonDocument<128> doc;
        deserializeJson(doc, data, len);
        String filename = doc["filename"] | Utils::makeFilename("eeprom", ".bin");

        bool ok = Backup.backupEEPROM(filename, nullptr);
        if (ok) {
            StaticJsonDocument<128> resp;
            resp["status"]   = "ok";
            resp["filename"] = filename;
            String out;
            serializeJson(resp, out);
            sendOK(req, out);
        } else {
            sendError(req, 500, "Backup failed");
        }
    }
}

void APIHandler::handleRestore(AsyncWebServerRequest* req,
                                uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        StaticJsonDocument<128> doc;
        deserializeJson(doc, data, len);
        String filename = doc["filename"] | "";
        if (filename.isEmpty()) { sendError(req, 400, "filename required"); return; }

        bool ok = Backup.simulateRestore(filename, nullptr);
        sendOK(req, ok ? "{\"status\":\"simulated\"}" : "{\"status\":\"failed\"}");
    }
}

void APIHandler::handleReboot(AsyncWebServerRequest* req) {
    sendOK(req, "{\"status\":\"rebooting\"}");
    delay(500);
    ESP.restart();
}

void APIHandler::handleUpdateSettings(AsyncWebServerRequest* req,
                                       uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        String json = String((char*)data).substring(0, len);
        bool changed = Settings.updateFromJson(json);
        sendOK(req, changed ? "{\"status\":\"saved\"}" : "{\"status\":\"no_change\"}");
    }
}

void APIHandler::handleSendKLine(AsyncWebServerRequest* req,
                                  uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        StaticJsonDocument<256> doc;
        deserializeJson(doc, data, len);
        String hexStr = doc["hex"] | "";
        if (hexStr.isEmpty()) { sendError(req, 400, "hex required"); return; }

        uint8_t buf[64];
        size_t  blen = Utils::fromHexString(hexStr, buf, sizeof(buf));

        if (!KLine.isInitialized()) { sendError(req, 503, "K-Line not initialized"); return; }

        uint8_t resp[128];
        size_t  respLen = 0;
        KLineResult r = KLine.request(buf, blen, resp, respLen, 1000);

        StaticJsonDocument<256> respDoc;
        respDoc["status"] = (r == KLINE_OK) ? "ok" : "error";
        respDoc["code"]   = (int)r;
        respDoc["rx"]     = Utils::toHexString(resp, respLen);
        String out;
        serializeJson(respDoc, out);
        sendOK(req, out);
    }
}

void APIHandler::handleSetModel(AsyncWebServerRequest* req,
                                 uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        StaticJsonDocument<128> doc;
        deserializeJson(doc, data, len);
        int modelId = doc["model"] | 14;  // default HONDA_UNKNOWN
        ECU.setModel((HondaModel)modelId);
        sendOK(req, "{\"status\":\"ok\"}");
    }
}

void APIHandler::handleDeleteBackup(AsyncWebServerRequest* req) {
    if (!req->hasParam("filename")) { sendError(req, 400, "filename required"); return; }
    String filename = req->getParam("filename")->value();
    bool ok = Backup.deleteBackup(filename);
    sendOK(req, ok ? "{\"status\":\"deleted\"}" : "{\"status\":\"failed\"}");
}

// ============================================================
// OTA Upload
// ============================================================
void APIHandler::handleOTAUpload(AsyncWebServerRequest* req,
                                  const String& filename, size_t index,
                                  uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        Logger.log(LOG_INFO, "OTA", "Upload start: %s", filename.c_str());
        size_t contentLen = req->contentLength();
        OTA.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN);
    }

    if (!OTA.write(data, len)) {
        Logger.log(LOG_ERROR, "OTA", "Write chunk failed");
        return;
    }

    if (final) {
        OTA.end();  // triggers reboot
    }
}

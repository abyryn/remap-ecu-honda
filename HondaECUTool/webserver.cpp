// ============================================================
// webserver.cpp - Async Web Server + WebSocket
// ============================================================
#include "include/webserver.h"
#include "include/api.h"
#include "include/ecu.h"
#include "include/logger.h"
#include "include/utils.h"
#include "include/config.h"
#include "include/settings.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

WebServerManager WebSrv;

WebServerManager::WebServerManager()
    : _server(nullptr), _ws(nullptr), _lastBroadcast(0) {}

// ============================================================
void WebServerManager::begin() {
    _server = new AsyncWebServer(HTTP_PORT);
    _ws = new AsyncWebSocket(WS_PATH);

    // --- WebSocket event ---
    _ws->onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                        AwsEventType t, void* a, uint8_t* d, size_t l) {
        _onWsEvent(s, c, t, a, d, l);
    });
    _server->addHandler(_ws);

    // --- CORS headers ---
    _setupCORS();

    // --- Register REST API routes ---
    API.registerRoutes(*_server);

    // --- Static files from LittleFS ---
    _serveStaticFiles();

    // --- 404 fallback ---
    _server->onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_OPTIONS) {
            req->send(200);
            return;
        }
        req->send(404, "text/plain", "Not Found");
    });

    _server->begin();
    Logger.log(LOG_INFO, "WebSrv", "Server started on port %d", HTTP_PORT);
}

// ============================================================
void WebServerManager::loop() {
    if (_ws) _ws->cleanupClients();

    // Broadcast live data at configured interval
    if (ECU.isConnected() &&
        (millis() - _lastBroadcast >= WS_LIVE_INTERVAL_MS)) {
        _lastBroadcast = millis();

        // Update live data from ECU
        ECU.readLiveData();

        // Build WS message
        StaticJsonDocument<640> doc;
        doc["type"] = "live";

        // Embed live data JSON
        StaticJsonDocument<512> liveDoc;
        deserializeJson(liveDoc, ECU.liveDataToJson());
        doc["data"] = liveDoc;

        // Add battery voltage
        doc["vbat"]   = Utils::readVoltage(VBAT_PIN);
        doc["heap"]   = ESP.getFreeHeap();
        doc["uptime"] = millis();

        String out;
        serializeJson(doc, out);
        broadcastWS(out);
    }
}

// ============================================================
void WebServerManager::broadcastWS(const String& json) {
    if (_ws && _ws->count() > 0) {
        _ws->textAll(json);
    }
}

// ============================================================
void WebServerManager::broadcastProgress(const String& type, int pct, const String& msg) {
    StaticJsonDocument<128> doc;
    doc["type"]    = type;
    doc["progress"]= pct;
    doc["message"] = msg;
    String out;
    serializeJson(doc, out);
    broadcastWS(out);
}

// ============================================================
void WebServerManager::_onWsEvent(AsyncWebSocket* server,
                                   AsyncWebSocketClient* client,
                                   AwsEventType type,
                                   void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Logger.log(LOG_INFO, "WS", "Client #%u connected from %s",
                       client->id(), client->remoteIP().toString().c_str());
            // Send current status immediately on connect
            {
                StaticJsonDocument<256> doc;
                doc["type"]    = "status";
                doc["connected"] = ECU.isConnected();
                doc["version"] = FW_VERSION;
                String out;
                serializeJson(doc, out);
                client->text(out);
            }
            break;

        case WS_EVT_DISCONNECT:
            Logger.log(LOG_INFO, "WS", "Client #%u disconnected", client->id());
            break;

        case WS_EVT_DATA:
            _handleWsMessage(client, data, len);
            break;

        case WS_EVT_ERROR:
            Logger.log(LOG_WARN, "WS", "Error from client #%u", client->id());
            break;

        default:
            break;
    }
}

// ============================================================
void WebServerManager::_handleWsMessage(AsyncWebSocketClient* client,
                                          const uint8_t* data, size_t len) {
    String msg = String((const char*)data).substring(0, len);
    Logger.log(LOG_DEBUG, "WS", "Received: %s", msg.c_str());

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

    String cmd = doc["cmd"] | "";

    if (cmd == "ping") {
        client->text("{\"type\":\"pong\"}");
    }
    else if (cmd == "status") {
        StaticJsonDocument<256> resp;
        resp["type"]      = "status";
        resp["connected"] = ECU.isConnected();
        resp["ecuState"]  = (int)ECU.getState();
        String out;
        serializeJson(resp, out);
        client->text(out);
    }
    else if (cmd == "live") {
        client->text(ECU.liveDataToJson());
    }
    else if (cmd == "log") {
        client->text(Logger.getLogsJson(20));
    }
}

// ============================================================
void WebServerManager::_serveStaticFiles() {
    // Serve all HTML pages
    _server->serveStatic("/", LittleFS, "/web/").setDefaultFile("index.html");
    _server->serveStatic("/css/", LittleFS, "/web/css/");
    _server->serveStatic("/js/",  LittleFS, "/web/js/");

    // Serve backup BIN file downloads
    _server->on("/download", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("file")) {
            req->send(400, "text/plain", "file param required");
            return;
        }
        String filename = req->getParam("file")->value();
        String path     = String(FS_BACKUP_DIR) + "/" + filename;
        if (!LittleFS.exists(path)) {
            req->send(404, "text/plain", "File not found");
            return;
        }
        req->send(LittleFS, path, "application/octet-stream", true);
    });
}

// ============================================================
void WebServerManager::_setupCORS() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

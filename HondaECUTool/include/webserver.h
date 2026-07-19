#pragma once
// ============================================================
// webserver.h - Async Web Server + WebSocket Manager
// ============================================================
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>

class WebServerManager {
public:
    WebServerManager();

    /**
     * @brief Initialize and start web server + WebSocket
     */
    void begin();

    /**
     * @brief Call in main loop — broadcasts live data via WebSocket
     */
    void loop();

    /**
     * @brief Broadcast a JSON string to all WebSocket clients
     */
    void broadcastWS(const String& json);

    /**
     * @brief Broadcast a progress update
     */
    void broadcastProgress(const String& type, int pct, const String& msg = "");

    AsyncWebServer& server() { return *_server; }

private:
    AsyncWebServer* _server;
    AsyncWebSocket* _ws;
    uint32_t       _lastBroadcast;

    void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleWsMessage(AsyncWebSocketClient* client,
                           const uint8_t* data, size_t len);
    void _serveStaticFiles();
    void _setupCORS();
};

extern WebServerManager WebSrv;

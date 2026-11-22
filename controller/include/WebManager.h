#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "Config.h"

class WebManager {
public:
    static WebManager& getInstance();

    // Initialize Web Server, WebSockets, and Routes
    void begin();

    // Periodic telemetry broadcasting
    void update();

    // Push notifications to all connected clients
    void broadcastRelayChange(uint8_t relayId, bool state);
    void broadcastDimmerChange(uint8_t channel, uint8_t level, bool state);
    void broadcastTelemetry();

private:
    WebManager();
    ~WebManager() = default;

    WebManager(const WebManager&) = delete;
    WebManager& operator=(const WebManager&) = delete;

    void setupRoutes();
    void setupWebSocket();
    void handleWebSocketMessage(AsyncWebSocketClient* client, void* arg, uint8_t* data, size_t len);

    // Helpers
    String getFullStatusJson();
    String getFallbackHtml();

    AsyncWebServer server;
    AsyncWebSocket ws;
    uint32_t lastTelemetryBroadcast = 0;

    bool rebootRequested = false;
    uint32_t rebootTime = 0;
};


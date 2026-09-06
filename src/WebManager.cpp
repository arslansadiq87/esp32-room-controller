#include "WebManager.h"
#include "RelayManager.h"
#include "DimmerManager.h"
#include "PowerMonitor.h"
#include "MmWaveManager.h"
#include "Co2Sensor.h"
#include "StorageManager.h"
#include "AutomationManager.h"
#include "AutomationSafetyManager.h"
#include "StatusLed.h"
#include "PowerLogManager.h"
#include "MqttManager.h"
#include <WiFi.h>
#include <Update.h>
#include <Preferences.h>

// Static Channel Custom Names Cache
static String g_nameFan1 = "Fan 1";
static String g_nameFan2 = "Fan 2";
static String g_nameRelay1 = "Main Light 1";
static String g_nameRelay2 = "Main Light 2";
static String g_nameRelay3 = "Spotlights";
static String g_nameRelay4 = "Wall Socket 1";
static String g_nameRelay5 = "Wall Socket 2";
static String g_nameRelay6 = "Ambient Strip";
static bool g_namesLoaded = false;

static void loadNamesFromNVS() {
    if (g_namesLoaded) return;
    Preferences prefs;
    if (prefs.begin("names", true)) {
        g_nameFan1 = prefs.getString("fan1", "Fan 1");
        g_nameFan2 = prefs.getString("fan2", "Fan 2");
        g_nameRelay1 = prefs.getString("r1", "Main Light 1");
        g_nameRelay2 = prefs.getString("r2", "Main Light 2");
        g_nameRelay3 = prefs.getString("r3", "Spotlights");
        g_nameRelay4 = prefs.getString("r4", "Wall Socket 1");
        g_nameRelay5 = prefs.getString("r5", "Wall Socket 2");
        g_nameRelay6 = prefs.getString("r6", "Ambient Strip");
        prefs.end();
    }
    g_namesLoaded = true;
}

// Static Dashboard Card Order Cache
static String g_cardOrder = "relays,fans,env,power";
static bool g_cardOrderLoaded = false;

static void loadLayoutFromNVS() {
    if (g_cardOrderLoaded) return;
    Preferences prefs;
    if (prefs.begin("layout", true)) {
        g_cardOrder = prefs.getString("order", "relays,fans,env,power");
        prefs.end();
    }
    g_cardOrderLoaded = true;
}

WebManager& WebManager::getInstance() {
    static WebManager instance;
    return instance;
}

WebManager::WebManager() 
    : server(HTTP_SERVER_PORT), ws("/ws") {
}

void WebManager::begin() {
    setupWebSocket();
    setupRoutes();

    server.begin();
    Serial.printf("[WEB] HTTP Server & WebSockets started on port %d\n", HTTP_SERVER_PORT);
}

void WebManager::setupWebSocket() {
    ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        switch (type) {
            case WS_EVT_CONNECT:
                Serial.printf("[WS] Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
                // Send initial full system state to newly connected client
                client->text(getFullStatusJson());
                break;
            case WS_EVT_DISCONNECT:
                Serial.printf("[WS] Client #%u disconnected\n", client->id());
                break;
            case WS_EVT_DATA:
                handleWebSocketMessage(client, arg, data, len);
                break;
            case WS_EVT_PONG:
            case WS_EVT_ERROR:
                break;
        }
    });

    server.addHandler(&ws);
}

void WebManager::handleWebSocketMessage(AsyncWebSocketClient* client, void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) {
            Serial.printf("[WS] JSON Deserialization error: %s\n", error.c_str());
            return;
        }

        const char* type = doc["type"] | "";

        if (strcmp(type, "relay") == 0) {
            uint8_t id = doc["id"] | 0;
            bool state = doc["state"] | false;
            if (id >= 1 && id <= NUM_RELAYS) {
                RelayManager::getInstance().setRelay(id, state, true);
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::RELAY, id);
            }
        } 
        else if (strcmp(type, "relay_toggle") == 0) {
            uint8_t id = doc["id"] | 0;
            if (id >= 1 && id <= NUM_RELAYS) {
                RelayManager::getInstance().toggleRelay(id, true);
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::RELAY, id);
            }
        }
        else if (strcmp(type, "dimmer") == 0) {
            uint8_t id = doc["id"] | 0;
            if (id == 1 || id == 2) {
                if (!doc["level"].isNull()) {
                    uint8_t level = doc["level"];
                    DimmerManager::getInstance().setLevel(id, level, true);
                }
                if (!doc["state"].isNull()) {
                    bool state = doc["state"];
                    DimmerManager::getInstance().setState(id, state);
                }
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, id);
                broadcastDimmerChange(id, DimmerManager::getInstance().getLevel(id), DimmerManager::getInstance().getState(id));
            }
        }
        else if (strcmp(type, "get_status") == 0) {
            client->text(getFullStatusJson());
        }
        else if (strcmp(type, "automation_get") == 0) {
            client->text(AutomationManager::getInstance().toJson());
        }
        else if (strcmp(type, "automation_set") == 0) {
            AutomationManager::getInstance().fromJson(doc);
            ws.textAll(AutomationManager::getInstance().toJson());
        }
        else if (strcmp(type, "apply_preset") == 0) {
            uint8_t p = doc["preset"] | 0;
            AutomationManager::getInstance().applyPreset((AutomationPreset)p);
            ws.textAll(AutomationManager::getInstance().toJson());
        }
        else if (strcmp(type, "safety_get") == 0) {
            client->text(AutomationSafetyManager::getInstance().toJson());
        }
        else if (strcmp(type, "safety_set") == 0) {
            AutomationSafetyManager::getInstance().fromJson(doc);
            ws.textAll(AutomationSafetyManager::getInstance().toJson());
        }
        else if (strcmp(type, "clear_override") == 0) {
            if (!doc["id"].isNull() && !doc["device"].isNull()) {
                const char* dev = doc["device"] | "relay";
                uint8_t id = doc["id"] | 1;
                if (strcmp(dev, "fan") == 0) {
                    AutomationSafetyManager::getInstance().clearManualOverride(SafetyDeviceType::FAN, id);
                } else {
                    AutomationSafetyManager::getInstance().clearManualOverride(SafetyDeviceType::RELAY, id);
                }
            } else {
                AutomationSafetyManager::getInstance().clearAllManualOverrides();
            }
            ws.textAll(AutomationSafetyManager::getInstance().toJson());
        }
        else if (strcmp(type, "sync_time") == 0) {
            uint8_t h = doc["hour"] | 12;
            uint8_t m = doc["minute"] | 0;
            AutomationManager::getInstance().syncTime(h, m);
        }
        else if (strcmp(type, "mmwave_led") == 0) {
            if (!doc["status_led"].isNull()) {
                MmWaveManager::getInstance().setStatusLed(doc["status_led"].as<bool>());
            }
            if (!doc["output_led"].isNull()) {
                MmWaveManager::getInstance().setOutputLed(doc["output_led"].as<bool>());
            }
            broadcastTelemetry();
        }
        else if (strcmp(type, "system_led") == 0) {
            if (!doc["status_led"].isNull()) {
                bool on = doc["status_led"].as<bool>();
                StatusLed::getInstance().setState(on ? LedState::SOLID_GREEN : LedState::OFF);
            }
            broadcastTelemetry();
        }
        else if (strcmp(type, "power_log_config") == 0) {
            if (!doc["logging_enabled"].isNull()) {
                PowerLogManager::getInstance().setLoggingEnabled(doc["logging_enabled"].as<bool>());
                broadcastTelemetry();
            }
        }
    }
}

void WebManager::setupRoutes() {
    // REST API - Status Endpoint
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", getFullStatusJson());
    });

    // REST API - Relay Control Endpoint
    server.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            uint8_t id = doc["id"] | 0;
            if (id >= 1 && id <= NUM_RELAYS) {
                if (!doc["state"].isNull()) {
                    bool state = doc["state"];
                    RelayManager::getInstance().setRelay(id, state, true);
                } else {
                    RelayManager::getInstance().toggleRelay(id, true);
                }
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::RELAY, id);
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // REST API - Dimmer Control Endpoint
    server.on("/api/dimmer", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            uint8_t id = doc["id"] | 0;
            if (id == 1 || id == 2) {
                if (!doc["level"].isNull()) {
                    DimmerManager::getInstance().setLevel(id, doc["level"], true);
                }
                if (!doc["state"].isNull()) {
                    DimmerManager::getInstance().setState(id, doc["state"]);
                }
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, id);
                WebManager::getInstance().broadcastDimmerChange(id, DimmerManager::getInstance().getLevel(id), DimmerManager::getInstance().getState(id));
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // REST API - Automation Safety Endpoints (Must be registered BEFORE /api/automation)
    server.on("/api/automation/safety/clear_override", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (!doc["id"].isNull() && !doc["device"].isNull()) {
                const char* dev = doc["device"] | "relay";
                uint8_t id = doc["id"] | 1;
                if (strcmp(dev, "fan") == 0) {
                    AutomationSafetyManager::getInstance().clearManualOverride(SafetyDeviceType::FAN, id);
                } else {
                    AutomationSafetyManager::getInstance().clearManualOverride(SafetyDeviceType::RELAY, id);
                }
            } else {
                AutomationSafetyManager::getInstance().clearAllManualOverrides();
            }
            WebManager::getInstance().ws.textAll(AutomationSafetyManager::getInstance().toJson());
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Override cleared\"}");
            return;
        }
        AutomationSafetyManager::getInstance().clearAllManualOverrides();
        WebManager::getInstance().ws.textAll(AutomationSafetyManager::getInstance().toJson());
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"All overrides cleared\"}");
    });

    server.on("/api/automation/safety", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", AutomationSafetyManager::getInstance().toJson());
    });

    server.on("/api/automation/safety", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            AutomationSafetyManager::getInstance().fromJson(doc);
            WebManager::getInstance().ws.textAll(AutomationSafetyManager::getInstance().toJson());
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Safety settings saved\"}");
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid safety payload\"}");
    });

    server.on("/api/automation/preset", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok && !doc["preset"].isNull()) {
            uint8_t p = doc["preset"].as<uint8_t>();
            AutomationManager::getInstance().applyPreset((AutomationPreset)p);
            WebManager::getInstance().ws.textAll(AutomationManager::getInstance().toJson());
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Preset applied\"}");
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid preset payload\"}");
    });

    // REST API - Automation Settings Endpoints
    server.on("/api/automation", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", AutomationManager::getInstance().toJson());
    });

    server.on("/api/automation", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            AutomationManager::getInstance().fromJson(doc);
            WebManager::getInstance().ws.textAll(AutomationManager::getInstance().toJson());
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Automation settings saved\"}");
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid automation payload\"}");
    });

    // REST API - mmWave LED Control Endpoints
    server.on("/api/mmwave/led", HTTP_GET, [](AsyncWebServerRequest* request) {
        MmWaveData mm = MmWaveManager::getInstance().getData();
        JsonDocument doc;
        doc["status_led"] = mm.statusLed;
        doc["output_led"] = mm.outputLed;
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/mmwave/led", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            bool ok = true;
            if (!doc["status_led"].isNull()) {
                ok &= MmWaveManager::getInstance().setStatusLed(doc["status_led"].as<bool>());
            }
            if (!doc["output_led"].isNull()) {
                ok &= MmWaveManager::getInstance().setOutputLed(doc["output_led"].as<bool>());
            }
            if (ok) {
                WebManager::getInstance().broadcastTelemetry();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"error\":\"failed to set c4002 leds\"}");
            }
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // REST API - System / Controller Status LED Endpoints
    server.on("/api/system/led", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["status_led"] = (StatusLed::getInstance().getState() != LedState::OFF);
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/system/led", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (!doc["status_led"].isNull()) {
                bool on = doc["status_led"].as<bool>();
                StatusLed::getInstance().setState(on ? LedState::SOLID_GREEN : LedState::OFF);
                WebManager::getInstance().broadcastTelemetry();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // REST API - System Reboot Endpoint
    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
        WebManager::getInstance().rebootRequested = true;
        WebManager::getInstance().rebootTime = millis() + 500;
    });

    // REST API - Power History & 24h Logs Endpoint
    server.on("/api/power/history", HTTP_GET, [](AsyncWebServerRequest* request) {
        String history = PowerLogManager::getInstance().getHistoryJson();
        request->send(200, "application/json", history);
    });

    // REST API - Power Logging Configuration Endpoint
    server.on("/api/power/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["logging_enabled"] = PowerLogManager::getInstance().isLoggingEnabled();
        doc["daily_kwh"] = round(PowerLogManager::getInstance().getDailyKwh() * 1000.0f) / 1000.0f;
        doc["monthly_kwh"] = round(PowerLogManager::getInstance().getMonthlyKwh() * 1000.0f) / 1000.0f;
        doc["peak_power"] = round(PowerLogManager::getInstance().getPeakPower24h() * 10.0f) / 10.0f;
        doc["avg_power"] = round(PowerLogManager::getInstance().getAvgPower24h() * 10.0f) / 10.0f;
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/power/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (!doc["logging_enabled"].isNull()) {
                PowerLogManager::getInstance().setLoggingEnabled(doc["logging_enabled"].as<bool>());
                WebManager::getInstance().broadcastTelemetry();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    server.on("/api/power/clear", HTTP_POST, [](AsyncWebServerRequest* request) {
        PowerLogManager::getInstance().clearLogs();
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Power logs reset\"}");
    });

    // REST API - I2C Bus Scan & Sensor Diagnostic Endpoint
    server.on("/api/i2c/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        JsonArray addrs = doc["devices"].to<JsonArray>();
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                char buf[8];
                snprintf(buf, sizeof(buf), "0x%02X", addr);
                addrs.add(buf);
            }
        }
        Co2Data co2 = Co2Sensor::getInstance().getData();
        doc["co2_online"] = co2.isConnected;
        doc["co2_init"] = Co2Sensor::getInstance().isSensorInitialized();
        doc["co2_last_err"] = Co2Sensor::getInstance().getLastError();
        doc["co2_ppm"] = co2.co2Ppm;
        doc["temp"] = co2.temperature;
        doc["humidity"] = co2.humidity;

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // REST API - Device & Channel Custom Names Endpoints
    server.on("/api/names", HTTP_GET, [](AsyncWebServerRequest* request) {
        loadNamesFromNVS();
        JsonDocument doc;
        doc["fan1"] = g_nameFan1;
        doc["fan2"] = g_nameFan2;
        doc["relay1"] = g_nameRelay1;
        doc["relay2"] = g_nameRelay2;
        doc["relay3"] = g_nameRelay3;
        doc["relay4"] = g_nameRelay4;
        doc["relay5"] = g_nameRelay5;
        doc["relay6"] = g_nameRelay6;
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/names", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            Preferences prefs;
            if (prefs.begin("names", false)) {
                if (doc["fan1"].is<const char*>()) { g_nameFan1 = doc["fan1"].as<String>(); prefs.putString("fan1", g_nameFan1); }
                if (doc["fan2"].is<const char*>()) { g_nameFan2 = doc["fan2"].as<String>(); prefs.putString("fan2", g_nameFan2); }
                if (doc["relay1"].is<const char*>()) { g_nameRelay1 = doc["relay1"].as<String>(); prefs.putString("r1", g_nameRelay1); }
                if (doc["relay2"].is<const char*>()) { g_nameRelay2 = doc["relay2"].as<String>(); prefs.putString("r2", g_nameRelay2); }
                if (doc["relay3"].is<const char*>()) { g_nameRelay3 = doc["relay3"].as<String>(); prefs.putString("r3", g_nameRelay3); }
                if (doc["relay4"].is<const char*>()) { g_nameRelay4 = doc["relay4"].as<String>(); prefs.putString("r4", g_nameRelay4); }
                if (doc["relay5"].is<const char*>()) { g_nameRelay5 = doc["relay5"].as<String>(); prefs.putString("r5", g_nameRelay5); }
                if (doc["relay6"].is<const char*>()) { g_nameRelay6 = doc["relay6"].as<String>(); prefs.putString("r6", g_nameRelay6); }
                prefs.end();
            }
            WebManager::getInstance().broadcastTelemetry();
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    });

    // REST API - Dashboard Layout Card Order Endpoints
    server.on("/api/layout", HTTP_GET, [](AsyncWebServerRequest* request) {
        loadLayoutFromNVS();
        JsonDocument doc;
        JsonArray arr = doc["order"].to<JsonArray>();
        int start = 0;
        int comma = g_cardOrder.indexOf(',');
        while (comma != -1) {
            arr.add(g_cardOrder.substring(start, comma));
            start = comma + 1;
            comma = g_cardOrder.indexOf(',', start);
        }
        arr.add(g_cardOrder.substring(start));
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/layout", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            if (doc["order"].is<JsonArray>()) {
                JsonArray arr = doc["order"].as<JsonArray>();
                String newOrder = "";
                for (size_t i = 0; i < arr.size(); i++) {
                    if (i > 0) newOrder += ",";
                    newOrder += arr[i].as<String>();
                }
                g_cardOrder = newOrder;
                Preferences prefs;
                if (prefs.begin("layout", false)) {
                    prefs.putString("order", g_cardOrder);
                    prefs.end();
                }
                WebManager::getInstance().broadcastTelemetry();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    });

    // REST API - Wi-Fi Scan Endpoint
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        int16_t n = WiFi.scanNetworks(false, true);
        JsonDocument doc;
        JsonArray netArr = doc["networks"].to<JsonArray>();
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                JsonObject net = netArr.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
                net["channel"] = WiFi.channel(i);
                net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            WiFi.scanDelete();
        }
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // REST API - Wi-Fi Configuration Endpoint
    server.on("/api/wifi/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            const char* ssid = doc["ssid"] | "";
            const char* pass = doc["pass"] | "";
            bool reconnect = doc["reconnect"] | true;

            if (strlen(ssid) > 0) {
                Preferences prefs;
                if (prefs.begin(NVS_NAMESPACE_WIFI, false)) {
                    prefs.putString("ssid", ssid);
                    prefs.putString("pass", pass);
                    prefs.end();
                    Serial.printf("[WIFI] New credentials saved in NVS: SSID '%s'\n", ssid);
                }

                if (reconnect) {
                    WiFi.begin(ssid, pass);
                }

                request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Wi-Fi credentials saved. Connecting...\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid wifi config\"}");
    });

    // REST API - Home Assistant / MQTT Configuration Endpoints
    server.on("/api/mqtt/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", MqttManager::getInstance().getConfigJson());
    });

    server.on("/api/mqtt/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            bool en = doc["enabled"] | false;
            const char* srv = doc["server"] | "";
            uint16_t port = doc["port"] | DEFAULT_MQTT_PORT;
            const char* usr = doc["user"] | "";
            const char* pwd = doc["pass"] | "";
            const char* nId = doc["node_id"] | DEFAULT_MQTT_NODE_ID;
            const char* pre = doc["disc_pre"] | DEFAULT_MQTT_DISCOVERY_PREFIX;

            MqttManager::getInstance().saveConfig(en, srv, port, usr, pwd, nId, pre);
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"MQTT settings saved\"}");
            return;
        }
        request->send(400, "application/json", "{\"error\":\"invalid mqtt payload\"}");
    });

    server.on("/api/mqtt/discovery", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (MqttManager::getInstance().isConnected()) {
            MqttManager::getInstance().triggerDiscovery();
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Discovery triggered\"}");
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"MQTT not connected\"}");
        }
    });

    // REST API - OTA Firmware & Filesystem Update Endpoint
    server.on("/api/ota/upload", HTTP_POST, 
        [](AsyncWebServerRequest* request) {
            bool hasError = Update.hasError() || !Update.isFinished();
            JsonDocument doc;
            if (!hasError) {
                doc["status"] = "ok";
                doc["message"] = "Firmware updated successfully! Rebooting...";
                WebManager::getInstance().rebootRequested = true;
                WebManager::getInstance().rebootTime = millis() + 1500;
            } else {
                doc["status"] = "error";
                doc["message"] = "OTA Update failed during write.";
                Update.abort();
            }
            String out;
            serializeJson(doc, out);
            request->send(hasError ? 500 : 200, "application/json", out);
        },
        [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA] Starting upload: %s\n", filename.c_str());
                int cmd = U_FLASH;
                if (filename.indexOf("littlefs") >= 0 || filename.indexOf("spiffs") >= 0 || (request->hasParam("target", true) && request->getParam("target", true)->value() == "filesystem")) {
                    cmd = U_SPIFFS;
                    Serial.println("[OTA] Target partition: LittleFS Filesystem");
                } else {
                    Serial.println("[OTA] Target partition: Firmware Flash");
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
                    Update.printError(Serial);
                }
            }

            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }

            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Upload complete: %u bytes written successfully.\n", (uint32_t)(index + len));
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    // LittleFS Static Files with cache control and fallback
    if (StorageManager::getInstance().isMounted() && StorageManager::getInstance().fileExists("/index.html")) {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache, no-store, must-revalidate");
    } else {
        // Fallback webpage served directly from flash memory if LittleFS is not yet uploaded
        server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
            request->send(200, "text/html", getFallbackHtml());
        });
    }

    // 404 Handler
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "404: Not Found");
    });
}

String WebManager::getFullStatusJson() {
    JsonDocument doc;
    doc["type"] = "full_status";

    // 6 Switchable Relays (GPB2..GPB7 / ULN2803A Pins 16..11)
    JsonArray relaysArr = doc["relays"].to<JsonArray>();
    RelayManager& rm = RelayManager::getInstance();
    for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
        JsonObject r = relaysArr.add<JsonObject>();
        r["id"] = i;
        r["state"] = rm.getRelay(i);
        r["sw"] = rm.getSwitchRaw(i + 2); // GPA2..GPA7 correspond to Relays 1..6
    }

    // Fans / Dimmers (Triac 1 & 2 + Fan 1 & 2 Relays on GPB0 & GPB1 / Pins 18 & 17)
    JsonArray dimmersArr = doc["dimmers"].to<JsonArray>();
    DimmerManager& dm = DimmerManager::getInstance();
    for (uint8_t i = 1; i <= 2; i++) {
        JsonObject d = dimmersArr.add<JsonObject>();
        d["id"] = i;
        d["level"] = dm.getLevel(i);
        d["state"] = dm.getState(i);
        d["fan_relay"] = rm.getFanRelay(i);
        d["sw"] = rm.getSwitchRaw(i); // GPA0 for Fan 1, GPA1 for Fan 2
    }

    // Power Telemetry
    PowerData pwr = PowerMonitor::getInstance().getData();
    JsonObject pwrObj = doc["power"].to<JsonObject>();
    bool pwrOnline = (pwr.connectionState == SENSOR_CONNECTED);
    bool acActive = (pwrOnline && pwr.acInputState == AC_PRESENT);
    pwrObj["online"] = pwrOnline;
    pwrObj["ac_present"] = acActive;
    pwrObj["ac_state"] = (!pwrOnline) ? "offline" :
                         (pwr.acInputState == AC_CHECKING) ? "checking" :
                         (pwr.acInputState == AC_PRESENT) ? "present" : "idle";
    pwrObj["v_rms"] = acActive ? (round(pwr.voltageRms * 10.0f) / 10.0f) : -1.0f;
    pwrObj["i_rms"] = acActive ? (round(pwr.currentRms * 100.0f) / 100.0f) : -1.0f;
    pwrObj["p_active"] = acActive ? (round(pwr.activePower * 10.0f) / 10.0f) : 0.0f;
    pwrObj["p_apparent"] = acActive ? (round(pwr.apparentPower * 10.0f) / 10.0f) : 0.0f;
    pwrObj["pf"] = acActive ? (round(pwr.powerFactor * 100.0f) / 100.0f) : 0.0f;
    pwrObj["freq"] = acActive ? pwr.lineFrequency : 0.0f;
    pwrObj["logging"] = PowerLogManager::getInstance().isLoggingEnabled();
    pwrObj["daily_kwh"] = round(PowerLogManager::getInstance().getDailyKwh() * 1000.0f) / 1000.0f;
    pwrObj["monthly_kwh"] = round(PowerLogManager::getInstance().getMonthlyKwh() * 1000.0f) / 1000.0f;
    pwrObj["peak_power"] = round(PowerLogManager::getInstance().getPeakPower24h() * 10.0f) / 10.0f;
    pwrObj["avg_power"] = round(PowerLogManager::getInstance().getAvgPower24h() * 10.0f) / 10.0f;

    // Fermion C4002 mmWave Human Presence Radar
    MmWaveData mm = MmWaveManager::getInstance().getData();
    JsonObject presObj = doc["presence"].to<JsonObject>();
    presObj["state"] = mm.targetState; // 0: None, 1: Static Presence/Breathing, 2: Motion, 3: Motion & Breathing
    presObj["present"] = mm.isPresence;
    presObj["motion"] = mm.isMotion;
    presObj["breathing"] = mm.isBreathing;
    presObj["dist"] = round(mm.distance * 100.0f) / 100.0f;
    presObj["speed"] = round(mm.speed * 100.0f) / 100.0f;
    presObj["energy"] = mm.energy;
    presObj["online"] = mm.isConnected;
    presObj["status_led"] = mm.statusLed;
    presObj["output_led"] = mm.outputLed;

    // Sensirion STCC4 True CO2 & Environmental Data
    Co2Data co2 = Co2Sensor::getInstance().getData();
    JsonObject co2Obj = doc["environment"].to<JsonObject>();
    co2Obj["co2_ppm"] = co2.co2Ppm;
    co2Obj["temp"] = round(co2.temperature * 10.0f) / 10.0f;
    co2Obj["humidity"] = round(co2.humidity * 10.0f) / 10.0f;
    co2Obj["status"] = co2.sensorStatus;
    co2Obj["online"] = co2.isConnected;

    // Custom Channel Names
    loadNamesFromNVS();
    JsonObject namesObj = doc["names"].to<JsonObject>();
    namesObj["fan1"] = g_nameFan1;
    namesObj["fan2"] = g_nameFan2;
    namesObj["relay1"] = g_nameRelay1;
    namesObj["relay2"] = g_nameRelay2;
    namesObj["relay3"] = g_nameRelay3;
    namesObj["relay4"] = g_nameRelay4;
    namesObj["relay5"] = g_nameRelay5;
    namesObj["relay6"] = g_nameRelay6;

    // Card Layout Order
    loadLayoutFromNVS();
    JsonArray orderArr = doc["card_order"].to<JsonArray>();
    int startIdx = 0;
    int commaIdx = g_cardOrder.indexOf(',');
    while (commaIdx != -1) {
        orderArr.add(g_cardOrder.substring(startIdx, commaIdx));
        startIdx = commaIdx + 1;
        commaIdx = g_cardOrder.indexOf(',', startIdx);
    }
    orderArr.add(g_cardOrder.substring(startIdx));

    // System Info
    JsonObject sysObj = doc["system"].to<JsonObject>();
    sysObj["heap"] = ESP.getFreeHeap();
    sysObj["uptime"] = millis() / 1000;
    sysObj["rssi"] = WiFi.RSSI();
    sysObj["ip"] = WiFi.localIP().toString();
    sysObj["ssid"] = WiFi.SSID();
    sysObj["status_led"] = (StatusLed::getInstance().getState() != LedState::OFF);

    // Home Assistant / MQTT Status
    JsonObject mqttObj = doc["mqtt"].to<JsonObject>();
    mqttObj["enabled"] = MqttManager::getInstance().isEnabled();
    mqttObj["connected"] = MqttManager::getInstance().isConnected();
    mqttObj["server"] = MqttManager::getInstance().getServer();

    String output;
    serializeJson(doc, output);
    return output;
}

void WebManager::broadcastRelayChange(uint8_t relayId, bool state) {
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "relay_update";
    doc["id"] = relayId;
    doc["state"] = state;

    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}

void WebManager::broadcastDimmerChange(uint8_t channel, uint8_t level, bool state) {
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "dimmer_update";
    doc["id"] = channel;
    doc["level"] = level;
    doc["state"] = state;
    doc["fan_relay"] = RelayManager::getInstance().getFanRelay(channel);

    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}

void WebManager::broadcastTelemetry() {
    if (ws.count() == 0) return;

    PowerData pwr = PowerMonitor::getInstance().getData();
    MmWaveData mm = MmWaveManager::getInstance().getData();
    Co2Data co2 = Co2Sensor::getInstance().getData();

    JsonDocument doc;
    doc["type"] = "telemetry";

    // Power Telemetry
    bool pwrOnline = (pwr.connectionState == SENSOR_CONNECTED);
    bool acActive = (pwrOnline && pwr.acInputState == AC_PRESENT);
    doc["pwr_online"] = pwrOnline;
    doc["ac_present"] = acActive;
    doc["ac_state"] = (!pwrOnline) ? "offline" :
                      (pwr.acInputState == AC_CHECKING) ? "checking" :
                      (pwr.acInputState == AC_PRESENT) ? "present" : "idle";
    doc["v_rms"] = acActive ? (round(pwr.voltageRms * 10.0f) / 10.0f) : -1.0f;
    doc["i_rms"] = acActive ? (round(pwr.currentRms * 100.0f) / 100.0f) : -1.0f;
    doc["p_active"] = acActive ? (round(pwr.activePower * 10.0f) / 10.0f) : 0.0f;
    doc["p_apparent"] = acActive ? (round(pwr.apparentPower * 10.0f) / 10.0f) : 0.0f;
    doc["pf"] = acActive ? (round(pwr.powerFactor * 100.0f) / 100.0f) : 0.0f;
    doc["freq"] = acActive ? pwr.lineFrequency : 0.0f;
    doc["power_logging"] = PowerLogManager::getInstance().isLoggingEnabled();
    doc["daily_kwh"] = round(PowerLogManager::getInstance().getDailyKwh() * 1000.0f) / 1000.0f;
    doc["monthly_kwh"] = round(PowerLogManager::getInstance().getMonthlyKwh() * 1000.0f) / 1000.0f;
    doc["peak_power"] = round(PowerLogManager::getInstance().getPeakPower24h() * 10.0f) / 10.0f;
    doc["avg_power"] = round(PowerLogManager::getInstance().getAvgPower24h() * 10.0f) / 10.0f;

    // Presence & Breathing
    doc["presence_state"] = mm.targetState;
    doc["presence_breathing"] = mm.isBreathing;
    doc["presence_energy"] = mm.energy;
    doc["presence_dist"] = round(mm.distance * 100.0f) / 100.0f;
    doc["presence_online"] = mm.isConnected;
    doc["radar_status_led"] = mm.statusLed;
    doc["radar_output_led"] = mm.outputLed;
    doc["system_status_led"] = (StatusLed::getInstance().getState() != LedState::OFF);

    // CO2 & Environment
    doc["co2_ppm"] = co2.co2Ppm;
    doc["temp"] = round(co2.temperature * 10.0f) / 10.0f;
    doc["humidity"] = round(co2.humidity * 10.0f) / 10.0f;
    doc["env_online"] = co2.isConnected;

    // Automation Live Telemetry
    AutomationStatus autoSt = AutomationManager::getInstance().getStatus();
    doc["auto_master"] = autoSt.masterEnabled;
    doc["fan1_auto"] = autoSt.fan1AutoActive;
    doc["fan1_demand"] = autoSt.fan1Demand;
    doc["fan1_status"] = autoSt.fan1StatusText;
    doc["fan2_auto"] = autoSt.fan2AutoActive;
    doc["fan2_demand"] = autoSt.fan2Demand;
    doc["fan2_status"] = autoSt.fan2StatusText;
    doc["night_quiet_active"] = autoSt.nightQuietActive;
    doc["air_boost_active"] = autoSt.airBoostActive;
    doc["inactivity_rem"] = autoSt.inactivityRemainingSec;
    doc["auto_preset"] = (uint8_t)autoSt.currentPreset;

    // Automation Safety Live Status
    AutomationSafetyManager& safety = AutomationSafetyManager::getInstance();
    doc["pwr_stabilized"] = safety.isPowerStabilized();
    doc["pwr_rem"] = safety.getStabilizationRemainingSec();
    doc["filtered_temp"] = safety.getFilteredTemp();
    doc["filtered_co2"] = safety.getFilteredCo2();
    doc["temp_valid"] = safety.isTempValid();
    doc["co2_valid"] = safety.isCo2Valid();

    for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
        char kAuto[16], kSt[16], kOv[16];
        snprintf(kAuto, sizeof(kAuto), "relay%u_auto", i);
        snprintf(kSt, sizeof(kSt), "relay%u_auto_st", i);
        snprintf(kOv, sizeof(kOv), "relay%u_ov_rem", i);
        doc[kAuto] = AutomationManager::getInstance().isRelayAutoEnabled(i);
        doc[kSt] = AutomationManager::getInstance().getLightConfig(i).statusDesc;
        doc[kOv] = safety.getManualOverrideRemainingSec(SafetyDeviceType::RELAY, i);
    }

    doc["fan1_ov_rem"] = safety.getManualOverrideRemainingSec(SafetyDeviceType::FAN, 1);
    doc["fan2_ov_rem"] = safety.getManualOverrideRemainingSec(SafetyDeviceType::FAN, 2);

    // System
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["rssi"] = WiFi.RSSI();

    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}

void WebManager::update() {
    ws.cleanupClients();

    uint32_t now = millis();
    if (now - lastTelemetryBroadcast >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryBroadcast = now;
        broadcastTelemetry();
    }

    if (rebootRequested && now >= rebootTime) {
        rebootRequested = false;
        Serial.println("[SYSTEM] Rebooting after OTA update...");
        delay(100);
        ESP.restart();
    }
}

String WebManager::getFallbackHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-C6 Smart Home Controller</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 40px 20px; }
        .card { max-width: 540px; margin: 0 auto; background: #1e293b; padding: 30px; border-radius: 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }
        h1 { color: #38bdf8; font-size: 24px; margin-bottom: 8px; }
        p { color: #94a3b8; font-size: 15px; }
        .btn { display: inline-block; background: #0284c7; color: white; padding: 12px 24px; border-radius: 8px; text-decoration: none; font-weight: bold; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="card">
        <h1>ESP32-C6 Room Controller</h1>
        <p>Firmware is running! Upload the LittleFS data directory (<code>data/index.html</code>, <code>data/style.css</code>, <code>data/app.js</code>) using PlatformIO to view the full interactive dashboard.</p>
        <a href="/api/status" class="btn">View JSON API Status</a>
    </div>
</body>
</html>
)rawliteral";
}

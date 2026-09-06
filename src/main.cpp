#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "Config.h"
#include "StorageManager.h"
#include "StatusLed.h"
#include "RelayManager.h"
#include "DimmerManager.h"
#include "EncoderManager.h"
#include "PowerMonitor.h"
#include "MmWaveManager.h"
#include "Co2Sensor.h"
#include "WebManager.h"
#include "AutomationManager.h"
#include "PowerLogManager.h"
#include "MqttManager.h"
#include <Preferences.h>

// Non-blocking WiFi reconnection tracker
static uint32_t lastWiFiCheck = 0;
static bool wasConnected = false;

void initWiFi() {
    Serial.println("\n[WIFI] Initializing Network Interfaces...");

    // Set WiFi to Dual Mode (AP + Station) for reliable initial configuration and local control
    WiFi.mode(WIFI_AP_STA);

    // Start SoftAP
    WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS);
    Serial.printf("[WIFI] Access Point started: SSID '%s' (IP: %s)\n", 
                  DEFAULT_AP_SSID, WiFi.softAPIP().toString().c_str());

    // Load custom Station credentials from NVS if saved, otherwise use defaults
    String staSsid = DEFAULT_STA_SSID;
    String staPass = DEFAULT_STA_PASS;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_WIFI, true)) {
        String savedSsid = prefs.getString("ssid", "");
        String savedPass = prefs.getString("pass", "");
        if (savedSsid.length() > 0) {
            staSsid = savedSsid;
            staPass = savedPass;
            Serial.printf("[WIFI] Loaded saved credentials from NVS for SSID: '%s'\n", staSsid.c_str());
        }
        prefs.end();
    }

    // Connect to Station if configured
    if (staSsid != "YourWiFiSSID" && staSsid.length() > 0) {
        Serial.printf("[WIFI] Connecting to Station SSID: '%s'...\n", staSsid.c_str());
        WiFi.begin(staSsid.c_str(), staPass.c_str());
    } else {
        Serial.println("[WIFI] Station credentials not configured. Running in standalone AP mode.");
    }

    // Initialize mDNS Responder
    if (MDNS.begin(DEFAULT_MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_SERVER_PORT);
        Serial.printf("[MDNS] Hostname registered: http://%s.local\n", DEFAULT_MDNS_HOSTNAME);
    }
}

void checkWiFiStatus() {
    uint32_t now = millis();
    if (now - lastWiFiCheck < 3000) {
        return;
    }
    lastWiFiCheck = now;

    bool isConnected = (WiFi.status() == WL_CONNECTED);

    if (isConnected && !wasConnected) {
        wasConnected = true;
        StatusLed::getInstance().setState(LedState::SOLID_GREEN);
        Serial.printf("[WIFI] Station Connected! IP: %s | RSSI: %d dBm\n", 
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        // Synchronize real-world time via SNTP
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    } else if (!isConnected && wasConnected) {
        wasConnected = false;
        StatusLed::getInstance().setState(LedState::PULSING_BLUE);
        Serial.println("[WIFI] Station connection lost, retrying...");
    }
}

void setup() {
    // 1. Initialize Serial Communication (Native USB CDC)
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==================================================");
    Serial.println("  ESP32-C6-WROOM-1 Smart Home Room Controller");
    Serial.println("==================================================");

    // 2. Initialize Status LED (WS2812B on GPIO11)
    StatusLed::getInstance().begin();
    StatusLed::getInstance().setState(LedState::PULSING_BLUE);

    // 3. Initialize Shared I2C Bus (SDA = GPIO6, SCL = GPIO7)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQUENCY);
    Serial.printf("[I2C] Bus initialized on SDA: GPIO%d, SCL: GPIO%d @ %u kHz\n", 
                  PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQUENCY / 1000);

    // 4. Initialize LittleFS Storage (External Flash W25Q128)
    StorageManager::getInstance().begin();

    // 5. Initialize MCP23017 (GPA: 8 Wall Switches, GPB: 2 Fan Relays + 6 General Relays)
    RelayManager& relayMgr = RelayManager::getInstance();
    relayMgr.begin(Wire, MCP23017_I2C_ADDR);
    relayMgr.onRelayChange([](uint8_t relayId, bool state) {
        // Broadcast general relay status update to all connected WebSockets and MQTT
        WebManager::getInstance().broadcastRelayChange(relayId, state);
        MqttManager::getInstance().publishRelayState(relayId, state);
    });
    relayMgr.onFanRelayChange([](uint8_t fanId, bool state) {
        // Broadcast fan relay & dimmer status update to all connected WebSockets and MQTT
        DimmerManager& dm = DimmerManager::getInstance();
        WebManager::getInstance().broadcastDimmerChange(fanId, dm.getLevel(fanId), dm.getState(fanId));
        MqttManager::getInstance().publishFanState(fanId, dm.getState(fanId), dm.getLevel(fanId));
    });

    // 6. Initialize AC Zero-Cross & Triac Phase-Cut Dimmers
    DimmerManager::getInstance().begin();

    // 7. Initialize Dual Rotary Encoders & Pushbuttons
    EncoderManager& encoderMgr = EncoderManager::getInstance();
    encoderMgr.begin();
    encoderMgr.onEncoderEvent([](uint8_t encId, uint8_t level, bool state) {
        // Broadcast dimmer status update to all connected WebSockets and MQTT
        WebManager::getInstance().broadcastDimmerChange(encId, level, state);
        MqttManager::getInstance().publishFanState(encId, state, level);
    });

    // 8. Initialize ACS37800 Power & Energy Monitor
    PowerMonitor::getInstance().begin(Wire, ACS37800_I2C_ADDR);

    // 8.1 Initialize Power Consumption Logging (Winbond LittleFS circular buffer)
    PowerLogManager::getInstance().begin();

    // 9. Initialize Fermion C4002 mmWave Human Presence Sensor (UART0 on GPIO16 TX, GPIO17 RX)
    MmWaveManager::getInstance().begin(Serial0, C4002_BAUDRATE, PIN_C4002_RX, PIN_C4002_TX);

    // 10. Initialize Sensirion STCC4 True CO2 Sensor (I2C on GPIO6 SDA, GPIO7 SCL)
    Co2Sensor::getInstance().begin(Wire, STCC4_I2C_ADDR);

    // 11. Initialize Automation Rules Manager (NVS Storage)
    AutomationManager::getInstance().begin();

    // 12. Initialize WiFi and mDNS
    initWiFi();

    // 13. Start Async Web Server and WebSockets
    WebManager::getInstance().begin();

    // 14. Initialize Home Assistant MQTT Auto-Discovery Client
    MqttManager::getInstance().begin();

    Serial.println("[SYSTEM] System initialization complete. Entering main control loop.");
}

void loop() {
    // Non-blocking subsystem updates
    RelayManager::getInstance().update();     // Debounces wall switches & tracks state transitions
    EncoderManager::getInstance().update();   // Handles encoder pushbutton debouncing & clicks
    DimmerManager::getInstance().update();     // Handles smooth dimmer ramp fading
    PowerMonitor::getInstance().update();      // Reads telemetry registers from ACS37800
    PowerLogManager::getInstance().update();   // High-rate energy integration & Winbond interval log
    MmWaveManager::getInstance().update();     // Reads mmWave human presence radar packets
    Co2Sensor::getInstance().update();         // Reads Sensirion STCC4 CO2, Temp & Humidity
    AutomationManager::getInstance().update(); // Processes presence occupancy automation timers
    StatusLed::getInstance().update();         // Advances WS2812B animation frame
    WebManager::getInstance().update();        // Pushes real-time telemetry over WebSockets
    MqttManager::getInstance().update();       // Home Assistant MQTT connection & telemetry loop

    // Periodic network check
    checkWiFiStatus();

    // Yield to FreeRTOS scheduler
    yield();
}

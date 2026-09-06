#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"

class MqttManager {
public:
    static MqttManager& getInstance();

    // Initialize MQTT client, load credentials from NVS
    void begin();

    // Periodic loop to handle connection state, subscriptions, and telemetry
    void update();

    // State reporting (called when relays or fans change locally or remotely)
    void publishRelayState(uint8_t relayId, bool state);
    void publishFanState(uint8_t fanId, bool state, uint8_t level);

    // Periodic telemetry publisher (Power, Climate, mmWave)
    void publishAllTelemetry();

    // Home Assistant Auto-Discovery
    void publishDiscovery();
    void triggerDiscovery();

    // Configuration management
    void saveConfig(bool enabled, const String& server, uint16_t port, 
                    const String& user, const String& pass, 
                    const String& nodeId, const String& discoveryPrefix);
    String getConfigJson();

    // Status inspection
    bool isConnected();
    bool isEnabled() const { return enabled; }
    String getServer() const { return server; }
    uint16_t getPort() const { return port; }
    String getUser() const { return user; }
    String getNodeId() const { return nodeId; }
    String getDiscoveryPrefix() const { return discoveryPrefix; }

private:
    MqttManager();
    ~MqttManager() = default;

    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;

    void loadConfig();
    bool connect();
    void subscribeTopics();
    void handleMessage(char* topic, uint8_t* payload, unsigned int length);
    static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);

    // Helpers to publish discovery entity configs
    void publishSwitchDiscovery(uint8_t relayId, const String& name);
    void publishFanDiscovery(uint8_t fanId, const String& name);
    void publishPowerSensorDiscovery();
    void publishClimateSensorDiscovery();
    void publishPresenceDiscovery();

    // Network & MQTT objects
    WiFiClient wifiClient;
    PubSubClient mqttClient;

    // Settings
    bool enabled = false;
    String server = "";
    uint16_t port = DEFAULT_MQTT_PORT;
    String user = "";
    String pass = "";
    String nodeId = DEFAULT_MQTT_NODE_ID;
    String discoveryPrefix = DEFAULT_MQTT_DISCOVERY_PREFIX;

    // State variables
    uint32_t lastReconnectAttempt = 0;
    uint32_t lastTelemetryPublish = 0;
    bool discoveryPending = false;
    uint32_t discoverySendTime = 0;
};

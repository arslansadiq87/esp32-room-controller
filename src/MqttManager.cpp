#include "MqttManager.h"
#include "RelayManager.h"
#include "DimmerManager.h"
#include "PowerMonitor.h"
#include "Co2Sensor.h"
#include "MmWaveManager.h"
#include "PowerLogManager.h"
#include "AutomationSafetyManager.h"
#include "WebManager.h"
#include <Preferences.h>

MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager() : mqttClient(wifiClient) {
}

void MqttManager::begin() {
    loadConfig();
    mqttClient.setBufferSize(1536);
    mqttClient.setCallback(mqttCallback);

    if (server.length() > 0 && port > 0) {
        mqttClient.setServer(server.c_str(), port);
    }
    Serial.printf("[MQTT] Initialized. Enabled: %s, Server: %s:%u, Node ID: %s\n",
                  enabled ? "YES" : "NO", server.c_str(), port, nodeId.c_str());
}

void MqttManager::loadConfig() {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_MQTT, true)) {
        enabled = prefs.getBool("enabled", false);
        server = prefs.getString("server", "");
        port = prefs.getUShort("port", DEFAULT_MQTT_PORT);
        user = prefs.getString("user", "");
        pass = prefs.getString("pass", "");
        nodeId = prefs.getString("node_id", DEFAULT_MQTT_NODE_ID);
        discoveryPrefix = prefs.getString("disc_pre", DEFAULT_MQTT_DISCOVERY_PREFIX);
        prefs.end();
    }
    if (port == 0) port = DEFAULT_MQTT_PORT;
    if (nodeId.length() == 0) nodeId = DEFAULT_MQTT_NODE_ID;
    if (discoveryPrefix.length() == 0) discoveryPrefix = DEFAULT_MQTT_DISCOVERY_PREFIX;
}

void MqttManager::saveConfig(bool en, const String& srv, uint16_t p, 
                             const String& usr, const String& pwd, 
                             const String& nId, const String& prefix) {
    enabled = en;
    server = srv;
    port = (p > 0) ? p : DEFAULT_MQTT_PORT;
    user = usr;
    pass = pwd;
    nodeId = (nId.length() > 0) ? nId : DEFAULT_MQTT_NODE_ID;
    discoveryPrefix = (prefix.length() > 0) ? prefix : DEFAULT_MQTT_DISCOVERY_PREFIX;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_MQTT, false)) {
        prefs.putBool("enabled", enabled);
        prefs.putString("server", server);
        prefs.putUShort("port", port);
        prefs.putString("user", user);
        prefs.putString("pass", pass);
        prefs.putString("node_id", nodeId);
        prefs.putString("disc_pre", discoveryPrefix);
        prefs.end();
    }

    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    if (server.length() > 0) {
        mqttClient.setServer(server.c_str(), port);
    }
    lastReconnectAttempt = 0;
    Serial.printf("[MQTT] Config updated. Reconnecting to %s:%u...\n", server.c_str(), port);
}

String MqttManager::getConfigJson() {
    JsonDocument doc;
    doc["enabled"] = enabled;
    doc["server"] = server;
    doc["port"] = port;
    doc["user"] = user;
    doc["pass"] = pass;
    doc["node_id"] = nodeId;
    doc["disc_pre"] = discoveryPrefix;
    doc["connected"] = isConnected();

    String out;
    serializeJson(doc, out);
    return out;
}

bool MqttManager::isConnected() {
    return mqttClient.connected();
}

void MqttManager::triggerDiscovery() {
    discoveryPending = true;
    discoverySendTime = millis() + 600; // Small delay to let subscriptions settle
}

void MqttManager::update() {
    if (!enabled || server.length() == 0) {
        if (mqttClient.connected()) {
            mqttClient.disconnect();
        }
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();

    if (!mqttClient.connected()) {
        if (now - lastReconnectAttempt >= 5000) {
            lastReconnectAttempt = now;
            if (connect()) {
                lastReconnectAttempt = 0;
            }
        }
        return;
    }

    // Process incoming packets & keepalive
    mqttClient.loop();

    // Trigger discovery if queued
    if (discoveryPending && now >= discoverySendTime) {
        discoveryPending = false;
        publishDiscovery();
    }

    // Periodic telemetry publishing (every 3 seconds)
    if (now - lastTelemetryPublish >= 3000) {
        lastTelemetryPublish = now;
        publishAllTelemetry();
    }
}

bool MqttManager::connect() {
    Serial.printf("[MQTT] Connecting to broker %s:%u...\n", server.c_str(), port);

    // Form Unique Client ID from MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char clientId[40];
    snprintf(clientId, sizeof(clientId), "%s_%02X%02X%02X", nodeId.c_str(), mac[3], mac[4], mac[5]);

    // Setup LWT (Last Will and Testament)
    String lwtTopic = nodeId + "/status";

    bool success = false;
    if (user.length() > 0) {
        success = mqttClient.connect(clientId, user.c_str(), pass.c_str(), lwtTopic.c_str(), 1, true, "offline");
    } else {
        success = mqttClient.connect(clientId, lwtTopic.c_str(), 1, true, "offline");
    }

    if (success) {
        Serial.printf("[MQTT] Connected successfully as '%s'!\n", clientId);
        // Publish device birth message
        mqttClient.publish(lwtTopic.c_str(), "online", true);

        // Subscribe to command topics
        subscribeTopics();

        // Queue Home Assistant Auto-Discovery
        triggerDiscovery();

        // Publish immediate states for all 6 relays & 2 fans
        RelayManager& rm = RelayManager::getInstance();
        for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
            publishRelayState(i, rm.getRelay(i));
        }
        DimmerManager& dm = DimmerManager::getInstance();
        for (uint8_t i = 1; i <= 2; i++) {
            publishFanState(i, dm.getState(i), dm.getLevel(i));
        }
    } else {
        Serial.printf("[MQTT] Connection failed, rc=%d. Will retry in 5s.\n", mqttClient.state());
    }

    return success;
}

void MqttManager::subscribeTopics() {
    // Relays: room_controller/relay/+/set
    String relaySub = nodeId + "/relay/+/set";
    mqttClient.subscribe(relaySub.c_str());

    // Fans: room_controller/fan/+/set and room_controller/fan/+/percentage_set
    String fanSub = nodeId + "/fan/+/set";
    String fanPctSub = nodeId + "/fan/+/percentage_set";
    mqttClient.subscribe(fanSub.c_str());
    mqttClient.subscribe(fanPctSub.c_str());

    Serial.printf("[MQTT] Subscribed to:\n  - %s\n  - %s\n  - %s\n", 
                  relaySub.c_str(), fanSub.c_str(), fanPctSub.c_str());
}

void MqttManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    MqttManager::getInstance().handleMessage(topic, payload, length);
}

void MqttManager::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
    char message[64];
    unsigned int copyLen = (length < sizeof(message) - 1) ? length : (sizeof(message) - 1);
    memcpy(message, payload, copyLen);
    message[copyLen] = '\0';

    String topicStr = String(topic);
    String msgStr = String(message);
    msgStr.trim();
    msgStr.toUpperCase();

    Serial.printf("[MQTT] Incoming [%s]: %s\n", topicStr.c_str(), msgStr.c_str());

    String relayPrefix = nodeId + "/relay/";
    String fanPrefix = nodeId + "/fan/";

    if (topicStr.startsWith(relayPrefix) && topicStr.endsWith("/set")) {
        // Topic format: <nodeId>/relay/<id>/set
        int idStart = relayPrefix.length();
        int idEnd = topicStr.indexOf('/', idStart);
        if (idEnd > idStart) {
            uint8_t relayId = topicStr.substring(idStart, idEnd).toInt();
            if (relayId >= 1 && relayId <= NUM_RELAYS) {
                bool newState = false;
                if (msgStr == "ON" || msgStr == "1" || msgStr == "TRUE") {
                    newState = true;
                    RelayManager::getInstance().setRelay(relayId, true, true);
                } else if (msgStr == "OFF" || msgStr == "0" || msgStr == "FALSE") {
                    newState = false;
                    RelayManager::getInstance().setRelay(relayId, false, true);
                } else if (msgStr == "TOGGLE") {
                    RelayManager::getInstance().toggleRelay(relayId, true);
                    newState = RelayManager::getInstance().getRelay(relayId);
                }
                AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::RELAY, relayId);
                publishRelayState(relayId, newState);
                WebManager::getInstance().broadcastRelayChange(relayId, newState);
            }
        }
    }
    else if (topicStr.startsWith(fanPrefix)) {
        if (topicStr.endsWith("/set")) {
            // Topic format: <nodeId>/fan/<id>/set
            int idStart = fanPrefix.length();
            int idEnd = topicStr.indexOf('/', idStart);
            if (idEnd > idStart) {
                uint8_t fanId = topicStr.substring(idStart, idEnd).toInt();
                if (fanId == 1 || fanId == 2) {
                    bool state = (msgStr == "ON" || msgStr == "1" || msgStr == "TRUE");
                    DimmerManager::getInstance().setState(fanId, state);
                    AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, fanId);
                    publishFanState(fanId, state, DimmerManager::getInstance().getLevel(fanId));
                    WebManager::getInstance().broadcastDimmerChange(fanId, DimmerManager::getInstance().getLevel(fanId), state);
                }
            }
        } else if (topicStr.endsWith("/percentage_set")) {
            // Topic format: <nodeId>/fan/<id>/percentage_set
            int idStart = fanPrefix.length();
            int idEnd = topicStr.indexOf('/', idStart);
            if (idEnd > idStart) {
                uint8_t fanId = topicStr.substring(idStart, idEnd).toInt();
                if (fanId == 1 || fanId == 2) {
                    int pct = msgStr.toInt();
                    pct = constrain(pct, 0, 100);
                    DimmerManager::getInstance().setLevel(fanId, pct, true);
                    AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, fanId);
                    publishFanState(fanId, DimmerManager::getInstance().getState(fanId), pct);
                    WebManager::getInstance().broadcastDimmerChange(fanId, pct, DimmerManager::getInstance().getState(fanId));
                }
            }
        }
    }
}

void MqttManager::publishRelayState(uint8_t relayId, bool state) {
    if (!mqttClient.connected()) return;
    String topic = nodeId + "/relay/" + String(relayId) + "/state";
    const char* payload = state ? "ON" : "OFF";
    mqttClient.publish(topic.c_str(), payload, true);
}

void MqttManager::publishFanState(uint8_t fanId, bool state, uint8_t level) {
    if (!mqttClient.connected()) return;
    String stateTopic = nodeId + "/fan/" + String(fanId) + "/state";
    const char* statePayload = state ? "ON" : "OFF";
    mqttClient.publish(stateTopic.c_str(), statePayload, true);

    String pctTopic = nodeId + "/fan/" + String(fanId) + "/percentage_state";
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%u", level);
    mqttClient.publish(pctTopic.c_str(), pctStr, true);
}

void MqttManager::publishAllTelemetry() {
    if (!mqttClient.connected()) return;

    // 1. Power Telemetry
    PowerData pwr = PowerMonitor::getInstance().getData();
    bool pwrOnline = (pwr.connectionState == SENSOR_CONNECTED);
    bool acActive = (pwrOnline && pwr.acInputState == AC_PRESENT);

    JsonDocument pwrDoc;
    pwrDoc["voltage"] = acActive ? (round(pwr.voltageRms * 10.0f) / 10.0f) : 0.0f;
    pwrDoc["current"] = acActive ? (round(pwr.currentRms * 100.0f) / 100.0f) : 0.0f;
    pwrDoc["power"] = acActive ? (round(pwr.activePower * 10.0f) / 10.0f) : 0.0f;
    pwrDoc["apparent_power"] = acActive ? (round(pwr.apparentPower * 10.0f) / 10.0f) : 0.0f;
    pwrDoc["power_factor"] = acActive ? (round(pwr.powerFactor * 100.0f) / 100.0f) : 0.0f;
    pwrDoc["frequency"] = acActive ? pwr.lineFrequency : 0.0f;
    pwrDoc["daily_energy"] = round(PowerLogManager::getInstance().getDailyKwh() * 1000.0f) / 1000.0f;
    pwrDoc["monthly_energy"] = round(PowerLogManager::getInstance().getMonthlyKwh() * 1000.0f) / 1000.0f;

    String pwrPayload;
    serializeJson(pwrDoc, pwrPayload);
    String pwrTopic = nodeId + "/power/state";
    mqttClient.publish(pwrTopic.c_str(), pwrPayload.c_str(), false);

    // 2. Climate & CO2 Telemetry
    Co2Data co2 = Co2Sensor::getInstance().getData();
    JsonDocument co2Doc;
    co2Doc["co2"] = co2.co2Ppm;
    co2Doc["temperature"] = round(co2.temperature * 10.0f) / 10.0f;
    co2Doc["humidity"] = round(co2.humidity * 10.0f) / 10.0f;

    String co2Payload;
    serializeJson(co2Doc, co2Payload);
    String co2Topic = nodeId + "/environment/state";
    mqttClient.publish(co2Topic.c_str(), co2Payload.c_str(), false);

    // 3. Human Presence Radar Telemetry
    MmWaveData mm = MmWaveManager::getInstance().getData();
    JsonDocument mmDoc;
    mmDoc["present"] = mm.isPresence ? "ON" : "OFF";
    mmDoc["motion"] = mm.isMotion ? "ON" : "OFF";
    mmDoc["breathing"] = mm.isBreathing ? "ON" : "OFF";
    mmDoc["distance"] = round(mm.distance * 100.0f) / 100.0f;
    mmDoc["speed"] = round(mm.speed * 100.0f) / 100.0f;

    String mmPayload;
    serializeJson(mmDoc, mmPayload);
    String mmTopic = nodeId + "/presence/state";
    mqttClient.publish(mmTopic.c_str(), mmPayload.c_str(), false);
}

// --------------------------------------------------------------------------
// Home Assistant Auto-Discovery Generator
// --------------------------------------------------------------------------

static void addDeviceRegistry(JsonObject& dev, const String& configUrl) {
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add("esp32_c6_room_controller");
    dev["name"] = "Room Controller";
    dev["mf"] = "Tech Panda";
    dev["mdl"] = "ESP32-C6 Smart Room Controller";
    dev["sw"] = "1.0.0";
    dev["cu"] = configUrl;
}

void MqttManager::publishSwitchDiscovery(uint8_t relayId, const String& name) {
    String topic = discoveryPrefix + "/switch/" + nodeId + "/relay_" + String(relayId) + "/config";

    JsonDocument doc;
    doc["~"] = nodeId + "/";
    doc["name"] = name;
    doc["uniq_id"] = "esp32_c6_rc_relay_" + String(relayId);
    doc["stat_t"] = "~relay/" + String(relayId) + "/state";
    doc["cmd_t"] = "~relay/" + String(relayId) + "/set";
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    doc["avty_t"] = "~status";
    doc["icon"] = "mdi:power-socket-eu";

    JsonObject dev = doc["dev"].to<JsonObject>();
    addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishFanDiscovery(uint8_t fanId, const String& name) {
    String topic = discoveryPrefix + "/fan/" + nodeId + "/fan_" + String(fanId) + "/config";

    JsonDocument doc;
    doc["~"] = nodeId + "/";
    doc["name"] = name;
    doc["uniq_id"] = "esp32_c6_rc_fan_" + String(fanId);
    doc["stat_t"] = "~fan/" + String(fanId) + "/state";
    doc["cmd_t"] = "~fan/" + String(fanId) + "/set";
    doc["pct_stat_t"] = "~fan/" + String(fanId) + "/percentage_state";
    doc["pct_cmd_t"] = "~fan/" + String(fanId) + "/percentage_set";
    doc["spd_rng_min"] = 1;
    doc["spd_rng_max"] = 100;
    doc["avty_t"] = "~status";
    doc["icon"] = "mdi:fan";

    JsonObject dev = doc["dev"].to<JsonObject>();
    addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishPowerSensorDiscovery() {
    struct SensorDef {
        const char* id;
        const char* name;
        const char* devCla;
        const char* unit;
        const char* statCla;
        const char* valTpl;
        const char* icon;
    };

    const SensorDef pwrSensors[] = {
        { "voltage", "AC Voltage", "voltage", "V", "measurement", "{{ value_json.voltage }}", "mdi:sine-wave" },
        { "current", "AC Current", "current", "A", "measurement", "{{ value_json.current }}", "mdi:current-ac" },
        { "power", "Active Power", "power", "W", "measurement", "{{ value_json.power }}", "mdi:flash" },
        { "apparent_power", "Apparent Power", "apparent_power", "VA", "measurement", "{{ value_json.apparent_power }}", "mdi:flash-outline" },
        { "power_factor", "Power Factor", "power_factor", "", "measurement", "{{ value_json.power_factor }}", "mdi:angle-acute" },
        { "frequency", "Grid Frequency", "frequency", "Hz", "measurement", "{{ value_json.frequency }}", "mdi:waveform" },
        { "daily_energy", "Daily Energy", "energy", "kWh", "total_increasing", "{{ value_json.daily_energy }}", "mdi:counter" },
        { "monthly_energy", "Monthly Energy", "energy", "kWh", "total_increasing", "{{ value_json.monthly_energy }}", "mdi:chart-bar" }
    };

    for (const auto& s : pwrSensors) {
        String topic = discoveryPrefix + "/sensor/" + nodeId + "/" + s.id + "/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = s.name;
        doc["uniq_id"] = "esp32_c6_rc_" + String(s.id);
        doc["stat_t"] = "~power/state";
        doc["val_tpl"] = s.valTpl;
        doc["avty_t"] = "~status";
        if (strlen(s.devCla) > 0) doc["dev_cla"] = s.devCla;
        if (strlen(s.unit) > 0) doc["unit_of_meas"] = s.unit;
        if (strlen(s.statCla) > 0) doc["stat_cla"] = s.statCla;
        if (strlen(s.icon) > 0) doc["icon"] = s.icon;

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }
}

void MqttManager::publishClimateSensorDiscovery() {
    struct SensorDef {
        const char* id;
        const char* name;
        const char* devCla;
        const char* unit;
        const char* valTpl;
        const char* icon;
    };

    const SensorDef envSensors[] = {
        { "co2", "CO2 Level", "carbon_dioxide", "ppm", "{{ value_json.co2 }}", "mdi:molecule-co2" },
        { "temperature", "Room Temperature", "temperature", "°C", "{{ value_json.temperature }}", "mdi:thermometer" },
        { "humidity", "Room Humidity", "humidity", "%", "{{ value_json.humidity }}", "mdi:water-percent" }
    };

    for (const auto& s : envSensors) {
        String topic = discoveryPrefix + "/sensor/" + nodeId + "/" + s.id + "/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = s.name;
        doc["uniq_id"] = "esp32_c6_rc_" + String(s.id);
        doc["stat_t"] = "~environment/state";
        doc["val_tpl"] = s.valTpl;
        doc["stat_cla"] = "measurement";
        doc["dev_cla"] = s.devCla;
        doc["unit_of_meas"] = s.unit;
        doc["avty_t"] = "~status";
        if (strlen(s.icon) > 0) doc["icon"] = s.icon;

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }
}

void MqttManager::publishPresenceDiscovery() {
    // 1. Occupancy Binary Sensor
    {
        String topic = discoveryPrefix + "/binary_sensor/" + nodeId + "/presence/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = "Human Presence";
        doc["uniq_id"] = "esp32_c6_rc_presence";
        doc["stat_t"] = "~presence/state";
        doc["val_tpl"] = "{{ value_json.present }}";
        doc["pl_on"] = "ON";
        doc["pl_off"] = "OFF";
        doc["dev_cla"] = "occupancy";
        doc["avty_t"] = "~status";
        doc["icon"] = "mdi:account-badge";

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 2. Motion Binary Sensor
    {
        String topic = discoveryPrefix + "/binary_sensor/" + nodeId + "/motion/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = "Presence Motion";
        doc["uniq_id"] = "esp32_c6_rc_motion";
        doc["stat_t"] = "~presence/state";
        doc["val_tpl"] = "{{ value_json.motion }}";
        doc["pl_on"] = "ON";
        doc["pl_off"] = "OFF";
        doc["dev_cla"] = "motion";
        doc["avty_t"] = "~status";
        doc["icon"] = "mdi:run-fast";

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 3. Micro-Breathing Target Binary Sensor
    {
        String topic = discoveryPrefix + "/binary_sensor/" + nodeId + "/breathing/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = "Breathing Detection";
        doc["uniq_id"] = "esp32_c6_rc_breathing";
        doc["stat_t"] = "~presence/state";
        doc["val_tpl"] = "{{ value_json.breathing }}";
        doc["pl_on"] = "ON";
        doc["pl_off"] = "OFF";
        doc["avty_t"] = "~status";
        doc["icon"] = "mdi:lungs";

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 4. Presence Distance Sensor
    {
        String topic = discoveryPrefix + "/sensor/" + nodeId + "/distance/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = "Presence Distance";
        doc["uniq_id"] = "esp32_c6_rc_distance";
        doc["stat_t"] = "~presence/state";
        doc["val_tpl"] = "{{ value_json.distance }}";
        doc["dev_cla"] = "distance";
        doc["unit_of_meas"] = "m";
        doc["stat_cla"] = "measurement";
        doc["avty_t"] = "~status";
        doc["icon"] = "mdi:ruler";

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    // 5. Presence Speed Sensor
    {
        String topic = discoveryPrefix + "/sensor/" + nodeId + "/speed/config";
        JsonDocument doc;
        doc["~"] = nodeId + "/";
        doc["name"] = "Presence Speed";
        doc["uniq_id"] = "esp32_c6_rc_speed";
        doc["stat_t"] = "~presence/state";
        doc["val_tpl"] = "{{ value_json.speed }}";
        doc["unit_of_meas"] = "m/s";
        doc["stat_cla"] = "measurement";
        doc["avty_t"] = "~status";
        doc["icon"] = "mdi:speedometer";

        JsonObject dev = doc["dev"].to<JsonObject>();
        addDeviceRegistry(dev, "http://" + WiFi.localIP().toString() + "/");

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }
}

void MqttManager::publishDiscovery() {
    if (!mqttClient.connected()) return;

    Serial.println("[MQTT] Publishing Home Assistant Auto-Discovery topics...");

    // Read custom friendly names from NVS
    Preferences prefs;
    prefs.begin("names", true);
    String nameFan1 = prefs.getString("fan1", "Fan 1");
    String nameFan2 = prefs.getString("fan2", "Fan 2");
    String nameR1 = prefs.getString("r1", "Main Light 1");
    String nameR2 = prefs.getString("r2", "Main Light 2");
    String nameR3 = prefs.getString("r3", "Spotlights");
    String nameR4 = prefs.getString("r4", "Wall Socket 1");
    String nameR5 = prefs.getString("r5", "Wall Socket 2");
    String nameR6 = prefs.getString("r6", "Ambient Strip");
    prefs.end();

    // 1. Publish 6 Switches (Relays 1..6)
    publishSwitchDiscovery(1, nameR1);
    publishSwitchDiscovery(2, nameR2);
    publishSwitchDiscovery(3, nameR3);
    publishSwitchDiscovery(4, nameR4);
    publishSwitchDiscovery(5, nameR5);
    publishSwitchDiscovery(6, nameR6);

    // 2. Publish 2 Fans
    publishFanDiscovery(1, nameFan1);
    publishFanDiscovery(2, nameFan2);

    // 3. Publish Power Sensors (ACS37800)
    publishPowerSensorDiscovery();

    // 4. Publish Climate Sensors (STCC4)
    publishClimateSensorDiscovery();

    // 5. Publish Presence Radar Sensors (C4002 mmWave)
    publishPresenceDiscovery();

    Serial.println("[MQTT] Auto-Discovery configuration published for all devices!");
}

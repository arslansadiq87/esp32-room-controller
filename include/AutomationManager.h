#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Config.h"
#include "MmWaveManager.h"
#include "Co2Sensor.h"

// Automation Modes for Exhaust Fan (Fan 2)
enum class ExhaustAutoMode : uint8_t {
    MANUAL = 0,
    CO2_BASED = 1,
    TEMP_BASED = 2,
    COMBINED = 3
};

// Ceiling Fan (Fan 1) Automation Configuration
struct CeilingFanConfig {
    bool enabled = false;           // Auto Mode vs Manual Mode
    float targetTemp = 26.0f;       // Target room temperature
    float tempLow = 24.0f;          // Below this: Fan OFF (0%)
    float tempMed = 27.0f;          // 24 - 27°C: 40%
    float tempHigh = 30.0f;         // 27 - 30°C: 70%, > 30°C: 100%
    uint8_t speedLow = 40;          // 40%
    uint8_t speedMed = 70;          // 70%
    uint8_t speedHigh = 100;        // 100%
};

// Exhaust Fan (Fan 2) Automation Configuration
struct ExhaustFanConfig {
    ExhaustAutoMode mode = ExhaustAutoMode::MANUAL; // Manual, CO2, Temp, Combined
    int16_t co2Low = 800;           // Below this: OFF (0%)
    int16_t co2Med = 1200;          // 800 - 1200: 40%
    int16_t co2High = 1800;         // 1200 - 1800: 70%, > 1800: 100%
    uint8_t co2SpeedLow = 40;       // 40%
    uint8_t co2SpeedMed = 70;       // 70%
    uint8_t co2SpeedHigh = 100;     // 100%
    float tempLow = 26.0f;          // Below this: OFF (0%)
    float tempHigh = 30.0f;         // 26 - 30°C: 50%, > 30°C: 100%
    uint8_t tempSpeedMed = 50;      // 50%
    uint8_t tempSpeedHigh = 100;    // 100%
};

// Smart Scenarios Configuration
struct SmartScenariosConfig {
    bool nightQuietEnabled = true;
    uint8_t nightStartHour = 22;    // 22:00 (10 PM)
    uint8_t nightEndHour = 7;       // 07:00 (7 AM)
    uint8_t nightMaxSpeed = 50;     // Max fan speed cap (50%)

    bool airBoostEnabled = true;
    int16_t airBoostTriggerCo2 = 1500; // Trigger when CO2 > 1500 ppm
    int16_t airBoostClearCo2 = 900;    // Return to normal when CO2 < 900 ppm
    uint8_t airBoostExhaustSpeed = 100; // 100% Exhaust
    uint8_t airBoostCeilingSpeed = 40;  // 40% Ceiling Fan
};

// Light Automation Modes (Per Channel)
enum class LightAutoMode : uint8_t {
    MANUAL = 0,
    PRESENCE = 1,
    PRESENCE_TIME = 2,
    SCHEDULED = 3
};

// Scenario Presets
enum class AutomationPreset : uint8_t {
    NORMAL = 0,
    MOVIE = 1,
    CLEANING = 2,
    AWAY = 3
};

// Per-Channel Light Automation Configuration
struct LightAutomationConfig {
    bool enabled = false;              // Auto vs Manual Mode
    LightAutoMode mode = LightAutoMode::PRESENCE;
    bool nightAllowed = false;         // Allow turn on during night hours (22:00-07:00)
    uint32_t inactivityTimeoutSec = 0; // 0 = inherit global delay, or custom seconds
    uint8_t startHour = 7;             // Time window start (for Mode 2 & 3)
    uint8_t startMin = 0;
    uint8_t endHour = 22;              // Time window end (for Mode 2 & 3)
    uint8_t endMin = 0;
    uint8_t quietLevel = 30;           // Quiet level for ambient loads (e.g. 30%)
    String statusDesc = "Manual Mode"; // Live status description
};

// Live Automation Status Telemetry
struct AutomationStatus {
    bool masterEnabled;
    bool presenceActive;
    uint32_t inactivityRemainingSec;
    bool fan1AutoActive;
    uint8_t fan1Demand;             // 0 - 100%
    String fan1StatusText;          // e.g. "Temp: 26.8°C -> 40%"
    bool fan2AutoActive;
    uint8_t fan2Demand;             // 0 - 100%
    String fan2StatusText;          // e.g. "CO2: 1350ppm -> 70%"
    bool nightQuietActive;
    bool airBoostActive;
    AutomationPreset currentPreset;
};

class AutomationManager {
public:
    static AutomationManager& getInstance();

    void begin();
    void update();

    // Master Automation & Inactivity
    bool isMasterEnabled() const { return masterEnabled; }
    void setMasterEnabled(bool enabled);
    uint32_t getAutoOffDelaySec() const { return autoOffDelaySec; }
    void setAutoOffDelaySec(uint32_t seconds);

    // Ceiling Fan (Fan 1)
    const CeilingFanConfig& getCeilingFanConfig() const { return ceilingFan; }
    void setCeilingFanConfig(const CeilingFanConfig& config);
    bool isCeilingFanAutoEnabled() const { return masterEnabled && ceilingFan.enabled; }

    // Exhaust Fan (Fan 2)
    const ExhaustFanConfig& getExhaustFanConfig() const { return exhaustFan; }
    void setExhaustFanConfig(const ExhaustFanConfig& config);
    bool isExhaustFanAutoEnabled() const { return masterEnabled && (exhaustFan.mode != ExhaustAutoMode::MANUAL); }

    // Smart Scenarios
    const SmartScenariosConfig& getSmartScenarios() const { return scenarios; }
    void setSmartScenarios(const SmartScenariosConfig& config);

    // 6-Channel Lighting Automation
    const LightAutomationConfig& getLightConfig(uint8_t relayId) const;
    void setLightConfig(uint8_t relayId, const LightAutomationConfig& config);
    bool isRelayAutoEnabled(uint8_t relayId) const;
    void setRelayAutoEnabled(uint8_t relayId, bool enabled);

    // Scenario Presets
    void applyPreset(AutomationPreset preset);
    AutomationPreset getCurrentPreset() const { return currentPreset; }

    // Local Time Synchronization (from client browser or NTP)
    void syncTime(uint8_t hour, uint8_t minute);
    uint8_t getCurrentHour() const;
    uint8_t getCurrentMinute() const;

    // Live Telemetry & Status
    AutomationStatus getStatus() const;

    // Persistence & API
    void saveSettings();
    void loadSettings();
    String toJson() const;
    bool fromJson(const JsonDocument& doc);

private:
    AutomationManager();
    ~AutomationManager() = default;

    AutomationManager(const AutomationManager&) = delete;
    AutomationManager& operator=(const AutomationManager&) = delete;

    Preferences prefs;

    bool masterEnabled = true;
    uint32_t autoOffDelaySec = 120; // Default 2 minutes

    CeilingFanConfig ceilingFan;
    ExhaustFanConfig exhaustFan;
    SmartScenariosConfig scenarios;
    LightAutomationConfig lightConfigs[NUM_RELAYS];

    AutomationPreset currentPreset = AutomationPreset::NORMAL;

    // Dynamic State Tracking
    uint32_t lastLoopMillis = 0;
    bool timeSynced = false;
    uint32_t timeSyncMillis = 0;
    uint32_t timeSyncSecOfDay = 43200; // Default 12:00:00

    bool currentPresence = false;
    bool lastPresenceState = false;
    uint32_t presenceLostTime = 0;
    bool pendingAutoOff = false;

    bool isNightQuietActive = false;
    bool isAirBoostActive = false;

    uint8_t currentFan1Demand = 0;
    uint8_t currentFan2Demand = 0;
    String fan1StatusDesc = "Manual Mode";
    String fan2StatusDesc = "Manual Mode";

    // Anti-Chatter Dwell & State Tracking
    uint32_t lastFan1SpeedChange = 0;
    uint32_t lastFan2SpeedChange = 0;
    uint8_t lastAppliedFan1Speed = 0;
    uint8_t lastAppliedFan2Speed = 0;
    uint8_t lastCo2Demand = 0;
    uint8_t lastTempDemand = 0;
    bool isRoomOccupied = false;

    // Relay anti-chatter dwell & timeout tracking
    uint32_t lastRelayToggleTime[NUM_RELAYS];
    uint32_t relayPresenceLostTime[NUM_RELAYS];
    bool relayPendingOff[NUM_RELAYS];

    // Internal Automation Evaluation Functions with Schmitt-Trigger Hysteresis
    uint8_t calculateCeilingFanSpeed(float temp, bool presence);
    uint8_t calculateExhaustFanSpeed(int16_t co2, float temp, bool presence);
    void evaluateLighting(uint32_t now, bool presence);
};

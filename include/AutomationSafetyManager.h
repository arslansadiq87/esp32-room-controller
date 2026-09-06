#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"

#define NVS_NAMESPACE_SAFETY "auto_safety"

enum class SafetyDeviceType : uint8_t {
    RELAY = 0,
    FAN = 1
};

struct SafetyConfig {
    float tempHysteresis = 1.5f;          // Range 0.5 - 5.0 °C
    int16_t co2Hysteresis = 300;          // Range 50 - 500 ppm
    uint32_t lightMinOnTimeSec = 30;      // Minimum ON duration for lights
    uint32_t lightMinOffTimeSec = 30;     // Minimum OFF duration for lights
    uint32_t fanMinRunTimeSec = 60;       // Minimum RUN duration for fans
    uint32_t fanMinOffTimeSec = 60;       // Minimum OFF duration for fans
    uint32_t relayMinIntervalSec = 10;    // Anti-chatter dwell between any toggle
    uint8_t fanRampStepPercent = 10;      // 10% step increment
    uint32_t fanRampIntervalMs = 5000;    // Step every 5 seconds
    uint32_t decisionDelayLightMs = 5000; // 5s confirmation delay for lights
    uint32_t decisionDelayFanMs = 15000;  // 15s confirmation delay for fans
    uint32_t manualOverrideDurationSec = 1800; // 30 minutes default override
    uint32_t powerRecoveryDelaySec = 30;  // 30s boot stabilization delay
};

class AutomationSafetyManager {
public:
    static AutomationSafetyManager& getInstance();

    void begin();
    void update(); // Called in main loop

    // Sensor Update & Moving Average Filtering
    void updateSensors(float rawTemp, int16_t rawCo2, bool rawPresence, bool isSensorConnected);
    float getFilteredTemp() const { return filteredTemp; }
    int16_t getFilteredCo2() const { return filteredCo2; }
    bool isPresenceConfirmed() const { return presenceConfirmed; }
    bool isTempValid() const { return tempValid; }
    bool isCo2Valid() const { return co2Valid; }
    bool isSensorOnline() const { return sensorOnline; }

    // Power Recovery & Stabilization
    bool isPowerStabilized() const;
    uint32_t getStabilizationRemainingSec() const;

    // Safety-Gated Automation Actuation
    // Returns true if the command was accepted and executed or scheduled
    bool requestRelayState(uint8_t relayId, bool targetState, const char* reason = "Automation");
    bool requestFanSpeed(uint8_t fanId, uint8_t targetSpeed, const char* reason = "Automation");

    // Manual Intervention & Override Protection
    void notifyManualIntervention(SafetyDeviceType type, uint8_t index);
    bool isManualOverrideActive(SafetyDeviceType type, uint8_t index) const;
    uint32_t getManualOverrideRemainingSec(SafetyDeviceType type, uint8_t index) const;
    void clearManualOverride(SafetyDeviceType type, uint8_t index);
    void clearAllManualOverrides();

    // Configuration & Settings
    const SafetyConfig& getConfig() const { return config; }
    void setConfig(const SafetyConfig& newConfig);
    void saveSettings();
    void loadSettings();
    String toJson() const;
    bool fromJson(const JsonDocument& doc);

private:
    AutomationSafetyManager();
    ~AutomationSafetyManager() = default;

    AutomationSafetyManager(const AutomationSafetyManager&) = delete;
    AutomationSafetyManager& operator=(const AutomationSafetyManager&) = delete;

    Preferences prefs;
    SafetyConfig config;
    uint32_t bootMillis = 0;

    // Sensor Moving Average Buffers
    static const uint8_t TEMP_WINDOW_SIZE = 5;
    static const uint8_t CO2_WINDOW_SIZE = 10;
    float tempBuffer[TEMP_WINDOW_SIZE] = {0};
    uint8_t tempBufIdx = 0;
    uint8_t tempBufCount = 0;
    float filteredTemp = 25.0f;

    int16_t co2Buffer[CO2_WINDOW_SIZE] = {0};
    uint8_t co2BufIdx = 0;
    uint8_t co2BufCount = 0;
    int16_t filteredCo2 = 600;

    bool tempValid = true;
    bool co2Valid = true;
    bool sensorOnline = true;

    // Presence Debounce
    uint8_t presenceTrueCount = 0;
    uint8_t presenceFalseCount = 0;
    bool presenceConfirmed = false;

    // Relay State & Dwell Tracking (Channels 1 to 6)
    uint32_t lastRelayToggleTime[NUM_RELAYS] = {0};
    bool relayCurrentState[NUM_RELAYS] = {false};

    // Relay Decision Confirmation Delay Buffers
    bool relayPendingTarget[NUM_RELAYS] = {false};
    bool relayHasPending[NUM_RELAYS] = {false};
    uint32_t relayPendingFirstCross[NUM_RELAYS] = {0};

    // Fan State, Dwell & Ramp Tracking (Fans 1 & 2)
    uint32_t lastFanStateChangeTime[2] = {0};
    uint8_t fanHardwareSpeed[2] = {0};
    uint8_t fanTargetSpeed[2] = {0};
    uint8_t fanRampSpeed[2] = {0};
    uint32_t lastFanRampStepTime[2] = {0};

    // Fan Decision Confirmation Delay Buffers
    uint8_t fanPendingSpeed[2] = {0};
    bool fanHasPending[2] = {false};
    uint32_t fanPendingFirstCross[2] = {0};

    // Manual Override Tracking
    bool manualOverrideRelay[NUM_RELAYS] = {false};
    uint32_t manualOverrideStartRelay[NUM_RELAYS] = {0};

    bool manualOverrideFan[2] = {false};
    uint32_t manualOverrideStartFan[2] = {0};

    void advanceFanRamp(uint32_t now);
};

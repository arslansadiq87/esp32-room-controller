#include "AutomationManager.h"
#include "AutomationSafetyManager.h"
#include "RelayManager.h"
#include "DimmerManager.h"

AutomationManager& AutomationManager::getInstance() {
    static AutomationManager instance;
    return instance;
}

AutomationManager::AutomationManager() {
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
        lastRelayToggleTime[i] = 0;
        relayPresenceLostTime[i] = 0;
        relayPendingOff[i] = false;
    }

    // Channel 1: Main Light 1
    lightConfigs[0].enabled = true;
    lightConfigs[0].mode = LightAutoMode::PRESENCE;
    lightConfigs[0].nightAllowed = false;
    lightConfigs[0].inactivityTimeoutSec = 300; // 5 min

    // Channel 2: Main Light 2
    lightConfigs[1].enabled = true;
    lightConfigs[1].mode = LightAutoMode::PRESENCE;
    lightConfigs[1].nightAllowed = false;
    lightConfigs[1].inactivityTimeoutSec = 300; // 5 min

    // Channel 3: Spotlights
    lightConfigs[2].enabled = true;
    lightConfigs[2].mode = LightAutoMode::PRESENCE_TIME;
    lightConfigs[2].nightAllowed = false;
    lightConfigs[2].inactivityTimeoutSec = 300;
    lightConfigs[2].startHour = 7;
    lightConfigs[2].endHour = 22;

    // Channel 4: Wall Socket 1 (Manual default for safety)
    lightConfigs[3].enabled = false;
    lightConfigs[3].mode = LightAutoMode::MANUAL;
    lightConfigs[3].nightAllowed = false;

    // Channel 5: Wall Socket 2 (Manual default for safety)
    lightConfigs[4].enabled = false;
    lightConfigs[4].mode = LightAutoMode::MANUAL;
    lightConfigs[4].nightAllowed = false;

    // Channel 6: Ambient Strip
    lightConfigs[5].enabled = true;
    lightConfigs[5].mode = LightAutoMode::PRESENCE;
    lightConfigs[5].nightAllowed = true; // Soft night guidance allowed
    lightConfigs[5].quietLevel = 30;
    lightConfigs[5].inactivityTimeoutSec = 300;
}

void AutomationManager::begin() {
    loadSettings();
    AutomationSafetyManager::getInstance().begin();
    Serial.println("[AUTO] Automation Manager initialized with persistent NVS rules.");
}

void AutomationManager::syncTime(uint8_t hour, uint8_t minute) {
    if (hour < 24 && minute < 60) {
        timeSyncMillis = millis();
        timeSyncSecOfDay = (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL;
        timeSynced = true;
        Serial.printf("[AUTO] Clock synchronized to %02u:%02u\n", hour, minute);
    }
}

uint8_t AutomationManager::getCurrentHour() const {
    if (!timeSynced) return 12; // Default midday if un-synced
    uint32_t elapsedSec = (millis() - timeSyncMillis) / 1000UL;
    uint32_t currentSecOfDay = (timeSyncSecOfDay + elapsedSec) % 86400UL;
    return (uint8_t)(currentSecOfDay / 3600UL);
}

uint8_t AutomationManager::getCurrentMinute() const {
    if (!timeSynced) return 0;
    uint32_t elapsedSec = (millis() - timeSyncMillis) / 1000UL;
    uint32_t currentSecOfDay = (timeSyncSecOfDay + elapsedSec) % 86400UL;
    return (uint8_t)((currentSecOfDay % 3600UL) / 60UL);
}

const LightAutomationConfig& AutomationManager::getLightConfig(uint8_t relayId) const {
    if (relayId >= 1 && relayId <= NUM_RELAYS) {
        return lightConfigs[relayId - 1];
    }
    static LightAutomationConfig dummy;
    return dummy;
}

void AutomationManager::setLightConfig(uint8_t relayId, const LightAutomationConfig& config) {
    if (relayId >= 1 && relayId <= NUM_RELAYS) {
        lightConfigs[relayId - 1] = config;
        saveSettings();
    }
}

bool AutomationManager::isRelayAutoEnabled(uint8_t relayId) const {
    if (relayId >= 1 && relayId <= NUM_RELAYS) {
        return masterEnabled && lightConfigs[relayId - 1].enabled;
    }
    return false;
}

void AutomationManager::setRelayAutoEnabled(uint8_t relayId, bool enabled) {
    if (relayId >= 1 && relayId <= NUM_RELAYS) {
        lightConfigs[relayId - 1].enabled = enabled;
        saveSettings();
    }
}

void AutomationManager::applyPreset(AutomationPreset preset) {
    currentPreset = preset;
    RelayManager& rm = RelayManager::getInstance();
    DimmerManager& dm = DimmerManager::getInstance();
    uint32_t now = millis();

    switch (preset) {
        case AutomationPreset::NORMAL:
            Serial.println("[AUTO] Preset NORMAL applied.");
            break;

        case AutomationPreset::MOVIE:
            Serial.println("[AUTO] Preset MOVIE MODE applied.");
            rm.setRelay(1, false, true);
            rm.setRelay(2, false, true);
            rm.setRelay(3, false, true);
            rm.setRelay(6, true, true);
            lastRelayToggleTime[0] = now;
            lastRelayToggleTime[1] = now;
            lastRelayToggleTime[2] = now;
            lastRelayToggleTime[5] = now;
            if (dm.getState(1) && dm.getLevel(1) > 40) {
                dm.setLevel(1, 40, true);
            }
            break;

        case AutomationPreset::CLEANING:
            Serial.println("[AUTO] Preset CLEANING MODE applied.");
            rm.setRelay(1, true, true);
            rm.setRelay(2, true, true);
            rm.setRelay(3, true, true);
            rm.setRelay(6, true, true);
            lastRelayToggleTime[0] = now;
            lastRelayToggleTime[1] = now;
            lastRelayToggleTime[2] = now;
            lastRelayToggleTime[5] = now;
            break;

        case AutomationPreset::AWAY:
            Serial.println("[AUTO] Preset AWAY MODE applied.");
            for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
                if (rm.getRelay(i)) {
                    rm.setRelay(i, false, true);
                    lastRelayToggleTime[i - 1] = now;
                }
            }
            if (dm.getState(1)) dm.setState(1, false);
            if (dm.getState(2) && !isAirBoostActive) dm.setState(2, false);
            break;
    }
}

void AutomationManager::loadSettings() {
    if (prefs.begin(NVS_NAMESPACE_AUTO, true)) { // Read-only
        masterEnabled = prefs.getBool("master", true);
        autoOffDelaySec = prefs.getULong("off_delay", 120);

        // Ceiling Fan
        ceilingFan.enabled = prefs.getBool("f1_en", false);
        ceilingFan.targetTemp = prefs.getFloat("f1_tgt", 26.0f);
        ceilingFan.tempLow = prefs.getFloat("f1_tlow", 24.0f);
        ceilingFan.tempMed = prefs.getFloat("f1_tmed", 27.0f);
        ceilingFan.tempHigh = prefs.getFloat("f1_thigh", 30.0f);
        ceilingFan.speedLow = prefs.getUChar("f1_slow", 40);
        ceilingFan.speedMed = prefs.getUChar("f1_smed", 70);
        ceilingFan.speedHigh = prefs.getUChar("f1_shigh", 100);

        // Exhaust Fan
        exhaustFan.mode = (ExhaustAutoMode)prefs.getUChar("f2_mode", (uint8_t)ExhaustAutoMode::MANUAL);
        exhaustFan.co2Low = prefs.getShort("f2_clow", 800);
        exhaustFan.co2Med = prefs.getShort("f2_cmed", 1200);
        exhaustFan.co2High = prefs.getShort("f2_chigh", 1800);
        exhaustFan.co2SpeedLow = prefs.getUChar("f2_cslow", 40);
        exhaustFan.co2SpeedMed = prefs.getUChar("f2_csmed", 70);
        exhaustFan.co2SpeedHigh = prefs.getUChar("f2_cshigh", 100);
        exhaustFan.tempLow = prefs.getFloat("f2_tlow", 26.0f);
        exhaustFan.tempHigh = prefs.getFloat("f2_thigh", 30.0f);
        exhaustFan.tempSpeedMed = prefs.getUChar("f2_tsmed", 50);
        exhaustFan.tempSpeedHigh = prefs.getUChar("f2_tshigh", 100);

        // Scenarios
        scenarios.nightQuietEnabled = prefs.getBool("nq_en", true);
        scenarios.nightStartHour = prefs.getUChar("nq_sh", 22);
        scenarios.nightEndHour = prefs.getUChar("nq_eh", 7);
        scenarios.nightMaxSpeed = prefs.getUChar("nq_max", 50);

        scenarios.airBoostEnabled = prefs.getBool("ab_en", true);
        scenarios.airBoostTriggerCo2 = prefs.getShort("ab_trig", 1500);
        scenarios.airBoostClearCo2 = prefs.getShort("ab_clr", 900);
        scenarios.airBoostExhaustSpeed = prefs.getUChar("ab_ex", 100);
        scenarios.airBoostCeilingSpeed = prefs.getUChar("ab_cf", 40);

        // 6-Channel Lighting Configuration
        for (uint8_t i = 0; i < NUM_RELAYS; i++) {
            char kEn[10], kMode[10], kNa[10], kTo[10], kSh[10], kSm[10], kEh[10], kEm[10], kQl[10];
            snprintf(kEn, sizeof(kEn), "l_en_%u", i + 1);
            snprintf(kMode, sizeof(kMode), "l_md_%u", i + 1);
            snprintf(kNa, sizeof(kNa), "l_na_%u", i + 1);
            snprintf(kTo, sizeof(kTo), "l_to_%u", i + 1);
            snprintf(kSh, sizeof(kSh), "l_sh_%u", i + 1);
            snprintf(kSm, sizeof(kSm), "l_sm_%u", i + 1);
            snprintf(kEh, sizeof(kEh), "l_eh_%u", i + 1);
            snprintf(kEm, sizeof(kEm), "l_em_%u", i + 1);
            snprintf(kQl, sizeof(kQl), "l_ql_%u", i + 1);

            lightConfigs[i].enabled = prefs.getBool(kEn, lightConfigs[i].enabled);
            lightConfigs[i].mode = (LightAutoMode)prefs.getUChar(kMode, (uint8_t)lightConfigs[i].mode);
            lightConfigs[i].nightAllowed = prefs.getBool(kNa, lightConfigs[i].nightAllowed);
            lightConfigs[i].inactivityTimeoutSec = prefs.getULong(kTo, lightConfigs[i].inactivityTimeoutSec);
            lightConfigs[i].startHour = prefs.getUChar(kSh, lightConfigs[i].startHour);
            lightConfigs[i].startMin = prefs.getUChar(kSm, lightConfigs[i].startMin);
            lightConfigs[i].endHour = prefs.getUChar(kEh, lightConfigs[i].endHour);
            lightConfigs[i].endMin = prefs.getUChar(kEm, lightConfigs[i].endMin);
            lightConfigs[i].quietLevel = prefs.getUChar(kQl, lightConfigs[i].quietLevel);
        }

        prefs.end();
        Serial.println("[AUTO] Configuration loaded from NVS successfully.");
    }
}

void AutomationManager::saveSettings() {
    if (prefs.begin(NVS_NAMESPACE_AUTO, false)) { // Read-write
        prefs.putBool("master", masterEnabled);
        prefs.putULong("off_delay", autoOffDelaySec);

        // Ceiling Fan
        prefs.putBool("f1_en", ceilingFan.enabled);
        prefs.putFloat("f1_tgt", ceilingFan.targetTemp);
        prefs.putFloat("f1_tlow", ceilingFan.tempLow);
        prefs.putFloat("f1_tmed", ceilingFan.tempMed);
        prefs.putFloat("f1_thigh", ceilingFan.tempHigh);
        prefs.putUChar("f1_slow", ceilingFan.speedLow);
        prefs.putUChar("f1_smed", ceilingFan.speedMed);
        prefs.putUChar("f1_shigh", ceilingFan.speedHigh);

        // Exhaust Fan
        prefs.putUChar("f2_mode", (uint8_t)exhaustFan.mode);
        prefs.putShort("f2_clow", exhaustFan.co2Low);
        prefs.putShort("f2_cmed", exhaustFan.co2Med);
        prefs.putShort("f2_chigh", exhaustFan.co2High);
        prefs.putUChar("f2_cslow", exhaustFan.co2SpeedLow);
        prefs.putUChar("f2_csmed", exhaustFan.co2SpeedMed);
        prefs.putUChar("f2_cshigh", exhaustFan.co2SpeedHigh);
        prefs.putFloat("f2_tlow", exhaustFan.tempLow);
        prefs.putFloat("f2_thigh", exhaustFan.tempHigh);
        prefs.putUChar("f2_tsmed", exhaustFan.tempSpeedMed);
        prefs.putUChar("f2_tshigh", exhaustFan.tempSpeedHigh);

        // Scenarios
        prefs.putBool("nq_en", scenarios.nightQuietEnabled);
        prefs.putUChar("nq_sh", scenarios.nightStartHour);
        prefs.putUChar("nq_eh", scenarios.nightEndHour);
        prefs.putUChar("nq_max", scenarios.nightMaxSpeed);

        prefs.putBool("ab_en", scenarios.airBoostEnabled);
        prefs.putShort("ab_trig", scenarios.airBoostTriggerCo2);
        prefs.putShort("ab_clr", scenarios.airBoostClearCo2);
        prefs.putUChar("ab_ex", scenarios.airBoostExhaustSpeed);
        prefs.putUChar("ab_cf", scenarios.airBoostCeilingSpeed);

        // 6-Channel Lighting
        for (uint8_t i = 0; i < NUM_RELAYS; i++) {
            char kEn[10], kMode[10], kNa[10], kTo[10], kSh[10], kSm[10], kEh[10], kEm[10], kQl[10];
            snprintf(kEn, sizeof(kEn), "l_en_%u", i + 1);
            snprintf(kMode, sizeof(kMode), "l_md_%u", i + 1);
            snprintf(kNa, sizeof(kNa), "l_na_%u", i + 1);
            snprintf(kTo, sizeof(kTo), "l_to_%u", i + 1);
            snprintf(kSh, sizeof(kSh), "l_sh_%u", i + 1);
            snprintf(kSm, sizeof(kSm), "l_sm_%u", i + 1);
            snprintf(kEh, sizeof(kEh), "l_eh_%u", i + 1);
            snprintf(kEm, sizeof(kEm), "l_em_%u", i + 1);
            snprintf(kQl, sizeof(kQl), "l_ql_%u", i + 1);

            prefs.putBool(kEn, lightConfigs[i].enabled);
            prefs.putUChar(kMode, (uint8_t)lightConfigs[i].mode);
            prefs.putBool(kNa, lightConfigs[i].nightAllowed);
            prefs.putULong(kTo, lightConfigs[i].inactivityTimeoutSec);
            prefs.putUChar(kSh, lightConfigs[i].startHour);
            prefs.putUChar(kSm, lightConfigs[i].startMin);
            prefs.putUChar(kEh, lightConfigs[i].endHour);
            prefs.putUChar(kEm, lightConfigs[i].endMin);
            prefs.putUChar(kQl, lightConfigs[i].quietLevel);
        }

        prefs.end();
        Serial.println("[AUTO] Configuration persisted to NVS.");
    }
}

void AutomationManager::setMasterEnabled(bool enabled) {
    masterEnabled = enabled;
    saveSettings();
}

void AutomationManager::setAutoOffDelaySec(uint32_t seconds) {
    autoOffDelaySec = seconds;
    saveSettings();
}

void AutomationManager::setCeilingFanConfig(const CeilingFanConfig& config) {
    ceilingFan = config;
    saveSettings();
}

void AutomationManager::setExhaustFanConfig(const ExhaustFanConfig& config) {
    exhaustFan = config;
    saveSettings();
}

void AutomationManager::setSmartScenarios(const SmartScenariosConfig& config) {
    scenarios = config;
    saveSettings();
}

// State Machine Update Loop (Called non-blockingly from main.cpp every loop)
void AutomationManager::update() {
    static uint32_t lastEvalTime = 0;
    uint32_t now = millis();

    AutomationSafetyManager& sm = AutomationSafetyManager::getInstance();
    sm.update(); // Advance fan ramping & override expiration

    // Run rule evaluation once per second
    if (now - lastEvalTime < 1000UL) {
        return;
    }
    lastEvalTime = now;

    // Read Sensors and Feed into Safety Filter
    MmWaveData mm = MmWaveManager::getInstance().getData();
    Co2Data env = Co2Sensor::getInstance().getData();
    sm.updateSensors(env.temperature, env.co2Ppm, mm.isPresence, env.isConnected);

    float currentTemp = sm.getFilteredTemp();
    int16_t currentCo2 = sm.getFilteredCo2();
    bool currentPresence = sm.isPresenceConfirmed();

    // Check Power Recovery Stabilization (30s delay)
    if (!sm.isPowerStabilized()) {
        uint32_t rem = sm.getStabilizationRemainingSec();
        fan1StatusDesc = String("Power Stabilizing (") + rem + "s)";
        fan2StatusDesc = String("Power Stabilizing (") + rem + "s)";
        for (uint8_t i = 0; i < NUM_RELAYS; i++) {
            lightConfigs[i].statusDesc = String("Power Stabilizing (") + rem + "s)";
        }
        return;
    }

    // Evaluate Night Quiet Schedule
    if (scenarios.nightQuietEnabled) {
        uint8_t curHour = getCurrentHour();
        if (scenarios.nightStartHour > scenarios.nightEndHour) {
            isNightQuietActive = (curHour >= scenarios.nightStartHour || curHour < scenarios.nightEndHour);
        } else {
            isNightQuietActive = (curHour >= scenarios.nightStartHour && curHour < scenarios.nightEndHour);
        }
    } else {
        isNightQuietActive = false;
    }

    // Evaluate Air Quality Boost Hysteresis (Priority 2)
    if (scenarios.airBoostEnabled && env.isConnected && sm.isCo2Valid()) {
        if (!isAirBoostActive && currentCo2 >= scenarios.airBoostTriggerCo2) {
            isAirBoostActive = true;
            Serial.printf("[AUTO] Air Quality Boost TRIGGERED (CO2: %d ppm > %d ppm)!\n", currentCo2, scenarios.airBoostTriggerCo2);
        } else if (isAirBoostActive && currentCo2 <= scenarios.airBoostClearCo2) {
            isAirBoostActive = false;
            Serial.printf("[AUTO] Air Quality Boost CLEARED (CO2: %d ppm <= %d ppm).\n", currentCo2, scenarios.airBoostClearCo2);
        }
    } else {
        isAirBoostActive = false;
    }

    if (!masterEnabled) {
        pendingAutoOff = false;
        fan1StatusDesc = "Master Automation OFF";
        fan2StatusDesc = "Master Automation OFF";
        evaluateLighting(now, false);
        return;
    }

    // 1. Debounced Human Presence & Inactivity Gate
    if (currentPresence) {
        pendingAutoOff = false;
        if (!isRoomOccupied) {
            isRoomOccupied = true;
            Serial.println("[AUTO] Presence confirmed! Activating automated room loads...");
        }
    } else {
        if (isRoomOccupied && !pendingAutoOff) {
            presenceLostTime = now;
            pendingAutoOff = true;
            Serial.printf("[AUTO] Presence lost. Inactivity countdown started (%u seconds)...\n", autoOffDelaySec);
        }

        if (pendingAutoOff && (now - presenceLostTime >= autoOffDelaySec * 1000UL)) {
            pendingAutoOff = false;
            isRoomOccupied = false;
            Serial.println("[AUTO] Inactivity timeout reached. Shutting down automated loads...");

            // Shut down Ceiling Fan (Fan 1)
            if (ceilingFan.enabled && !sm.isManualOverrideActive(SafetyDeviceType::FAN, 1)) {
                sm.requestFanSpeed(1, 0, "Inactivity Timeout");
                currentFan1Demand = 0;
                fan1StatusDesc = "Unoccupied (Inactivity Off)";
            }

            // Shut down Exhaust Fan (Fan 2) unless Air Quality Boost is active
            if (exhaustFan.mode != ExhaustAutoMode::MANUAL && !isAirBoostActive && !sm.isManualOverrideActive(SafetyDeviceType::FAN, 2)) {
                sm.requestFanSpeed(2, 0, "Inactivity Timeout");
                currentFan2Demand = 0;
                fan2StatusDesc = "Unoccupied (Inactivity Off)";
            }
        }
    }

    // Evaluate 6-Channel Lighting Engine with Per-Relay Modes & Inactivity
    evaluateLighting(now, isRoomOccupied);

    // Calculate effective presence state for fans
    bool effectivePresence = isRoomOccupied || (pendingAutoOff && (now - presenceLostTime < autoOffDelaySec * 1000UL));

    // 2. Evaluate Ceiling Fan (Fan 1) Automation
    if (ceilingFan.enabled) {
        if (sm.isManualOverrideActive(SafetyDeviceType::FAN, 1)) {
            uint32_t remMin = (sm.getManualOverrideRemainingSec(SafetyDeviceType::FAN, 1) + 59) / 60;
            fan1StatusDesc = String("Manual Override (") + remMin + "m)";
            currentFan1Demand = DimmerManager::getInstance().getLevel(1);
        } else if (!effectivePresence) {
            currentFan1Demand = 0;
            fan1StatusDesc = "Standby (No Presence)";
            sm.requestFanSpeed(1, 0, "No Presence");
        } else {
            if (isAirBoostActive) {
                currentFan1Demand = scenarios.airBoostCeilingSpeed;
                fan1StatusDesc = String("Air Boost Active (") + currentFan1Demand + "%)";
                sm.requestFanSpeed(1, currentFan1Demand, "Air Boost Active");
            } else if (!env.isConnected || !sm.isTempValid()) {
                fan1StatusDesc = "Sensor Fault / Offline";
            } else {
                uint8_t targetSpeed = calculateCeilingFanSpeed(currentTemp, effectivePresence);
                currentFan1Demand = targetSpeed;

                if (targetSpeed == 0) {
                    fan1StatusDesc = String("Temp (") + String(currentTemp, 1) + "°C): Standby / OFF";
                } else {
                    fan1StatusDesc = String("Temp (") + String(currentTemp, 1) + "°C): " + targetSpeed + "%";
                }
                if (isNightQuietActive && targetSpeed == scenarios.nightMaxSpeed) {
                    fan1StatusDesc += String(" [Night Capped ") + scenarios.nightMaxSpeed + "%]";
                }

                sm.requestFanSpeed(1, targetSpeed, "Ceiling Fan Auto");
            }
        }
    } else {
        currentFan1Demand = DimmerManager::getInstance().getState(1) ? DimmerManager::getInstance().getLevel(1) : 0;
        fan1StatusDesc = "Manual Mode";
    }

    // 3. Evaluate Exhaust Fan (Fan 2) Automation
    if (exhaustFan.mode != ExhaustAutoMode::MANUAL) {
        if (sm.isManualOverrideActive(SafetyDeviceType::FAN, 2)) {
            uint32_t remMin = (sm.getManualOverrideRemainingSec(SafetyDeviceType::FAN, 2) + 59) / 60;
            fan2StatusDesc = String("Manual Override (") + remMin + "m)";
            currentFan2Demand = DimmerManager::getInstance().getLevel(2);
        } else if (!effectivePresence && !isAirBoostActive) {
            currentFan2Demand = 0;
            fan2StatusDesc = "Standby (No Presence)";
            sm.requestFanSpeed(2, 0, "No Presence");
        } else {
            if (isAirBoostActive) {
                currentFan2Demand = scenarios.airBoostExhaustSpeed;
                fan2StatusDesc = String("Emergency Air Boost (") + currentFan2Demand + "%)";
                sm.requestFanSpeed(2, currentFan2Demand, "Emergency Air Boost");
            } else if (!env.isConnected || !sm.isCo2Valid()) {
                fan2StatusDesc = "Sensor Fault / Offline";
            } else {
                uint8_t targetSpeed = calculateExhaustFanSpeed(currentCo2, currentTemp, effectivePresence);
                currentFan2Demand = targetSpeed;

                if (exhaustFan.mode == ExhaustAutoMode::CO2_BASED) {
                    fan2StatusDesc = String("CO₂ (") + currentCo2 + " ppm): " + targetSpeed + "%";
                } else if (exhaustFan.mode == ExhaustAutoMode::TEMP_BASED) {
                    fan2StatusDesc = String("Temp (") + String(currentTemp, 1) + "°C): " + targetSpeed + "%";
                } else if (exhaustFan.mode == ExhaustAutoMode::COMBINED) {
                    fan2StatusDesc = String("Demand (CO₂: ") + lastCo2Demand + "%, Temp: " + lastTempDemand + "%): " + targetSpeed + "%";
                }
                if (isNightQuietActive && targetSpeed == scenarios.nightMaxSpeed) {
                    fan2StatusDesc += String(" [Night Capped ") + scenarios.nightMaxSpeed + "%]";
                }

                sm.requestFanSpeed(2, targetSpeed, "Exhaust Fan Auto");
            }
        }
    } else {
        currentFan2Demand = DimmerManager::getInstance().getState(2) ? DimmerManager::getInstance().getLevel(2) : 0;
        fan2StatusDesc = "Manual Mode";
    }
}

AutomationStatus AutomationManager::getStatus() const {
    AutomationStatus st;
    st.masterEnabled = masterEnabled;
    st.presenceActive = AutomationSafetyManager::getInstance().isPresenceConfirmed();

    if (pendingAutoOff) {
        uint32_t elapsed = (millis() - presenceLostTime) / 1000UL;
        st.inactivityRemainingSec = (elapsed < autoOffDelaySec) ? (autoOffDelaySec - elapsed) : 0;
    } else {
        st.inactivityRemainingSec = st.presenceActive ? autoOffDelaySec : 0;
    }

    st.fan1AutoActive = isCeilingFanAutoEnabled();
    st.fan1Demand = currentFan1Demand;
    st.fan1StatusText = fan1StatusDesc;

    st.fan2AutoActive = isExhaustFanAutoEnabled();
    st.fan2Demand = currentFan2Demand;
    st.fan2StatusText = fan2StatusDesc;

    st.nightQuietActive = isNightQuietActive;
    st.airBoostActive = isAirBoostActive;
    st.currentPreset = currentPreset;
    return st;
}

void AutomationManager::evaluateLighting(uint32_t now, bool presence) {
    if (!masterEnabled) {
        for (uint8_t i = 0; i < NUM_RELAYS; i++) {
            lightConfigs[i].statusDesc = "Master Automation OFF";
        }
        return;
    }

    AutomationSafetyManager& sm = AutomationSafetyManager::getInstance();
    RelayManager& rm = RelayManager::getInstance();
    uint8_t curHour = getCurrentHour();
    uint8_t curMin = getCurrentMinute();
    uint16_t curMinOfDay = (uint16_t)curHour * 60 + curMin;

    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
        uint8_t ch = i + 1;
        LightAutomationConfig& cfg = lightConfigs[i];

        if (!cfg.enabled || cfg.mode == LightAutoMode::MANUAL) {
            cfg.statusDesc = "Manual Mode";
            relayPendingOff[i] = false;
            continue;
        }

        // Check Manual Override Lockout
        if (sm.isManualOverrideActive(SafetyDeviceType::RELAY, ch)) {
            uint32_t remMin = (sm.getManualOverrideRemainingSec(SafetyDeviceType::RELAY, ch) + 59) / 60;
            cfg.statusDesc = String("Manual Override (") + remMin + "m)";
            continue;
        }

        // Check Presets
        if (currentPreset == AutomationPreset::CLEANING) {
            cfg.statusDesc = "Cleaning Mode (Hold ON)";
            sm.requestRelayState(ch, true, "Preset Cleaning");
            continue;
        } else if (currentPreset == AutomationPreset::AWAY) {
            cfg.statusDesc = "Away Mode (Hold OFF)";
            sm.requestRelayState(ch, false, "Preset Away");
            continue;
        }

        // Mode 3: SCHEDULED
        if (cfg.mode == LightAutoMode::SCHEDULED) {
            uint16_t startMin = (uint16_t)cfg.startHour * 60 + cfg.startMin;
            uint16_t endMin = (uint16_t)cfg.endHour * 60 + cfg.endMin;
            bool inSchedule = false;
            if (startMin < endMin) {
                inSchedule = (curMinOfDay >= startMin && curMinOfDay < endMin);
            } else { // Overnight schedule e.g. 18:00 to 02:00
                inSchedule = (curMinOfDay >= startMin || curMinOfDay < endMin);
            }

            bool target = inSchedule;
            cfg.statusDesc = inSchedule ? "Scheduled (Active)" : "Scheduled (Standby)";
            sm.requestRelayState(ch, target, "Scheduled Rule");
            continue;
        }

        // Mode 1: PRESENCE, Mode 2: PRESENCE_TIME
        bool inTimeWindow = true;
        if (cfg.mode == LightAutoMode::PRESENCE_TIME) {
            uint16_t startMin = (uint16_t)cfg.startHour * 60 + cfg.startMin;
            uint16_t endMin = (uint16_t)cfg.endHour * 60 + cfg.endMin;
            if (startMin < endMin) {
                inTimeWindow = (curMinOfDay >= startMin && curMinOfDay < endMin);
            } else {
                inTimeWindow = (curMinOfDay >= startMin || curMinOfDay < endMin);
            }
        }

        if (!inTimeWindow) {
            cfg.statusDesc = "Time Restricted (07:00-22:00)";
            sm.requestRelayState(ch, false, "Time Window Closed");
            continue;
        }

        // Night Protection check (22:00 - 07:00)
        if (isNightQuietActive && !cfg.nightAllowed) {
            // At night, if light is OFF, DO NOT turn it ON automatically!
            if (!rm.getRelay(ch)) {
                cfg.statusDesc = "Night Blocked (No Auto-ON)";
                continue;
            }
        }

        // Presence Evaluation with Per-Channel Inactivity Timeout
        uint32_t chanTimeout = (cfg.inactivityTimeoutSec > 0) ? cfg.inactivityTimeoutSec : autoOffDelaySec;

        if (presence) {
            // Occupied
            relayPendingOff[i] = false;
            cfg.statusDesc = "Active (Presence)";
            sm.requestRelayState(ch, true, "Presence Detected");
        } else {
            // Unoccupied
            if (!relayPendingOff[i]) {
                relayPendingOff[i] = true;
                relayPresenceLostTime[i] = now;
            }

            if (now - relayPresenceLostTime[i] >= chanTimeout * 1000UL) {
                // Timeout reached -> Turn OFF
                cfg.statusDesc = "Unoccupied (Off)";
                sm.requestRelayState(ch, false, "Inactivity Timeout");
            } else {
                // Counting down
                uint32_t rem = (chanTimeout * 1000UL - (now - relayPresenceLostTime[i])) / 1000UL;
                cfg.statusDesc = String("Auto-OFF in ") + rem + "s";
            }
        }
    }
}

String AutomationManager::toJson() const {
    JsonDocument doc;
    doc["master"] = masterEnabled;
    doc["off_delay"] = autoOffDelaySec;
    doc["preset"] = (uint8_t)currentPreset;

    // Ceiling Fan
    JsonObject f1 = doc["fan1"].to<JsonObject>();
    f1["enabled"] = ceilingFan.enabled;
    f1["target_temp"] = ceilingFan.targetTemp;
    f1["temp_low"] = ceilingFan.tempLow;
    f1["temp_med"] = ceilingFan.tempMed;
    f1["temp_high"] = ceilingFan.tempHigh;
    f1["speed_low"] = ceilingFan.speedLow;
    f1["speed_med"] = ceilingFan.speedMed;
    f1["speed_high"] = ceilingFan.speedHigh;
    f1["demand"] = currentFan1Demand;
    f1["status"] = fan1StatusDesc;

    // Exhaust Fan
    JsonObject f2 = doc["fan2"].to<JsonObject>();
    f2["mode"] = (uint8_t)exhaustFan.mode;
    f2["co2_low"] = exhaustFan.co2Low;
    f2["co2_med"] = exhaustFan.co2Med;
    f2["co2_high"] = exhaustFan.co2High;
    f2["co2_speed_low"] = exhaustFan.co2SpeedLow;
    f2["co2_speed_med"] = exhaustFan.co2SpeedMed;
    f2["co2_speed_high"] = exhaustFan.co2SpeedHigh;
    f2["temp_low"] = exhaustFan.tempLow;
    f2["temp_high"] = exhaustFan.tempHigh;
    f2["temp_speed_med"] = exhaustFan.tempSpeedMed;
    f2["temp_speed_high"] = exhaustFan.tempSpeedHigh;
    f2["demand"] = currentFan2Demand;
    f2["status"] = fan2StatusDesc;

    // Smart Scenarios
    JsonObject sc = doc["scenarios"].to<JsonObject>();
    sc["night_quiet"] = scenarios.nightQuietEnabled;
    sc["night_start"] = scenarios.nightStartHour;
    sc["night_end"] = scenarios.nightEndHour;
    sc["night_max_speed"] = scenarios.nightMaxSpeed;
    sc["air_boost"] = scenarios.airBoostEnabled;
    sc["air_boost_trigger"] = scenarios.airBoostTriggerCo2;
    sc["air_boost_clear"] = scenarios.airBoostClearCo2;
    sc["air_boost_exhaust"] = scenarios.airBoostExhaustSpeed;
    sc["air_boost_ceiling"] = scenarios.airBoostCeilingSpeed;
    sc["night_quiet_active"] = isNightQuietActive;
    sc["air_boost_active"] = isAirBoostActive;

    // Relays (6-Channel Lighting)
    JsonArray rArr = doc["relays"].to<JsonArray>();
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
        JsonObject r = rArr.add<JsonObject>();
        r["id"] = i + 1;
        r["auto"] = lightConfigs[i].enabled;
        r["mode"] = (uint8_t)lightConfigs[i].mode;
        r["night_allowed"] = lightConfigs[i].nightAllowed;
        r["timeout"] = lightConfigs[i].inactivityTimeoutSec;
        r["start_h"] = lightConfigs[i].startHour;
        r["start_m"] = lightConfigs[i].startMin;
        r["end_h"] = lightConfigs[i].endHour;
        r["end_m"] = lightConfigs[i].endMin;
        r["quiet_lvl"] = lightConfigs[i].quietLevel;
        r["status"] = lightConfigs[i].statusDesc;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool AutomationManager::fromJson(const JsonDocument& doc) {
    if (!doc["master"].isNull()) {
        masterEnabled = doc["master"].as<bool>();
    }
    if (!doc["off_delay"].isNull()) {
        autoOffDelaySec = doc["off_delay"].as<uint32_t>();
    }
    if (!doc["preset"].isNull()) {
        applyPreset((AutomationPreset)doc["preset"].as<uint8_t>());
    }

    // Ceiling Fan
    if (doc["fan1"].is<JsonObjectConst>()) {
        JsonObjectConst f1 = doc["fan1"].as<JsonObjectConst>();
        if (!f1["enabled"].isNull()) ceilingFan.enabled = f1["enabled"].as<bool>();
        if (!f1["target_temp"].isNull()) ceilingFan.targetTemp = f1["target_temp"].as<float>();
        if (!f1["temp_low"].isNull()) ceilingFan.tempLow = f1["temp_low"].as<float>();
        if (!f1["temp_med"].isNull()) ceilingFan.tempMed = f1["temp_med"].as<float>();
        if (!f1["temp_high"].isNull()) ceilingFan.tempHigh = f1["temp_high"].as<float>();
        if (!f1["speed_low"].isNull()) ceilingFan.speedLow = f1["speed_low"].as<uint8_t>();
        if (!f1["speed_med"].isNull()) ceilingFan.speedMed = f1["speed_med"].as<uint8_t>();
        if (!f1["speed_high"].isNull()) ceilingFan.speedHigh = f1["speed_high"].as<uint8_t>();
    }

    // Exhaust Fan
    if (doc["fan2"].is<JsonObjectConst>()) {
        JsonObjectConst f2 = doc["fan2"].as<JsonObjectConst>();
        if (!f2["mode"].isNull()) exhaustFan.mode = (ExhaustAutoMode)f2["mode"].as<uint8_t>();
        if (!f2["co2_low"].isNull()) exhaustFan.co2Low = f2["co2_low"].as<int16_t>();
        if (!f2["co2_med"].isNull()) exhaustFan.co2Med = f2["co2_med"].as<int16_t>();
        if (!f2["co2_high"].isNull()) exhaustFan.co2High = f2["co2_high"].as<int16_t>();
        if (!f2["co2_speed_low"].isNull()) exhaustFan.co2SpeedLow = f2["co2_speed_low"].as<uint8_t>();
        if (!f2["co2_speed_med"].isNull()) exhaustFan.co2SpeedMed = f2["co2_speed_med"].as<uint8_t>();
        if (!f2["co2_speed_high"].isNull()) exhaustFan.co2SpeedHigh = f2["co2_speed_high"].as<uint8_t>();
        if (!f2["temp_low"].isNull()) exhaustFan.tempLow = f2["temp_low"].as<float>();
        if (!f2["temp_high"].isNull()) exhaustFan.tempHigh = f2["temp_high"].as<float>();
        if (!f2["temp_speed_med"].isNull()) exhaustFan.tempSpeedMed = f2["temp_speed_med"].as<uint8_t>();
        if (!f2["temp_speed_high"].isNull()) exhaustFan.tempSpeedHigh = f2["temp_speed_high"].as<uint8_t>();
    }

    // Scenarios
    if (doc["scenarios"].is<JsonObjectConst>()) {
        JsonObjectConst sc = doc["scenarios"].as<JsonObjectConst>();
        if (!sc["night_quiet"].isNull()) scenarios.nightQuietEnabled = sc["night_quiet"].as<bool>();
        if (!sc["night_start"].isNull()) scenarios.nightStartHour = sc["night_start"].as<uint8_t>();
        if (!sc["night_end"].isNull()) scenarios.nightEndHour = sc["night_end"].as<uint8_t>();
        if (!sc["night_max_speed"].isNull()) scenarios.nightMaxSpeed = sc["night_max_speed"].as<uint8_t>();
        if (!sc["air_boost"].isNull()) scenarios.airBoostEnabled = sc["air_boost"].as<bool>();
        if (!sc["air_boost_trigger"].isNull()) scenarios.airBoostTriggerCo2 = sc["air_boost_trigger"].as<int16_t>();
        if (!sc["air_boost_clear"].isNull()) scenarios.airBoostClearCo2 = sc["air_boost_clear"].as<int16_t>();
        if (!sc["air_boost_exhaust"].isNull()) scenarios.airBoostExhaustSpeed = sc["air_boost_exhaust"].as<uint8_t>();
        if (!sc["air_boost_ceiling"].isNull()) scenarios.airBoostCeilingSpeed = sc["air_boost_ceiling"].as<uint8_t>();
    }

    // 6-Channel Lighting
    if (doc["relays"].is<JsonArrayConst>()) {
        for (JsonObjectConst r : doc["relays"].as<JsonArrayConst>()) {
            uint8_t id = r["id"] | 0;
            if (id >= 1 && id <= NUM_RELAYS) {
                uint8_t idx = id - 1;
                if (!r["auto"].isNull()) lightConfigs[idx].enabled = r["auto"].as<bool>();
                if (!r["mode"].isNull()) lightConfigs[idx].mode = (LightAutoMode)r["mode"].as<uint8_t>();
                if (!r["night_allowed"].isNull()) lightConfigs[idx].nightAllowed = r["night_allowed"].as<bool>();
                if (!r["timeout"].isNull()) lightConfigs[idx].inactivityTimeoutSec = r["timeout"].as<uint32_t>();
                if (!r["start_h"].isNull()) lightConfigs[idx].startHour = r["start_h"].as<uint8_t>();
                if (!r["start_m"].isNull()) lightConfigs[idx].startMin = r["start_m"].as<uint8_t>();
                if (!r["end_h"].isNull()) lightConfigs[idx].endHour = r["end_h"].as<uint8_t>();
                if (!r["end_m"].isNull()) lightConfigs[idx].endMin = r["end_m"].as<uint8_t>();
                if (!r["quiet_lvl"].isNull()) lightConfigs[idx].quietLevel = r["quiet_lvl"].as<uint8_t>();
            }
        }
    }

    saveSettings();
    return true;
}

uint8_t AutomationManager::calculateCeilingFanSpeed(float currentTemp, bool presence) {
    if (!presence) {
        lastAppliedFan1Speed = 0;
        return 0;
    }

    if (isAirBoostActive) {
        return scenarios.airBoostCeilingSpeed; // e.g. 40%
    }

    float tLow = ceilingFan.tempLow;   // 24.0°C
    float tMed = ceilingFan.tempMed;   // 27.0°C
    float tHigh = ceilingFan.tempHigh; // 30.0°C
    const float h = AutomationSafetyManager::getInstance().getConfig().tempHysteresis;

    uint8_t speed = lastAppliedFan1Speed;

    if (speed == 0) {
        // Must cross threshold + hysteresis to turn ON from standstill
        if (currentTemp >= tHigh + h) {
            speed = ceilingFan.speedHigh; // 100%
        } else if (currentTemp >= tMed + h) {
            speed = ceilingFan.speedMed;  // 70%
        } else if (currentTemp >= tLow + h) {
            speed = ceilingFan.speedLow;  // 40%
        }
    } else if (speed <= ceilingFan.speedLow) { // At 40%
        if (currentTemp >= tMed + h) {
            speed = ceilingFan.speedMed;  // Step up to 70%
        } else if (currentTemp < tLow - h) {
            speed = 0;                    // Step down to OFF
        }
    } else if (speed <= ceilingFan.speedMed) { // At 70%
        if (currentTemp >= tHigh + h) {
            speed = ceilingFan.speedHigh; // Step up to 100%
        } else if (currentTemp < tMed - h) {
            speed = ceilingFan.speedLow;  // Step down to 40%
        }
    } else { // At 100%
        if (currentTemp < tHigh - h) {
            speed = ceilingFan.speedMed;  // Step down to 70%
        }
    }

    // Apply Night Quiet Cap
    if (isNightQuietActive && speed > scenarios.nightMaxSpeed) {
        speed = scenarios.nightMaxSpeed;
    }

    return speed;
}

uint8_t AutomationManager::calculateExhaustFanSpeed(int16_t currentCo2, float currentTemp, bool presence) {
    if (!presence && !isAirBoostActive) {
        lastCo2Demand = 0;
        lastTempDemand = 0;
        lastAppliedFan2Speed = 0;
        return 0;
    }

    if (isAirBoostActive) {
        return scenarios.airBoostExhaustSpeed; // 100%
    }

    // 1. CO2 Demand calculation with configurable Schmitt-trigger hysteresis
    int16_t cLow = exhaustFan.co2Low;   // 800 ppm
    int16_t cMed = exhaustFan.co2Med;   // 1200 ppm
    int16_t cHigh = exhaustFan.co2High; // 1800 ppm
    const int16_t co2Hyst = AutomationSafetyManager::getInstance().getConfig().co2Hysteresis;

    if (lastCo2Demand == 0) {
        if (currentCo2 >= cHigh + co2Hyst) lastCo2Demand = exhaustFan.co2SpeedHigh;
        else if (currentCo2 >= cMed + co2Hyst) lastCo2Demand = exhaustFan.co2SpeedMed;
        else if (currentCo2 >= cLow + co2Hyst) lastCo2Demand = exhaustFan.co2SpeedLow;
    } else if (lastCo2Demand <= exhaustFan.co2SpeedLow) { // 40%
        if (currentCo2 >= cMed + co2Hyst) lastCo2Demand = exhaustFan.co2SpeedMed;
        else if (currentCo2 < cLow - co2Hyst) lastCo2Demand = 0;
    } else if (lastCo2Demand <= exhaustFan.co2SpeedMed) { // 70%
        if (currentCo2 >= cHigh + co2Hyst) lastCo2Demand = exhaustFan.co2SpeedHigh;
        else if (currentCo2 < cMed - co2Hyst) lastCo2Demand = exhaustFan.co2SpeedLow;
    } else { // 100%
        if (currentCo2 < cHigh - co2Hyst) lastCo2Demand = exhaustFan.co2SpeedMed;
    }

    // 2. Temperature Demand calculation with configurable Schmitt-trigger hysteresis
    float tLow = exhaustFan.tempLow;   // 26.0°C
    float tHigh = exhaustFan.tempHigh; // 30.0°C
    const float tempHyst = AutomationSafetyManager::getInstance().getConfig().tempHysteresis;

    if (lastTempDemand == 0) {
        if (currentTemp >= tHigh + tempHyst) lastTempDemand = exhaustFan.tempSpeedHigh;
        else if (currentTemp >= tLow + tempHyst) lastTempDemand = exhaustFan.tempSpeedMed;
    } else if (lastTempDemand <= exhaustFan.tempSpeedMed) { // 50%
        if (currentTemp >= tHigh + tempHyst) lastTempDemand = exhaustFan.tempSpeedHigh;
        else if (currentTemp < tLow - tempHyst) lastTempDemand = 0; // Turns off only below 25.5°C!
    } else { // 100%
        if (currentTemp < tHigh - tempHyst) lastTempDemand = exhaustFan.tempSpeedMed;
    }

    // 3. Arbitration
    uint8_t targetSpeed = 0;
    if (exhaustFan.mode == ExhaustAutoMode::CO2_BASED) {
        targetSpeed = lastCo2Demand;
    } else if (exhaustFan.mode == ExhaustAutoMode::TEMP_BASED) {
        targetSpeed = lastTempDemand;
    } else if (exhaustFan.mode == ExhaustAutoMode::COMBINED) {
        targetSpeed = (lastCo2Demand >= lastTempDemand) ? lastCo2Demand : lastTempDemand;
    }

    // Apply Night Quiet Cap
    if (isNightQuietActive && targetSpeed > scenarios.nightMaxSpeed) {
        targetSpeed = scenarios.nightMaxSpeed;
    }

    return targetSpeed;
}

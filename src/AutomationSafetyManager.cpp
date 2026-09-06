#include "AutomationSafetyManager.h"
#include "RelayManager.h"
#include "DimmerManager.h"

AutomationSafetyManager& AutomationSafetyManager::getInstance() {
    static AutomationSafetyManager instance;
    return instance;
}

AutomationSafetyManager::AutomationSafetyManager() {
    bootMillis = millis();
}

void AutomationSafetyManager::begin() {
    bootMillis = millis();
    loadSettings();

    // Synchronize initial hardware states
    RelayManager& rm = RelayManager::getInstance();
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
        relayCurrentState[i] = rm.getRelay(i + 1);
        lastRelayToggleTime[i] = millis();
    }

    DimmerManager& dm = DimmerManager::getInstance();
    for (uint8_t i = 0; i < 2; i++) {
        fanHardwareSpeed[i] = dm.getState(i + 1) ? dm.getLevel(i + 1) : 0;
        fanTargetSpeed[i] = fanHardwareSpeed[i];
        fanRampSpeed[i] = fanHardwareSpeed[i];
        lastFanStateChangeTime[i] = millis();
        lastFanRampStepTime[i] = millis();
    }

    Serial.println("[Safety] Universal Automation Safety Manager initialized.");
}

bool AutomationSafetyManager::isPowerStabilized() const {
    return (millis() - bootMillis) >= (config.powerRecoveryDelaySec * 1000UL);
}

uint32_t AutomationSafetyManager::getStabilizationRemainingSec() const {
    uint32_t elapsed = (millis() - bootMillis) / 1000UL;
    if (elapsed >= config.powerRecoveryDelaySec) return 0;
    return config.powerRecoveryDelaySec - elapsed;
}

void AutomationSafetyManager::updateSensors(float rawTemp, int16_t rawCo2, bool rawPresence, bool isSensorConnected) {
    sensorOnline = isSensorConnected;

    // Extreme Bounds Protection
    tempValid = (isSensorConnected && rawTemp >= 0.0f && rawTemp <= 60.0f);
    co2Valid = (isSensorConnected && rawCo2 >= 300 && rawCo2 <= 5000);

    // 1. Temperature Moving Average (5 samples)
    if (tempValid) {
        tempBuffer[tempBufIdx] = rawTemp;
        tempBufIdx = (tempBufIdx + 1) % TEMP_WINDOW_SIZE;
        if (tempBufCount < TEMP_WINDOW_SIZE) tempBufCount++;

        float sum = 0;
        for (uint8_t i = 0; i < tempBufCount; i++) {
            sum += tempBuffer[i];
        }
        filteredTemp = round((sum / tempBufCount) * 10.0f) / 10.0f;
    }

    // 2. CO2 Moving Average (10 samples)
    if (co2Valid) {
        co2Buffer[co2BufIdx] = rawCo2;
        co2BufIdx = (co2BufIdx + 1) % CO2_WINDOW_SIZE;
        if (co2BufCount < CO2_WINDOW_SIZE) co2BufCount++;

        int32_t sum = 0;
        for (uint8_t i = 0; i < co2BufCount; i++) {
            sum += co2Buffer[i];
        }
        filteredCo2 = (int16_t)(sum / co2BufCount);
    }

    // 3. Debounced Presence Filter
    if (rawPresence) {
        if (presenceTrueCount < 255) presenceTrueCount++;
        presenceFalseCount = 0;
        if (presenceTrueCount >= 2) {
            presenceConfirmed = true;
        }
    } else {
        if (presenceFalseCount < 255) presenceFalseCount++;
        presenceTrueCount = 0;
        if (presenceFalseCount >= 3) {
            presenceConfirmed = false;
        }
    }
}

void AutomationSafetyManager::update() {
    uint32_t now = millis();

    // Advance smooth fan speed ramping
    advanceFanRamp(now);

    // Auto-expire manual overrides
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
        if (manualOverrideRelay[i]) {
            if (now - manualOverrideStartRelay[i] >= config.manualOverrideDurationSec * 1000UL) {
                manualOverrideRelay[i] = false;
                Serial.printf("[Safety] Manual override expired on Relay %u. Returned to automation.\n", i + 1);
            }
        }
    }

    for (uint8_t i = 0; i < 2; i++) {
        if (manualOverrideFan[i]) {
            if (now - manualOverrideStartFan[i] >= config.manualOverrideDurationSec * 1000UL) {
                manualOverrideFan[i] = false;
                Serial.printf("[Safety] Manual override expired on Fan %u. Returned to automation.\n", i + 1);
            }
        }
    }
}

void AutomationSafetyManager::advanceFanRamp(uint32_t now) {
    DimmerManager& dm = DimmerManager::getInstance();

    for (uint8_t i = 0; i < 2; i++) {
        uint8_t fanId = i + 1;
        uint8_t tgt = fanTargetSpeed[i];
        uint8_t cur = fanHardwareSpeed[i];

        if (cur != tgt) {
            if (now - lastFanRampStepTime[i] >= config.fanRampIntervalMs) {
                uint8_t nextSpeed = cur;
                uint8_t step = config.fanRampStepPercent;

                if (tgt > cur) {
                    nextSpeed = (cur + step > tgt) ? tgt : (cur + step);
                } else {
                    nextSpeed = (cur < step || cur - step < tgt) ? tgt : (cur - step);
                }

                uint8_t prevSpeed = cur;
                fanHardwareSpeed[i] = nextSpeed;
                lastFanRampStepTime[i] = now;

                if (nextSpeed > 0) {
                    dm.setState(fanId, true);
                    dm.setLevel(fanId, nextSpeed, true);
                } else {
                    dm.setState(fanId, false);
                    dm.setLevel(fanId, 0, true);
                }

                lastFanStateChangeTime[i] = now;
                Serial.printf("[Safety] Fan %u ramped: %u%% -> %u%% (Target: %u%%)\n", fanId, prevSpeed, nextSpeed, tgt);
            }
        }
    }
}

bool AutomationSafetyManager::requestRelayState(uint8_t relayId, bool targetState, const char* reason) {
    if (relayId < 1 || relayId > NUM_RELAYS) return false;
    uint8_t idx = relayId - 1;
    uint32_t now = millis();

    // 1. Power Recovery Protection
    if (!isPowerStabilized()) {
        return false;
    }

    // 2. Manual Override Lockout
    if (isManualOverrideActive(SafetyDeviceType::RELAY, relayId)) {
        return false;
    }

    RelayManager& rm = RelayManager::getInstance();
    bool curState = rm.getRelay(relayId);

    // If already at target state, clear any pending request
    if (curState == targetState) {
        relayHasPending[idx] = false;
        return true;
    }

    // 3. Automation Decision Confirmation Delay (5s default)
    if (config.decisionDelayLightMs > 0) {
        if (!relayHasPending[idx] || relayPendingTarget[idx] != targetState) {
            relayPendingTarget[idx] = targetState;
            relayHasPending[idx] = true;
            relayPendingFirstCross[idx] = now;
            return false; // Waiting for decision confirmation
        }

        if (now - relayPendingFirstCross[idx] < config.decisionDelayLightMs) {
            return false; // Still within confirmation window
        }
    }

    // 4. Minimum State Change Dwell Time
    // Turning OFF: Check minimum ON time (30s)
    if (curState && !targetState) {
        uint32_t onDuration = now - lastRelayToggleTime[idx];
        if (onDuration < config.lightMinOnTimeSec * 1000UL) {
            uint32_t rem = (config.lightMinOnTimeSec * 1000UL - onDuration) / 1000UL;
            // Suppress spammy log, log only once per pending cycle
            static uint32_t lastLog[NUM_RELAYS] = {0};
            if (now - lastLog[idx] >= 5000UL) {
                lastLog[idx] = now;
                Serial.printf("[Safety] Relay %u: Turn-OFF rejected (Min ON dwell remaining: %us)\n", relayId, rem);
            }
            return false;
        }
    }

    // Turning ON: Check minimum OFF time (30s)
    if (!curState && targetState) {
        uint32_t offDuration = now - lastRelayToggleTime[idx];
        if (offDuration < config.lightMinOffTimeSec * 1000UL) {
            uint32_t rem = (config.lightMinOffTimeSec * 1000UL - offDuration) / 1000UL;
            static uint32_t lastLog[NUM_RELAYS] = {0};
            if (now - lastLog[idx] >= 5000UL) {
                lastLog[idx] = now;
                Serial.printf("[Safety] Relay %u: Turn-ON rejected (Min OFF dwell remaining: %us)\n", relayId, rem);
            }
            return false;
        }
    }

    // 5. Anti-Chatter Contact Protection (10s minimum between ANY toggle)
    if (now - lastRelayToggleTime[idx] < config.relayMinIntervalSec * 1000UL) {
        return false;
    }

    // Safety checks passed! Actuate hardware
    relayHasPending[idx] = false;
    rm.setRelay(relayId, targetState, true);
    lastRelayToggleTime[idx] = now;
    relayCurrentState[idx] = targetState;

    Serial.printf("[Safety] Relay %u switched to %s (Reason: %s)\n", relayId, targetState ? "ON" : "OFF", reason);
    return true;
}

bool AutomationSafetyManager::requestFanSpeed(uint8_t fanId, uint8_t targetSpeed, const char* reason) {
    if (fanId < 1 || fanId > 2) return false;
    uint8_t idx = fanId - 1;
    uint32_t now = millis();

    // 1. Power Recovery Protection
    if (!isPowerStabilized()) {
        return false;
    }

    // 2. Manual Override Lockout
    if (isManualOverrideActive(SafetyDeviceType::FAN, fanId)) {
        return false;
    }

    // If already targeting this speed, nothing more to do
    if (fanTargetSpeed[idx] == targetSpeed) {
        fanHasPending[idx] = false;
        return true;
    }

    // 3. Automation Decision Confirmation Delay (15s default)
    if (config.decisionDelayFanMs > 0) {
        if (!fanHasPending[idx] || fanPendingSpeed[idx] != targetSpeed) {
            fanPendingSpeed[idx] = targetSpeed;
            fanHasPending[idx] = true;
            fanPendingFirstCross[idx] = now;
            return false; // Waiting for decision confirmation
        }

        if (now - fanPendingFirstCross[idx] < config.decisionDelayFanMs) {
            return false; // Still within confirmation window
        }
    }

    // 4. Minimum Runtime / Minimum OFF Time (60s default)
    bool isCurrentlyRunning = (fanHardwareSpeed[idx] > 0);
    bool willRun = (targetSpeed > 0);

    // Turning OFF
    if (isCurrentlyRunning && !willRun) {
        uint32_t runDuration = now - lastFanStateChangeTime[idx];
        if (runDuration < config.fanMinRunTimeSec * 1000UL) {
            uint32_t rem = (config.fanMinRunTimeSec * 1000UL - runDuration) / 1000UL;
            static uint32_t lastFanLog[2] = {0};
            if (now - lastFanLog[idx] >= 5000UL) {
                lastFanLog[idx] = now;
                Serial.printf("[Safety] Fan %u: Shutdown rejected (Min runtime remaining: %us)\n", fanId, rem);
            }
            return false;
        }
    }

    // Turning ON from standstill
    if (!isCurrentlyRunning && willRun) {
        uint32_t offDuration = now - lastFanStateChangeTime[idx];
        if (offDuration < config.fanMinOffTimeSec * 1000UL) {
            uint32_t rem = (config.fanMinOffTimeSec * 1000UL - offDuration) / 1000UL;
            static uint32_t lastFanLog[2] = {0};
            if (now - lastFanLog[idx] >= 5000UL) {
                lastFanLog[idx] = now;
                Serial.printf("[Safety] Fan %u: Start rejected (Min OFF dwell remaining: %us)\n", fanId, rem);
            }
            return false;
        }
    }

    // Safety checks passed! Assign target speed for non-blocking ramping
    fanHasPending[idx] = false;
    fanTargetSpeed[idx] = targetSpeed;

    Serial.printf("[Safety] Fan %u target speed set to %u%% (Reason: %s, smooth ramp initiated)\n", fanId, targetSpeed, reason);
    return true;
}

void AutomationSafetyManager::notifyManualIntervention(SafetyDeviceType type, uint8_t index) {
    uint32_t now = millis();
    if (type == SafetyDeviceType::RELAY) {
        if (index >= 1 && index <= NUM_RELAYS) {
            manualOverrideRelay[index - 1] = true;
            manualOverrideStartRelay[index - 1] = now;
            lastRelayToggleTime[index - 1] = now; // Also reset anti-chatter dwell
            Serial.printf("[Safety] Manual command on Relay %u. Automation locked for %u min.\n", index, config.manualOverrideDurationSec / 60);
        }
    } else if (type == SafetyDeviceType::FAN) {
        if (index >= 1 && index <= 2) {
            manualOverrideFan[index - 1] = true;
            manualOverrideStartFan[index - 1] = now;
            lastFanStateChangeTime[index - 1] = now;
            // Sync target speed immediately with hardware on manual action
            DimmerManager& dm = DimmerManager::getInstance();
            fanHardwareSpeed[index - 1] = dm.getState(index) ? dm.getLevel(index) : 0;
            fanTargetSpeed[index - 1] = fanHardwareSpeed[index - 1];
            Serial.printf("[Safety] Manual command on Fan %u. Automation locked for %u min.\n", index, config.manualOverrideDurationSec / 60);
        }
    }
}

bool AutomationSafetyManager::isManualOverrideActive(SafetyDeviceType type, uint8_t index) const {
    uint32_t now = millis();
    if (type == SafetyDeviceType::RELAY) {
        if (index >= 1 && index <= NUM_RELAYS) {
            if (manualOverrideRelay[index - 1]) {
                return (now - manualOverrideStartRelay[index - 1] < config.manualOverrideDurationSec * 1000UL);
            }
        }
    } else if (type == SafetyDeviceType::FAN) {
        if (index >= 1 && index <= 2) {
            if (manualOverrideFan[index - 1]) {
                return (now - manualOverrideStartFan[index - 1] < config.manualOverrideDurationSec * 1000UL);
            }
        }
    }
    return false;
}

uint32_t AutomationSafetyManager::getManualOverrideRemainingSec(SafetyDeviceType type, uint8_t index) const {
    uint32_t now = millis();
    if (type == SafetyDeviceType::RELAY) {
        if (index >= 1 && index <= NUM_RELAYS && manualOverrideRelay[index - 1]) {
            uint32_t elapsed = (now - manualOverrideStartRelay[index - 1]) / 1000UL;
            if (elapsed < config.manualOverrideDurationSec) {
                return config.manualOverrideDurationSec - elapsed;
            }
        }
    } else if (type == SafetyDeviceType::FAN) {
        if (index >= 1 && index <= 2 && manualOverrideFan[index - 1]) {
            uint32_t elapsed = (now - manualOverrideStartFan[index - 1]) / 1000UL;
            if (elapsed < config.manualOverrideDurationSec) {
                return config.manualOverrideDurationSec - elapsed;
            }
        }
    }
    return 0;
}

void AutomationSafetyManager::clearManualOverride(SafetyDeviceType type, uint8_t index) {
    if (type == SafetyDeviceType::RELAY && index >= 1 && index <= NUM_RELAYS) {
        manualOverrideRelay[index - 1] = false;
        Serial.printf("[Safety] Manual override cleared on Relay %u.\n", index);
    } else if (type == SafetyDeviceType::FAN && index >= 1 && index <= 2) {
        manualOverrideFan[index - 1] = false;
        Serial.printf("[Safety] Manual override cleared on Fan %u.\n", index);
    }
}

void AutomationSafetyManager::clearAllManualOverrides() {
    for (uint8_t i = 0; i < NUM_RELAYS; i++) manualOverrideRelay[i] = false;
    for (uint8_t i = 0; i < 2; i++) manualOverrideFan[i] = false;
    Serial.println("[Safety] All manual overrides cleared. Automation resumed on all channels.");
}

void AutomationSafetyManager::setConfig(const SafetyConfig& newConfig) {
    config = newConfig;
    saveSettings();
}

void AutomationSafetyManager::loadSettings() {
    if (prefs.begin(NVS_NAMESPACE_SAFETY, true)) {
        config.tempHysteresis = prefs.getFloat("t_hyst", 1.5f);
        config.co2Hysteresis = prefs.getShort("c_hyst", 300);
        config.lightMinOnTimeSec = prefs.getULong("l_min_on", 30);
        config.lightMinOffTimeSec = prefs.getULong("l_min_off", 30);
        config.fanMinRunTimeSec = prefs.getULong("f_min_on", 60);
        config.fanMinOffTimeSec = prefs.getULong("f_min_off", 60);
        config.relayMinIntervalSec = prefs.getULong("r_min_int", 10);
        config.fanRampStepPercent = prefs.getUChar("f_ramp_st", 10);
        config.fanRampIntervalMs = prefs.getULong("f_ramp_int", 5000);
        config.decisionDelayLightMs = prefs.getULong("d_del_l", 5000);
        config.decisionDelayFanMs = prefs.getULong("d_del_f", 15000);
        config.manualOverrideDurationSec = prefs.getULong("man_dur", 1800);
        config.powerRecoveryDelaySec = prefs.getULong("pwr_del", 30);
        prefs.end();
        Serial.println("[Safety] Safety configuration loaded from NVS.");
    }
}

void AutomationSafetyManager::saveSettings() {
    if (prefs.begin(NVS_NAMESPACE_SAFETY, false)) {
        prefs.putFloat("t_hyst", config.tempHysteresis);
        prefs.putShort("c_hyst", config.co2Hysteresis);
        prefs.putULong("l_min_on", config.lightMinOnTimeSec);
        prefs.putULong("l_min_off", config.lightMinOffTimeSec);
        prefs.putULong("f_min_on", config.fanMinRunTimeSec);
        prefs.putULong("f_min_off", config.fanMinOffTimeSec);
        prefs.putULong("r_min_int", config.relayMinIntervalSec);
        prefs.putUChar("f_ramp_st", config.fanRampStepPercent);
        prefs.putULong("f_ramp_int", config.fanRampIntervalMs);
        prefs.putULong("d_del_l", config.decisionDelayLightMs);
        prefs.putULong("d_del_f", config.decisionDelayFanMs);
        prefs.putULong("man_dur", config.manualOverrideDurationSec);
        prefs.putULong("pwr_del", config.powerRecoveryDelaySec);
        prefs.end();
        Serial.println("[Safety] Safety configuration saved to NVS.");
    }
}

String AutomationSafetyManager::toJson() const {
    JsonDocument doc;
    doc["t_hyst"] = config.tempHysteresis;
    doc["c_hyst"] = config.co2Hysteresis;
    doc["l_min_on"] = config.lightMinOnTimeSec;
    doc["l_min_off"] = config.lightMinOffTimeSec;
    doc["f_min_on"] = config.fanMinRunTimeSec;
    doc["f_min_off"] = config.fanMinOffTimeSec;
    doc["r_min_int"] = config.relayMinIntervalSec;
    doc["f_ramp_step"] = config.fanRampStepPercent;
    doc["f_ramp_int"] = config.fanRampIntervalMs / 1000UL;
    doc["d_del_light"] = config.decisionDelayLightMs / 1000UL;
    doc["d_del_fan"] = config.decisionDelayFanMs / 1000UL;
    doc["man_dur"] = config.manualOverrideDurationSec;
    doc["pwr_delay"] = config.powerRecoveryDelaySec;

    doc["filtered_temp"] = filteredTemp;
    doc["filtered_co2"] = filteredCo2;
    doc["pwr_stabilized"] = isPowerStabilized();
    doc["pwr_rem"] = getStabilizationRemainingSec();

    JsonArray orRelays = doc["override_relays"].to<JsonArray>();
    for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
        JsonObject o = orRelays.add<JsonObject>();
        o["id"] = i;
        o["active"] = isManualOverrideActive(SafetyDeviceType::RELAY, i);
        o["rem_sec"] = getManualOverrideRemainingSec(SafetyDeviceType::RELAY, i);
    }

    JsonArray orFans = doc["override_fans"].to<JsonArray>();
    for (uint8_t i = 1; i <= 2; i++) {
        JsonObject o = orFans.add<JsonObject>();
        o["id"] = i;
        o["active"] = isManualOverrideActive(SafetyDeviceType::FAN, i);
        o["rem_sec"] = getManualOverrideRemainingSec(SafetyDeviceType::FAN, i);
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool AutomationSafetyManager::fromJson(const JsonDocument& doc) {
    if (!doc["t_hyst"].isNull()) config.tempHysteresis = doc["t_hyst"].as<float>();
    if (!doc["c_hyst"].isNull()) config.co2Hysteresis = doc["c_hyst"].as<int16_t>();
    if (!doc["l_min_on"].isNull()) config.lightMinOnTimeSec = doc["l_min_on"].as<uint32_t>();
    if (!doc["l_min_off"].isNull()) config.lightMinOffTimeSec = doc["l_min_off"].as<uint32_t>();
    if (!doc["f_min_on"].isNull()) config.fanMinRunTimeSec = doc["f_min_on"].as<uint32_t>();
    if (!doc["f_min_off"].isNull()) config.fanMinOffTimeSec = doc["f_min_off"].as<uint32_t>();
    if (!doc["r_min_int"].isNull()) config.relayMinIntervalSec = doc["r_min_int"].as<uint32_t>();
    if (!doc["f_ramp_step"].isNull()) config.fanRampStepPercent = doc["f_ramp_step"].as<uint8_t>();
    if (!doc["f_ramp_int"].isNull()) config.fanRampIntervalMs = doc["f_ramp_int"].as<uint32_t>() * 1000UL;
    if (!doc["d_del_light"].isNull()) config.decisionDelayLightMs = doc["d_del_light"].as<uint32_t>() * 1000UL;
    if (!doc["d_del_fan"].isNull()) config.decisionDelayFanMs = doc["d_del_fan"].as<uint32_t>() * 1000UL;
    if (!doc["man_dur"].isNull()) config.manualOverrideDurationSec = doc["man_dur"].as<uint32_t>();
    if (!doc["pwr_delay"].isNull()) config.powerRecoveryDelaySec = doc["pwr_delay"].as<uint32_t>();

    saveSettings();
    return true;
}

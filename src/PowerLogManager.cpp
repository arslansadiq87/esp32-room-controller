#include "PowerLogManager.h"
#include "PowerMonitor.h"
#include "StorageManager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

PowerLogManager& PowerLogManager::getInstance() {
    static PowerLogManager instance;
    return instance;
}

PowerLogManager::PowerLogManager() {
    memset(&header, 0, sizeof(header));
    header.magic = HEADER_MAGIC;
    header.maxEntries = POWER_LOG_MAX_ENTRIES;
}

bool PowerLogManager::begin() {
    loadConfig();

    if (!StorageManager::getInstance().isMounted()) {
        Serial.println("[POWER_LOG] StorageManager LittleFS not mounted. Cannot initialize log.");
        return false;
    }

    loadHeader();
    recalculate24hStats();

    lastSampleTime = millis();
    lastIntegrationTime = millis();
    currentIntervalWattSeconds = 0.0;

    Serial.printf("[POWER_LOG] Initialized. Logging: %s | Count: %u/%u | Daily: %.3f kWh | Monthly: %.3f kWh\n",
                  loggingEnabled ? "ENABLED" : "DISABLED",
                  header.count, header.maxEntries,
                  header.dailyKwh, header.monthlyKwh);
    return true;
}

void PowerLogManager::loadConfig() {
    // 1. Try reading from Winbond LittleFS
    if (StorageManager::getInstance().fileExists(POWER_CONFIG_FILE)) {
        String json = StorageManager::getInstance().readFile(POWER_CONFIG_FILE);
        JsonDocument doc;
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            if (!doc["logging_enabled"].isNull()) {
                loggingEnabled = doc["logging_enabled"].as<bool>();
                return;
            }
        }
    }

    // 2. Fallback to NVS Preferences
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_POWER, true)) {
        loggingEnabled = prefs.getBool("log_en", false);
        prefs.end();
    }
}

void PowerLogManager::saveConfig() {
    // 1. Save to Winbond LittleFS
    JsonDocument doc;
    doc["logging_enabled"] = loggingEnabled;
    String out;
    serializeJson(doc, out);
    StorageManager::getInstance().writeFile(POWER_CONFIG_FILE, out);

    // 2. Mirror in NVS
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_POWER, false)) {
        prefs.putBool("log_en", loggingEnabled);
        prefs.end();
    }
}

void PowerLogManager::loadHeader() {
    if (LittleFS.exists(POWER_LOG_BIN_FILE)) {
        File file = LittleFS.open(POWER_LOG_BIN_FILE, "r");
        if (file) {
            if (file.size() >= sizeof(PowerLogHeader)) {
                file.read((uint8_t*)&header, sizeof(PowerLogHeader));
                file.close();
                if (header.magic == HEADER_MAGIC && header.maxEntries == POWER_LOG_MAX_ENTRIES) {
                    return; // Header valid
                }
            } else {
                file.close();
            }
        }
    }

    // Initialize fresh header
    memset(&header, 0, sizeof(header));
    header.magic = HEADER_MAGIC;
    header.maxEntries = POWER_LOG_MAX_ENTRIES;
    header.lastDaySec = getCurrentTimestamp();
    header.lastMonthDay = 1;
    saveHeader();
}

void PowerLogManager::saveHeader() {
    File file = LittleFS.open(POWER_LOG_BIN_FILE, "r+");
    if (!file) {
        // Create file if not existing
        file = LittleFS.open(POWER_LOG_BIN_FILE, "w");
    }
    if (file) {
        file.seek(0, SeekSet);
        file.write((const uint8_t*)&header, sizeof(PowerLogHeader));
        file.close();
    }
}

uint32_t PowerLogManager::getCurrentTimestamp() const {
    time_t now = time(nullptr);
    if (now > 1700000000) { // Valid NTP epoch (post Nov 2023)
        return (uint32_t)now;
    }
    // Monotonic relative time fallback: seconds since boot
    return millis() / 1000;
}

void PowerLogManager::setLoggingEnabled(bool enable) {
    if (loggingEnabled != enable) {
        loggingEnabled = enable;
        saveConfig();
        if (loggingEnabled) {
            lastSampleTime = millis();
            lastIntegrationTime = millis();
            currentIntervalWattSeconds = 0.0;
            // Record immediate baseline point if buffer is empty
            if (header.count == 0) {
                recordSample();
            }
        }
        Serial.printf("[POWER_LOG] Power logging toggled: %s\n", enable ? "ON" : "OFF");
    }
}

void PowerLogManager::update() {
    if (!loggingEnabled) return;

    uint32_t now = millis();

    // 1. High-rate energy integration (~every 1000ms)
    if (now - lastIntegrationTime >= 1000) {
        uint32_t dtMs = now - lastIntegrationTime;
        lastIntegrationTime = now;

        PowerData pwr = PowerMonitor::getInstance().getData();
        if (pwr.connectionState == SENSOR_CONNECTED && pwr.acInputState == AC_PRESENT) {
            float activeWatts = (pwr.activePower > 0.0f) ? pwr.activePower : 0.0f;

            double wattSeconds = activeWatts * ((double)dtMs / 1000.0);
            currentIntervalWattSeconds += wattSeconds;

            double deltaKwh = wattSeconds / 3600000.0;
            header.dailyKwh += (float)deltaKwh;
            header.monthlyKwh += (float)deltaKwh;
        }

        // Daily rollover check
        uint32_t curTime = getCurrentTimestamp();
        if (curTime > 1700000000) {
            // NTP synchronized: check midnight transition
            struct tm timeinfo;
            time_t tNow = (time_t)curTime;
            localtime_r(&tNow, &timeinfo);
            if (header.lastMonthDay != (uint32_t)timeinfo.tm_mday) {
                // New day!
                if (timeinfo.tm_mday == 1) {
                    header.monthlyKwh = 0.0f; // New month
                }
                header.dailyKwh = 0.0f;
                header.lastMonthDay = (uint32_t)timeinfo.tm_mday;
                header.lastDaySec = curTime;
                saveHeader();
                Serial.printf("[POWER_LOG] New day rollover at %02d:%02d. Daily reset to 0.\n",
                              timeinfo.tm_hour, timeinfo.tm_min);
            }
        } else {
            // Uptime fallback: reset every 86,400 seconds (24 hours)
            if (curTime - header.lastDaySec >= 86400) {
                header.dailyKwh = 0.0f;
                header.lastDaySec = curTime;
                saveHeader();
                Serial.println("[POWER_LOG] 24-hour rollover reached. Daily reset to 0.");
            }
        }
    }

    // 2. Interval sample commit to Winbond flash (every POWER_LOG_INTERVAL_SEC)
    if (now - lastSampleTime >= (POWER_LOG_INTERVAL_SEC * 1000)) {
        recordSample();
        lastSampleTime = now;
    }
}

void PowerLogManager::recordSample() {
    PowerData pwr = PowerMonitor::getInstance().getData();
    float sampleDurationSec = (float)POWER_LOG_INTERVAL_SEC;

    // Average power over the interval
    float intervalAvgWatts = 0.0f;
    float intervalWh = 0.0f;
    if (pwr.connectionState == SENSOR_CONNECTED && pwr.acInputState == AC_PRESENT) {
        if (currentIntervalWattSeconds > 0.0 && sampleDurationSec > 0.0f) {
            intervalAvgWatts = (float)(currentIntervalWattSeconds / sampleDurationSec);
            intervalWh = (float)(currentIntervalWattSeconds / 3600.0);
        } else {
            intervalAvgWatts = pwr.activePower;
        }
    }
    currentIntervalWattSeconds = 0.0; // Reset for next interval

    PowerLogEntry entry;
    entry.timestamp = getCurrentTimestamp();
    entry.activePower = (intervalAvgWatts >= 0.0f) ? intervalAvgWatts : 0.0f;
    entry.voltage = (pwr.acInputState == AC_PRESENT) ? pwr.voltageRms : 0.0f;
    entry.energyWh = intervalWh;

    // Open ring buffer file in read/write mode
    File file = LittleFS.open(POWER_LOG_BIN_FILE, "r+");
    if (!file) {
        file = LittleFS.open(POWER_LOG_BIN_FILE, "w");
    }

    if (file) {
        // Calculate byte position for circular head index
        size_t writePos = sizeof(PowerLogHeader) + (header.headIndex * sizeof(PowerLogEntry));
        file.seek(writePos, SeekSet);
        file.write((const uint8_t*)&entry, sizeof(PowerLogEntry));

        // Advance circular FIFO head pointer
        header.headIndex = (header.headIndex + 1) % header.maxEntries;
        if (header.count < header.maxEntries) {
            header.count++;
        }

        // Commit updated header to flash
        file.seek(0, SeekSet);
        file.write((const uint8_t*)&header, sizeof(PowerLogHeader));
        file.close();

        recalculate24hStats();

        Serial.printf("[POWER_LOG] Sample recorded @ slot %u: %.1f W, %.1f V, %.3f Wh | Daily: %.3f kWh\n",
                      header.headIndex, entry.activePower, entry.voltage, entry.energyWh, header.dailyKwh);
    } else {
        Serial.println("[POWER_LOG] Failed to open ring buffer file on Winbond flash!");
    }
}

void PowerLogManager::recalculate24hStats() {
    if (header.count == 0) {
        peakPower24h = 0.0f;
        avgPower24h = 0.0f;
        return;
    }

    File file = LittleFS.open(POWER_LOG_BIN_FILE, "r");
    if (!file) return;

    // Determine how many entries to look back (up to 288 for 24h)
    uint32_t samplesToRead = min((uint32_t)POWER_LOG_24H_SAMPLES, header.count);
    float maxP = 0.0f;
    double sumP = 0.0;
    uint32_t validSamples = 0;

    for (uint32_t i = 0; i < samplesToRead; i++) {
        // Step backwards from current headIndex - 1
        int32_t slot = (int32_t)header.headIndex - 1 - i;
        if (slot < 0) {
            slot += header.maxEntries;
        }

        size_t seekPos = sizeof(PowerLogHeader) + (slot * sizeof(PowerLogEntry));
        file.seek(seekPos, SeekSet);

        PowerLogEntry e;
        if (file.read((uint8_t*)&e, sizeof(PowerLogEntry)) == sizeof(PowerLogEntry)) {
            if (e.activePower > maxP) maxP = e.activePower;
            sumP += e.activePower;
            validSamples++;
        }
    }
    file.close();

    peakPower24h = maxP;
    avgPower24h = (validSamples > 0) ? (float)(sumP / validSamples) : 0.0f;
}

String PowerLogManager::getHistoryJson() const {
    JsonDocument doc;
    doc["logging_enabled"] = loggingEnabled;
    doc["daily_kwh"] = round(header.dailyKwh * 1000.0f) / 1000.0f;
    doc["monthly_kwh"] = round(header.monthlyKwh * 1000.0f) / 1000.0f;
    doc["peak_power"] = round(peakPower24h * 10.0f) / 10.0f;
    doc["avg_power"] = round(avgPower24h * 10.0f) / 10.0f;
    doc["sample_interval"] = POWER_LOG_INTERVAL_SEC;

    JsonArray points = doc["history"].to<JsonArray>();

    if (header.count > 0 && LittleFS.exists(POWER_LOG_BIN_FILE)) {
        File file = LittleFS.open(POWER_LOG_BIN_FILE, "r");
        if (file) {
            uint32_t samplesToRead = min((uint32_t)POWER_LOG_24H_SAMPLES, header.count);

            // Read from oldest to newest
            for (int32_t i = samplesToRead - 1; i >= 0; i--) {
                int32_t slot = (int32_t)header.headIndex - 1 - i;
                if (slot < 0) {
                    slot += header.maxEntries;
                }

                size_t seekPos = sizeof(PowerLogHeader) + (slot * sizeof(PowerLogEntry));
                file.seek(seekPos, SeekSet);

                PowerLogEntry e;
                if (file.read((uint8_t*)&e, sizeof(PowerLogEntry)) == sizeof(PowerLogEntry)) {
                    JsonObject pt = points.add<JsonObject>();
                    pt["t"] = e.timestamp;
                    pt["w"] = round(e.activePower * 10.0f) / 10.0f;
                    pt["v"] = round(e.voltage * 10.0f) / 10.0f;
                }
            }
            file.close();
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void PowerLogManager::clearLogs() {
    memset(&header, 0, sizeof(header));
    header.magic = HEADER_MAGIC;
    header.maxEntries = POWER_LOG_MAX_ENTRIES;
    header.lastDaySec = getCurrentTimestamp();
    header.lastMonthDay = 1;
    saveHeader();

    peakPower24h = 0.0f;
    avgPower24h = 0.0f;
    currentIntervalWattSeconds = 0.0;
    Serial.println("[POWER_LOG] All logs and counters reset on Winbond flash.");
}

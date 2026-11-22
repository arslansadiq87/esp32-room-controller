#include "MmWaveManager.h"
#include "StorageManager.h"
#include <ArduinoJson.h>
#include <Preferences.h>

MmWaveManager& MmWaveManager::getInstance() {
    static MmWaveManager instance;
    return instance;
}

MmWaveManager::MmWaveManager() {
    telemetry.targetState = 0;
    telemetry.isPresence = false;
    telemetry.isMotion = false;
    telemetry.isBreathing = false;
    telemetry.distance = 0.0f;
    telemetry.speed = 0.0f;
    telemetry.energy = 0;
    telemetry.direction = 1;
    telemetry.isConnected = false;
    telemetry.statusLed = false;
    telemetry.outputLed = false;
}

void MmWaveManager::loadSettings() {
    bool savedStatus = false;
    bool savedOutput = false;

    // 1. Attempt to load from Winbond LittleFS file
    if (StorageManager::getInstance().fileExists("/radar_led.json")) {
        String content = StorageManager::getInstance().readFile("/radar_led.json");
        JsonDocument doc;
        if (deserializeJson(doc, content) == DeserializationError::Ok) {
            if (!doc["status_led"].isNull()) savedStatus = doc["status_led"].as<bool>();
            if (!doc["output_led"].isNull()) savedOutput = doc["output_led"].as<bool>();
            Serial.printf("[MMWAVE] Restored LED state from Winbond LittleFS: Status=%s, Out=%s\n",
                          savedStatus ? "ON" : "OFF", savedOutput ? "ON" : "OFF");
            telemetry.statusLed = savedStatus;
            telemetry.outputLed = savedOutput;
            return;
        }
    }

    // 2. Fallback to NVS Preferences
    Preferences prefs;
    if (prefs.begin("rc_radar", true)) {
        savedStatus = prefs.getBool("status_led", false);
        savedOutput = prefs.getBool("output_led", false);
        prefs.end();
        Serial.printf("[MMWAVE] Restored LED state from NVS: Status=%s, Out=%s\n",
                      savedStatus ? "ON" : "OFF", savedOutput ? "ON" : "OFF");
    }

    telemetry.statusLed = savedStatus;
    telemetry.outputLed = savedOutput;
}

void MmWaveManager::saveSettings() {
    // 1. Persist to Winbond LittleFS file
    JsonDocument doc;
    doc["status_led"] = telemetry.statusLed;
    doc["output_led"] = telemetry.outputLed;
    String out;
    serializeJson(doc, out);
    StorageManager::getInstance().writeFile("/radar_led.json", out);

    // 2. Persist to NVS Preferences
    Preferences prefs;
    if (prefs.begin("rc_radar", false)) {
        prefs.putBool("status_led", telemetry.statusLed);
        prefs.putBool("output_led", telemetry.outputLed);
        prefs.end();
    }
    Serial.printf("[MMWAVE] Saved LED state to Winbond & NVS: Status=%s, Out=%s\n",
                  telemetry.statusLed ? "ON" : "OFF", telemetry.outputLed ? "ON" : "OFF");
}

bool MmWaveManager::begin(HardwareSerial& serial, uint32_t baud, uint8_t rxPin, uint8_t txPin) {
    serialPort = &serial;

    // 1. Always load persisted settings from Winbond LittleFS / NVS FIRST
    loadSettings();

    // 2. Create C4002Sensor instance on hardware UART0
    if (sensor) {
        delete sensor;
    }
    sensor = new C4002Sensor(serialPort, baud, rxPin, txPin);

    // 3. Retry sensor begin with backoff
    bool started = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (sensor->begin()) {
            started = true;
            break;
        }
        delay(150);
    }

    if (!started) {
        Serial.printf("[MMWAVE] Fermion C4002 begin failed after retries on RX:GPIO%d, TX:GPIO%d!\n", rxPin, txPin);
        telemetry.isConnected = false;
        return false;
    }

    telemetry.isConnected = true;
    lastValidPacketTime = millis();
    Serial.printf("[MMWAVE] Fermion C4002 mmWave sensor initialized on RX:GPIO%d, TX:GPIO%d @ %u bps\n", 
                  rxPin, txPin, baud);

    // 4. Apply restored LED configuration from Winbond SPI Flash to hardware sensor
    delay(60);
    sensor->setLed(telemetry.statusLed ? eLedOn : eLedOff, telemetry.outputLed ? eLedOn : eLedOff);
    delay(30);
    sensor->setRunLedState(telemetry.statusLed ? eLedOn : eLedOff);
    delay(30);
    sensor->setOutLedState(telemetry.outputLed ? eLedOn : eLedOff);

    Serial.printf("[MMWAVE] Applied restored LEDs to sensor: Status=%s, Out=%s\n",
                  telemetry.statusLed ? "ON" : "OFF", telemetry.outputLed ? "ON" : "OFF");

    return true;
}

void MmWaveManager::update() {
    if (!sensor) return;

    uint32_t now = millis();
    if (now - lastPollTime < MMWAVE_POLL_INTERVAL_MS) {
        return;
    }
    lastPollTime = now;

    // Query C4002 notification / state
    sRetResult_t note = sensor->getNoteInfo();
    if (note.noteType == eResult) {
        eTargetState_t state = sensor->getTargetState();
        telemetry.targetState = static_cast<uint8_t>(state);
        // ePresence (1): Static presence / breathing / micro-motion
        // eMotion (2): Active macro motion
        // eMotionOrPresence (3): Both motion & breathing
        telemetry.isPresence = (state == ePresence || state == eMotion || state == eMotionOrPresence);
        telemetry.isMotion = (state == eMotion || state == eMotionOrPresence);
        telemetry.isBreathing = (state == ePresence || state == eMotionOrPresence);

        // Read Target Information
        if (state == eMotion) {
            sMotionTarget_t motion = sensor->getMotionTargetInfo();
            telemetry.distance = motion.distance / 100.0f; // Convert cm to meters
            telemetry.speed = motion.speed;
            telemetry.energy = motion.energy;
            telemetry.direction = static_cast<uint8_t>(motion.direction);
        } else if (state == ePresence || state == eMotionOrPresence) {
            sPresenceTarget_t pres = sensor->getPresenceTargetInfo();
            telemetry.distance = pres.distance / 100.0f; // Convert cm to meters
            telemetry.speed = 0.0f;
            telemetry.energy = pres.energy;
            telemetry.direction = 1; // None
        } else {
            telemetry.distance = 0.0f;
            telemetry.speed = 0.0f;
            telemetry.energy = 0;
            telemetry.direction = 1;
        }

        telemetry.isConnected = true;
        lastValidPacketTime = now;

        // Trigger callback if presence state changed
        if (telemetry.targetState != lastReportedState) {
            lastReportedState = telemetry.targetState;
            if (changeCallback) {
                changeCallback(telemetry);
            }
            Serial.printf("[MMWAVE] State: %d | Presence: %s | Motion: %s | Breathing: %s | Dist: %.2fm | Energy: %d\n",
                          telemetry.targetState,
                          telemetry.isPresence ? "YES" : "NO",
                          telemetry.isMotion ? "YES" : "NO",
                          telemetry.isBreathing ? "YES" : "NO",
                          telemetry.distance,
                          telemetry.energy);
        }
    } else {
        // Check timeout (if no valid packet in 5 seconds, mark offline)
        if (now - lastValidPacketTime > 5000) {
            telemetry.isConnected = false;
        }
    }
}

MmWaveData MmWaveManager::getData() const {
    return telemetry;
}

void MmWaveManager::onPresenceChange(PresenceCallback callback) {
    changeCallback = callback;
}

bool MmWaveManager::setBothLeds(bool statusLed, bool outputLed) {
    if (!sensor) return false;
    eLedMode_t runMode = statusLed ? eLedOn : eLedOff;
    eLedMode_t outMode = outputLed ? eLedOn : eLedOff;
    
    // 1. Send unified setLed command
    sensor->setLed(runMode, outMode);
    delay(20);

    // 2. Also send specific run & out LED commands
    sensor->setRunLedState(runMode);
    delay(20);
    sensor->setOutLedState(outMode);

    telemetry.statusLed = statusLed;
    telemetry.outputLed = outputLed;

    // 3. Persist immediately to Winbond SPI Flash and NVS
    saveSettings();

    Serial.printf("[MMWAVE] LED command executed: Status=%s, Out=%s (Persisted to Winbond)\n", 
                  statusLed ? "ON" : "OFF", outputLed ? "ON" : "OFF");
    return true;
}

bool MmWaveManager::setStatusLed(bool enable) {
    return setBothLeds(enable, telemetry.outputLed);
}

bool MmWaveManager::setOutputLed(bool enable) {
    return setBothLeds(telemetry.statusLed, enable);
}

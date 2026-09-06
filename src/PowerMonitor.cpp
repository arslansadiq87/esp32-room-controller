#include "PowerMonitor.h"
#include "RelayManager.h"
#include "DimmerManager.h"

// ACS37800 Standard Register Addresses
static constexpr uint8_t REG_VRMS_IRMS   = 0x20; // [31:16] VRMS, [15:0] IRMS
static constexpr uint8_t REG_PACTIVE     = 0x21; // Active Power
static constexpr uint8_t REG_PAPPARENT   = 0x22; // Apparent Power
static constexpr uint8_t REG_PREACTIVE   = 0x23; // Reactive Power
static constexpr uint8_t REG_PFACTOR     = 0x24; // Power Factor
static constexpr uint8_t REG_STATUS      = 0x2B; // Status Flags

PowerMonitor& PowerMonitor::getInstance() {
    static PowerMonitor instance;
    return instance;
}

PowerMonitor::PowerMonitor() {
    telemetry.voltageRms = 0.0f;
    telemetry.currentRms = 0.0f;
    telemetry.activePower = 0.0f;
    telemetry.apparentPower = 0.0f;
    telemetry.reactivePower = 0.0f;
    telemetry.powerFactor = 0.0f;
    telemetry.lineFrequency = 0.0f;

    telemetry.rawVoltageRms = 0.0f;
    telemetry.rawCurrentRms = 0.0f;
    telemetry.rawActivePower = 0.0f;

    telemetry.filteredVoltageRms = 0.0f;
    telemetry.filteredCurrentRms = 0.0f;

    telemetry.connectionState = SENSOR_ERROR;
    telemetry.acInputState = AC_CHECKING;
    telemetry.isConnected = false;
    telemetry.isAcPresent = false;

    for (uint8_t i = 0; i < SAMPLES_COUNT; i++) {
        vSamples[i] = 0.0f;
        iSamples[i] = 0.0f;
    }
}

bool PowerMonitor::begin(TwoWire& wire, uint8_t address) {
    i2cWire = &wire;
    i2cAddress = address;
    bootTime = millis();
    sampleIdx = 0;
    samplesCollected = 0;

    // Check device physical presence on I2C bus
    i2cWire->beginTransmission(i2cAddress);
    if (i2cWire->endTransmission() == 0) {
        // Confirm register read works
        uint32_t testVal = 0;
        if (readRegister32(REG_VRMS_IRMS, testVal)) {
            telemetry.connectionState = SENSOR_CONNECTED;
            telemetry.isConnected = true;
            telemetry.acInputState = AC_CHECKING;
            telemetry.isAcPresent = false;
            i2cErrorCount = 0;
            Serial.printf("[ACS37800] IC Status: ONLINE (I2C 0x%02X) | Starting AC input detection...\n", i2cAddress);
        } else {
            telemetry.connectionState = SENSOR_ERROR;
            telemetry.isConnected = false;
            telemetry.acInputState = AC_NOT_DETECTED;
            telemetry.isAcPresent = false;
            Serial.printf("[ACS37800] IC Status: ERROR (I2C ACK at 0x%02X but register read failed)\n", i2cAddress);
        }
    } else {
        telemetry.connectionState = SENSOR_ERROR;
        telemetry.isConnected = false;
        telemetry.acInputState = AC_NOT_DETECTED;
        telemetry.isAcPresent = false;
        Serial.printf("[ACS37800] IC Status: OFFLINE (Address 0x%02X not responding)\n", i2cAddress);
    }

    return telemetry.isConnected;
}

bool PowerMonitor::readRegister32(uint8_t regAddr, uint32_t& value) {
    if (!i2cWire) return false;

    i2cWire->beginTransmission(i2cAddress);
    i2cWire->write(regAddr);
    if (i2cWire->endTransmission(false) != 0) {
        return false;
    }

    uint8_t bytesReceived = i2cWire->requestFrom(i2cAddress, (uint8_t)4);
    if (bytesReceived != 4) {
        return false;
    }

    // ACS37800 sends 32-bit words Little-Endian
    uint8_t b0 = i2cWire->read();
    uint8_t b1 = i2cWire->read();
    uint8_t b2 = i2cWire->read();
    uint8_t b3 = i2cWire->read();

    value = ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;
    return true;
}

bool PowerMonitor::writeRegister32(uint8_t regAddr, uint32_t value) {
    if (!i2cWire) return false;

    i2cWire->beginTransmission(i2cAddress);
    i2cWire->write(regAddr);
    i2cWire->write((uint8_t)(value & 0xFF));
    i2cWire->write((uint8_t)((value >> 8) & 0xFF));
    i2cWire->write((uint8_t)((value >> 16) & 0xFF));
    i2cWire->write((uint8_t)((value >> 24) & 0xFF));
    return (i2cWire->endTransmission() == 0);
}

void PowerMonitor::evaluateAcPresence(float instantVrms, float instantIrms) {
    // 1. Collect into 20-sample circular buffer
    vSamples[sampleIdx] = instantVrms;
    iSamples[sampleIdx] = instantIrms;
    sampleIdx = (sampleIdx + 1) % SAMPLES_COUNT;
    if (samplesCollected < SAMPLES_COUNT) {
        samplesCollected++;
    }

    // 2. Compute moving averages
    float sumV = 0.0f;
    float sumI = 0.0f;
    for (uint8_t i = 0; i < samplesCollected; i++) {
        sumV += vSamples[i];
        sumI += iSamples[i];
    }
    float avgV = (samplesCollected > 0) ? (sumV / (float)samplesCollected) : 0.0f;
    float avgI = (samplesCollected > 0) ? (sumI / (float)samplesCollected) : 0.0f;

    telemetry.filteredVoltageRms = avgV;
    telemetry.filteredCurrentRms = avgI;

    // 3. Startup stabilization window (~4 seconds / 16 samples)
    if (samplesCollected < 12 && (millis() - bootTime < 4000)) {
        telemetry.acInputState = AC_CHECKING;
        telemetry.isAcPresent = false;
        return;
    }

    // 4. Hysteresis-based AC Input Detection State Machine
    if (telemetry.acInputState != AC_PRESENT) {
        // Currently idle or checking: require voltage to exceed 25V RMS to trigger active state
        if (avgV >= AC_VOLTAGE_MIN_PRESENT) {
            telemetry.acInputState = AC_PRESENT;
            telemetry.isAcPresent = true;
            Serial.printf("[ACS37800] AC Input: DETECTED (Mains Voltage: %.1fV RMS, Load: %.2fA)\n", avgV, avgI);
        } else {
            telemetry.acInputState = AC_NOT_DETECTED;
            telemetry.isAcPresent = false;
        }
    } else {
        // Currently active: dropout threshold is 20V RMS
        if (avgV < AC_VOLTAGE_DROPOUT) {
            telemetry.acInputState = AC_NOT_DETECTED;
            telemetry.isAcPresent = false;
            Serial.printf("[ACS37800] AC Input: LOST (Voltage dropped to %.1fV RMS)\n", avgV);
        } else {
            telemetry.acInputState = AC_PRESENT;
            telemetry.isAcPresent = true;
        }
    }
}

void PowerMonitor::update() {
    uint32_t now = millis();
    if (now - lastReadTime < 250) { // Refresh telemetry at 4Hz
        return;
    }
    lastReadTime = now;

    // If communication previously failed, periodically retry I2C recovery every 5s
    if (telemetry.connectionState != SENSOR_CONNECTED) {
        if (i2cWire && (now - lastLogTime >= 5000)) {
            i2cWire->beginTransmission(i2cAddress);
            if (i2cWire->endTransmission() == 0) {
                telemetry.connectionState = SENSOR_CONNECTED;
                telemetry.isConnected = true;
                telemetry.acInputState = AC_CHECKING;
                i2cErrorCount = 0;
                Serial.println(F("[ACS37800] I2C Communication re-established!"));
            }
        }
        if (telemetry.connectionState != SENSOR_CONNECTED) {
            // Zero all readings when offline
            telemetry.voltageRms = 0.0f;
            telemetry.currentRms = 0.0f;
            telemetry.activePower = 0.0f;
            telemetry.apparentPower = 0.0f;
            telemetry.reactivePower = 0.0f;
            telemetry.powerFactor = 0.0f;
            telemetry.lineFrequency = 0.0f;
            telemetry.isConnected = false;
            telemetry.isAcPresent = false;
            return;
        }
    }

    uint32_t rawVrmsIrms = 0;
    uint32_t rawPactive = 0;
    uint32_t rawPfactor = 0;

    if (readRegister32(REG_VRMS_IRMS, rawVrmsIrms)) {
        i2cErrorCount = 0;

        // Upper 16 bits = VRMS raw, Lower 16 bits = IRMS raw
        uint16_t vrmsRaw = (rawVrmsIrms >> 16) & 0xFFFF;
        uint16_t irmsRaw = rawVrmsIrms & 0xFFFF;

        float instantV = (float)vrmsRaw * voltageMultiplier;
        float instantI = (float)irmsRaw * currentMultiplier;

        telemetry.rawVoltageRms = instantV;
        telemetry.rawCurrentRms = instantI;

        // Process noise filter & state machine
        evaluateAcPresence(instantV, instantI);

        if (telemetry.acInputState == AC_PRESENT) {
            // Normal AC mains operation: populate real telemetry
            telemetry.voltageRms = instantV;

            // Low current noise suppression (< 50mA)
            if (instantI < AC_CURRENT_NOISE_FLOOR) {
                telemetry.currentRms = 0.0f;
                telemetry.activePower = 0.0f;
                telemetry.apparentPower = 0.0f;
                telemetry.reactivePower = 0.0f;
                telemetry.powerFactor = 1.0f;
            } else {
                telemetry.currentRms = instantI;

                if (readRegister32(REG_PACTIVE, rawPactive)) {
                    int32_t pActiveSigned = (int32_t)rawPactive;
                    float calcPower = (float)pActiveSigned * (voltageMultiplier * currentMultiplier);
                    telemetry.activePower = (calcPower > 0.0f) ? calcPower : 0.0f;
                } else {
                    telemetry.activePower = telemetry.voltageRms * telemetry.currentRms;
                }

                if (readRegister32(REG_PFACTOR, rawPfactor)) {
                    int16_t pfRaw = (int16_t)(rawPfactor & 0xFFFF);
                    telemetry.powerFactor = constrain((float)pfRaw / 2048.0f, 0.0f, 1.0f);
                } else {
                    telemetry.powerFactor = 0.98f;
                }

                telemetry.apparentPower = telemetry.voltageRms * telemetry.currentRms;
            }

            telemetry.lineFrequency = (float)AC_MAINS_FREQ_HZ;
        } else {
            // AC Input Absent or Checking: block floating ADC noise from leaking into UI or Energy calculations
            telemetry.voltageRms = 0.0f;
            telemetry.currentRms = 0.0f;
            telemetry.activePower = 0.0f;
            telemetry.apparentPower = 0.0f;
            telemetry.reactivePower = 0.0f;
            telemetry.powerFactor = 0.0f;
            telemetry.lineFrequency = 0.0f;
        }
    } else {
        // Register read failed
        i2cErrorCount++;
        if (i2cErrorCount >= 5) {
            telemetry.connectionState = SENSOR_ERROR;
            telemetry.isConnected = false;
            telemetry.acInputState = AC_NOT_DETECTED;
            telemetry.isAcPresent = false;
            Serial.println(F("[ACS37800] I2C read failure threshold reached. Sensor marked OFFLINE."));
        }
    }

    // Periodic Industrial Debug Logging (every 5 seconds)
    if (now - lastLogTime >= 5000) {
        lastLogTime = now;
        if (telemetry.connectionState == SENSOR_CONNECTED) {
            if (telemetry.acInputState == AC_PRESENT) {
                Serial.printf("[ACS37800] IC Status: ONLINE | AC Input: PRESENT | Voltage: %.1fV RMS | Current: %.2fA | Power: %.1fW\n",
                              telemetry.voltageRms, telemetry.currentRms, telemetry.activePower);
            } else if (telemetry.acInputState == AC_CHECKING) {
                Serial.printf("[ACS37800] IC Status: ONLINE | AC Input: CHECKING (%d/20 samples | Raw V: %.1fV RMS | Raw I: %.2fA)\n",
                              samplesCollected, telemetry.rawVoltageRms, telemetry.rawCurrentRms);
            } else {
                Serial.printf("[ACS37800] IC Status: ONLINE | AC Input: NOT DETECTED | Voltage RMS: %.1fV (noise ignored) | Current RMS: %.2fA | Measurements blocked\n",
                              telemetry.rawVoltageRms, telemetry.rawCurrentRms);
            }
        } else {
            Serial.println(F("[ACS37800] IC Status: OFFLINE | Communication failed"));
        }
    }
}

PowerData PowerMonitor::getData() const {
    return telemetry;
}

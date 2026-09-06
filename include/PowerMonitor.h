#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

// Industrial Sensor & AC Presence States
enum SensorConnectionState {
    SENSOR_CONNECTED,
    SENSOR_ERROR
};

enum ACInputState {
    AC_PRESENT,
    AC_NOT_DETECTED,
    AC_CHECKING
};

struct PowerData {
    float voltageRms;    // Volts (V) - Validated measurement (0.0 if no AC)
    float currentRms;    // Amperes (A) - Validated measurement (0.0 if no AC)
    float activePower;   // Watts (W) - Validated measurement (0.0 if no AC)
    float apparentPower; // Volt-Amperes (VA)
    float reactivePower; // VAR
    float powerFactor;   // 0.00 to 1.00
    float lineFrequency; // Hertz (Hz)

    // Raw unsuppressed readings (for diagnostics & noise logging)
    float rawVoltageRms;
    float rawCurrentRms;
    float rawActivePower;

    // Filtered multi-sample moving averages
    float filteredVoltageRms;
    float filteredCurrentRms;

    SensorConnectionState connectionState;
    ACInputState acInputState;

    bool isConnected; // Backwards-compatible alias: (connectionState == SENSOR_CONNECTED)
    bool isAcPresent; // Helper: (acInputState == AC_PRESENT)
};

class PowerMonitor {
public:
    static PowerMonitor& getInstance();

    // Initialize I2C connection with ACS37800
    bool begin(TwoWire& wire = Wire, uint8_t address = ACS37800_I2C_ADDR);

    // Periodic telemetry update (non-blocking, e.g. 4Hz)
    void update();

    // Thread-safe telemetry snapshot
    PowerData getData() const;

    SensorConnectionState getConnectionState() const { return telemetry.connectionState; }
    ACInputState getAcInputState() const { return telemetry.acInputState; }

    // Calibration multipliers (preserved even when AC is absent)
    void setVoltageScaling(float scale) { voltageMultiplier = scale; }
    void setCurrentScaling(float scale) { currentMultiplier = scale; }

private:
    PowerMonitor();
    ~PowerMonitor() = default;

    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    // Low-level register read (32-bit registers)
    bool readRegister32(uint8_t regAddr, uint32_t& value);
    bool writeRegister32(uint8_t regAddr, uint32_t value);

    // Multi-sample noise filtering and AC presence arbitration
    void evaluateAcPresence(float instantVrms, float instantIrms);

    TwoWire* i2cWire = nullptr;
    uint8_t i2cAddress = ACS37800_I2C_ADDR;
    PowerData telemetry;
    uint32_t lastReadTime = 0;
    uint32_t lastLogTime = 0;
    uint32_t bootTime = 0;
    uint8_t i2cErrorCount = 0;

    // 20-sample circular buffers for noise filtering
    static constexpr uint8_t SAMPLES_COUNT = 20;
    float vSamples[SAMPLES_COUNT] = {0};
    float iSamples[SAMPLES_COUNT] = {0};
    uint8_t sampleIdx = 0;
    uint8_t samplesCollected = 0;

    // Detection Thresholds & Hysteresis
    static constexpr float AC_VOLTAGE_MIN_PRESENT = 25.0f; // Must exceed 25V RMS to declare AC Present
    static constexpr float AC_VOLTAGE_DROPOUT     = 20.0f; // Below 20V RMS declared AC Not Detected
    static constexpr float AC_CURRENT_NOISE_FLOOR = 0.05f; // 50mA noise threshold (ignore ADC/Hall drift)

    // Scaling factors based on voltage divider and shunt / Hall range
    float voltageMultiplier = 0.0084f; // Default calibration for 250V AC mains
    float currentMultiplier = 0.0012f; // Default calibration for 15A/30A sensor
};

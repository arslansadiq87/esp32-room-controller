#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"
#include <SensirionI2cStcc4.h>

struct Co2Data {
    int16_t co2Ppm;      // CO2 concentration in ppm (400 - 5000)
    float temperature;   // Temperature in degrees Celsius
    float humidity;      // Relative Humidity in %RH
    uint16_t sensorStatus; // Sensor internal status flag
    bool isConnected;    // Sensor online status
};

class Co2Sensor {
public:
    static Co2Sensor& getInstance();

    // Initialize Sensirion STCC4 sensor on I2C bus
    bool begin(TwoWire& wire = Wire, uint8_t address = STCC4_I2C_ADDR);

    // Periodic non-blocking measurement polling & auto-recovery
    void update();

    // Thread-safe telemetry snapshot
    Co2Data getData() const;

    bool isSensorInitialized() const { return isInitialized; }
    int16_t getLastError() const { return lastError; }

private:
    Co2Sensor();
    ~Co2Sensor() = default;

    Co2Sensor(const Co2Sensor&) = delete;
    Co2Sensor& operator=(const Co2Sensor&) = delete;

    TwoWire* i2cWire = nullptr;
    uint8_t i2cAddress = STCC4_I2C_ADDR;
    SensirionI2cStcc4 sensor;
    Co2Data telemetry;
    uint32_t lastReadTime = 0;
    uint32_t lastInitAttempt = 0;
    uint8_t consecutiveErrors = 0;
    int16_t lastError = 0;
    bool isInitialized = false;
};

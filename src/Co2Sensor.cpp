#include "Co2Sensor.h"

Co2Sensor& Co2Sensor::getInstance() {
    static Co2Sensor instance;
    return instance;
}

Co2Sensor::Co2Sensor() {
    telemetry.co2Ppm = 0;
    telemetry.temperature = 0.0f;
    telemetry.humidity = 0.0f;
    telemetry.sensorStatus = 0;
    telemetry.isConnected = false;
    isInitialized = false;
    lastReadTime = 0;
    lastInitAttempt = 0;
    consecutiveErrors = 0;
    lastError = 0;
}

bool Co2Sensor::begin(TwoWire& wire, uint8_t address) {
    i2cWire = &wire;
    i2cAddress = address;
    lastInitAttempt = millis();

    // 1. Initialize Sensirion driver instance with I2C bus & address
    sensor.begin(*i2cWire, i2cAddress);

    // 2. Wake sensor up if it was in sleep mode (address + write 0)
    sensor.exitSleepMode();
    delay(15);

    // 3. Stop continuous measurement if it was running before ESP32 soft reset/OTA
    sensor.stopContinuousMeasurement();
    delay(100);

    // 4. Verify sensor is responding on I2C address
    i2cWire->beginTransmission(i2cAddress);
    uint8_t i2cErr = i2cWire->endTransmission();
    if (i2cErr != 0) {
        // Try waking once more with a slight delay
        sensor.exitSleepMode();
        delay(20);
        i2cWire->beginTransmission(i2cAddress);
        i2cErr = i2cWire->endTransmission();
        if (i2cErr != 0) {
            Serial.printf("[CO2] Sensirion STCC4 not found at I2C address 0x%02X (i2cErr=%d)!\n", i2cAddress, i2cErr);
            telemetry.isConnected = false;
            isInitialized = false;
            lastError = i2cErr;
            return false;
        }
    }

    // 5. Read Product ID to confirm healthy bidirectional communication
    uint32_t prodId = 0;
    uint64_t serialNum = 0;
    int16_t err = sensor.getProductId(prodId, serialNum);
    if (err == 0) {
        Serial.printf("[CO2] Sensirion STCC4 Product ID: 0x%08X, Serial: %llu\n", prodId, serialNum);
    } else {
        Serial.printf("[CO2] Warning: getProductId returned code %d (proceeding with start)\n", err);
    }

    // 6. Start Continuous Measurement mode (1s sample interval)
    err = sensor.startContinuousMeasurement();
    if (err != 0) {
        Serial.printf("[CO2] STCC4 startContinuousMeasurement failed with error code: %d\n", err);
        telemetry.isConnected = false;
        isInitialized = false;
        lastError = err;
        return false;
    }

    telemetry.isConnected = true;
    isInitialized = true;
    consecutiveErrors = 0;
    lastError = 0;
    Serial.printf("[CO2] Sensirion STCC4 CO2 Sensor initialized at I2C 0x%02X (Continuous Mode)\n", i2cAddress);
    return true;
}

void Co2Sensor::update() {
    uint32_t now = millis();

    // Auto-recovery: If not initialized, retry periodically every 4 seconds
    if (!isInitialized) {
        if (now - lastInitAttempt >= 4000) {
            lastInitAttempt = now;
            if (i2cWire != nullptr) {
                Serial.println("[CO2] Retrying STCC4 sensor initialization...");
                begin(*i2cWire, i2cAddress);
            }
        }
        return;
    }

    // Rate-limit reads according to poll interval (1s)
    if (now - lastReadTime < CO2_POLL_INTERVAL_MS) {
        return;
    }
    lastReadTime = now;

    int16_t co2 = 0;
    float temp = 0.0f;
    float rh = 0.0f;
    uint16_t status = 0;

    int16_t err = sensor.readMeasurement(co2, temp, rh, status);
    lastError = err;
    if (err == 0) {
        telemetry.co2Ppm = co2;
        telemetry.temperature = temp;
        telemetry.humidity = rh;
        telemetry.sensorStatus = status;
        telemetry.isConnected = true;
        consecutiveErrors = 0;
    } else {
        consecutiveErrors++;
        // If 5 consecutive reads fail, mark offline and attempt recovery
        if (consecutiveErrors >= 5) {
            telemetry.isConnected = false;
            Serial.printf("[CO2] Consecutive read failures (%d), attempting sensor recovery...\n", consecutiveErrors);
            consecutiveErrors = 0;
            sensor.exitSleepMode();
            delay(10);
            sensor.stopContinuousMeasurement();
            delay(100);
            int16_t startErr = sensor.startContinuousMeasurement();
            if (startErr != 0) {
                isInitialized = false;
                lastInitAttempt = now;
            }
        }
    }
}

Co2Data Co2Sensor::getData() const {
    return telemetry;
}

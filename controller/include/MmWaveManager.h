#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <functional>
#include "Config.h"
#include <DFRobot_C4002.h>

struct MmWaveData {
    uint8_t targetState;    // 0: NoTarget, 1: Static Presence/Breathing, 2: Motion, 3: Motion & Breathing
    bool isPresence;        // Target detected (static presence or motion)
    bool isMotion;          // Active motion detected
    bool isBreathing;       // Micro-motion / breathing detected (static human presence)
    float distance;         // Target distance in meters
    float speed;            // Target speed in m/s
    uint8_t energy;         // Target signal energy
    uint8_t direction;      // 0: Away, 1: None, 2: Approaching
    bool isConnected;       // Sensor online status
    bool statusLed;         // C4002 Status/Run LED state (false = off)
    bool outputLed;         // C4002 Output detection LED state (false = off)
};

using PresenceCallback = std::function<void(const MmWaveData&)>;

// Derived class to access protected setLed method
class C4002Sensor : public DFRobot_C4002 {
public:
    using DFRobot_C4002::DFRobot_C4002;
    using DFRobot_C4002::setLed;
};

class MmWaveManager {
public:
    static MmWaveManager& getInstance();

    // Initialize UART0 interface and C4002 sensor
    bool begin(HardwareSerial& serial = Serial0, 
               uint32_t baud = C4002_BAUDRATE, 
               uint8_t rxPin = PIN_C4002_RX, 
               uint8_t txPin = PIN_C4002_TX);

    // Periodic non-blocking polling in cooperative loop
    void update();

    // Thread-safe telemetry snapshot
    MmWaveData getData() const;

    // Presence state change listener
    void onPresenceChange(PresenceCallback callback);

    // C4002 Onboard LED Control
    bool setBothLeds(bool statusLed, bool outputLed);
    bool setStatusLed(bool enable);
    bool setOutputLed(bool enable);

    // Persistence to Winbond SPI Flash and NVS
    void loadSettings();
    void saveSettings();

private:
    MmWaveManager();
    ~MmWaveManager() = default;

    MmWaveManager(const MmWaveManager&) = delete;
    MmWaveManager& operator=(const MmWaveManager&) = delete;

    HardwareSerial* serialPort = nullptr;
    C4002Sensor* sensor = nullptr;
    MmWaveData telemetry;
    uint32_t lastPollTime = 0;
    uint32_t lastValidPacketTime = 0;
    PresenceCallback changeCallback = nullptr;
    uint8_t lastReportedState = 255;
};

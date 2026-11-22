#pragma once

#include <Arduino.h>
#include "Config.h"
#include "esp_timer.h"

struct DimmerChannel {
    uint8_t pin;
    volatile uint8_t targetLevel;  // 0 to 100%
    volatile uint8_t currentLevel; // 0 to 100% (for smooth fading)
    volatile bool isEnabled;       // ON / OFF state
    volatile uint32_t firingDelayUs;
    esp_timer_handle_t timerHandle;
};

class DimmerManager {
public:
    static DimmerManager& getInstance();

    // Initialize hardware pins, interrupts, and timers
    void begin();

    // Channel control (channel: 1 or 2)
    void setLevel(uint8_t channel, uint8_t level, bool updateState = true);
    uint8_t getLevel(uint8_t channel) const;

    void setState(uint8_t channel, bool state);
    bool getState(uint8_t channel) const;

    void toggle(uint8_t channel);

    // Fade / step update in cooperative loop
    void update();

    // Internal ISR & Timer callbacks (must be public or friend for C callbacks)
    static void IRAM_ATTR onZeroCrossISR();
    static void IRAM_ATTR onTriac1Timer(void* arg);
    static void IRAM_ATTR onTriac2Timer(void* arg);

private:
    DimmerManager();
    ~DimmerManager() = default;

    DimmerManager(const DimmerManager&) = delete;
    DimmerManager& operator=(const DimmerManager&) = delete;

    void calculateDelay(uint8_t channelIndex);

    DimmerChannel channels[2];
    volatile uint32_t lastZeroCrossTimeUs = 0;
    bool initialized = false;
};

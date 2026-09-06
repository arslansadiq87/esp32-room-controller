#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"

enum class LedState {
    OFF,
    PULSING_BLUE,   // WiFi Connecting / AP Mode
    SOLID_GREEN,    // WiFi Connected & Normal Operation
    BLINKING_RED,   // WiFi / System Error
    WARNING_ORANGE, // High Load / Warning
    MANUAL          // Custom CRGB color
};

class StatusLed {
public:
    static StatusLed& getInstance();

    // Initialize FastLED on GPIO11
    void begin();

    // State control
    void setState(LedState state);
    LedState getState() const { return currentState; }

    void setColor(CRGB color);
    void setBrightness(uint8_t brightness);

    // Non-blocking animation loop
    void update();

private:
    StatusLed();
    ~StatusLed() = default;

    StatusLed(const StatusLed&) = delete;
    StatusLed& operator=(const StatusLed&) = delete;

    CRGB leds[NUM_LEDS];
    LedState currentState = LedState::OFF;
    uint8_t maxBrightness = 120;
    uint32_t lastAnimationTick = 0;
    uint8_t breathCounter = 0;
    int8_t breathDirection = 1;
    bool isBlinkOn = false;
};

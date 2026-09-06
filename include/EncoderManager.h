#pragma once

#include <Arduino.h>
#include <functional>
#include "Config.h"

// Callback type for encoder events: (encoderId: 1..2, newLevel: uint8_t, isEnabled: bool)
using EncoderEventCallback = std::function<void(uint8_t, uint8_t, bool)>;

struct EncoderState {
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinSW;
    
    volatile int16_t position;
    volatile uint8_t lastAB;
    
    // Switch debounce
    bool swStableState;
    bool swLastReading;
    uint32_t lastSwDebounceTime;
};

class EncoderManager {
public:
    static EncoderManager& getInstance();

    // Initialize pins and hardware interrupts
    void begin();

    // Periodic update for pushbutton debouncing and actions
    void update();

    // Register callback for UI/WebSocket sync
    void onEncoderEvent(EncoderEventCallback callback);

    // ISR routines
    static void IRAM_ATTR onEnc1ISR();
    static void IRAM_ATTR onEnc2ISR();

private:
    EncoderManager();
    ~EncoderManager() = default;

    EncoderManager(const EncoderManager&) = delete;
    EncoderManager& operator=(const EncoderManager&) = delete;

    void handleEncoderRotation(uint8_t encIndex, int8_t direction);
    void handleSwitchClick(uint8_t encIndex);

    EncoderState encoders[2];
    EncoderEventCallback eventCallback = nullptr;
    bool initialized = false;
};

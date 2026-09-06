#include "EncoderManager.h"
#include "DimmerManager.h"

// Lookup table for Gray Code quadrature decoder
// Returns: +1 for CW, -1 for CCW, 0 for invalid/no movement
static const int8_t QUAD_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

EncoderManager& EncoderManager::getInstance() {
    static EncoderManager instance;
    return instance;
}

EncoderManager::EncoderManager() {
    // Encoder 1 (Dimmer 1)
    encoders[0].pinA = PIN_ENC1_A;
    encoders[0].pinB = PIN_ENC1_B;
    encoders[0].pinSW = PIN_ENC1_SW;
    encoders[0].position = 0;
    encoders[0].lastAB = 0;
    encoders[0].swStableState = HIGH;
    encoders[0].swLastReading = HIGH;
    encoders[0].lastSwDebounceTime = 0;

    // Encoder 2 (Dimmer 2)
    encoders[1].pinA = PIN_ENC2_A;
    encoders[1].pinB = PIN_ENC2_B;
    encoders[1].pinSW = PIN_ENC2_SW;
    encoders[1].position = 0;
    encoders[1].lastAB = 0;
    encoders[1].swStableState = HIGH;
    encoders[1].swLastReading = HIGH;
    encoders[1].lastSwDebounceTime = 0;
}

void EncoderManager::begin() {
    if (initialized) return;

    for (int i = 0; i < 2; i++) {
        pinMode(encoders[i].pinA, INPUT_PULLUP);
        pinMode(encoders[i].pinB, INPUT_PULLUP);
        pinMode(encoders[i].pinSW, INPUT_PULLUP);

        uint8_t a = digitalRead(encoders[i].pinA);
        uint8_t b = digitalRead(encoders[i].pinB);
        encoders[i].lastAB = (a << 1) | b;
    }

    // Attach interrupts to both quadrature pins for Encoder 1
    attachInterrupt(digitalPinToInterrupt(encoders[0].pinA), EncoderManager::onEnc1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(encoders[0].pinB), EncoderManager::onEnc1ISR, CHANGE);

    // Attach interrupts to both quadrature pins for Encoder 2
    attachInterrupt(digitalPinToInterrupt(encoders[1].pinA), EncoderManager::onEnc2ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(encoders[1].pinB), EncoderManager::onEnc2ISR, CHANGE);

    initialized = true;
    Serial.println("[ENCODER] Initialized Encoder 1 (GPIO0,1,23) and Encoder 2 (GPIO2,3,10)");
}

void IRAM_ATTR EncoderManager::onEnc1ISR() {
    EncoderManager& self = EncoderManager::getInstance();
    uint8_t a = digitalRead(self.encoders[0].pinA);
    uint8_t b = digitalRead(self.encoders[0].pinB);
    uint8_t currAB = (a << 1) | b;

    uint8_t state = (self.encoders[0].lastAB << 2) | currAB;
    int8_t step = QUAD_TABLE[state & 0x0F];
    if (step != 0) {
        self.encoders[0].position += step;
    }
    self.encoders[0].lastAB = currAB;
}

void IRAM_ATTR EncoderManager::onEnc2ISR() {
    EncoderManager& self = EncoderManager::getInstance();
    uint8_t a = digitalRead(self.encoders[1].pinA);
    uint8_t b = digitalRead(self.encoders[1].pinB);
    uint8_t currAB = (a << 1) | b;

    uint8_t state = (self.encoders[1].lastAB << 2) | currAB;
    int8_t step = QUAD_TABLE[state & 0x0F];
    if (step != 0) {
        self.encoders[1].position += step;
    }
    self.encoders[1].lastAB = currAB;
}

void EncoderManager::handleEncoderRotation(uint8_t encIndex, int8_t direction) {
    uint8_t channel = encIndex + 1;
    DimmerManager& dimmer = DimmerManager::getInstance();
    int currentLevel = dimmer.getLevel(channel);

    // Step size: 5% change per detent
    int newLevel = constrain(currentLevel + (direction * 5), 0, 100);

    dimmer.setLevel(channel, (uint8_t)newLevel, true);
    Serial.printf("[ENCODER] Encoder %d rotated -> Dimmer %d Level: %d%%\n", channel, channel, newLevel);

    if (eventCallback) {
        eventCallback(channel, (uint8_t)newLevel, dimmer.getState(channel));
    }
}

void EncoderManager::handleSwitchClick(uint8_t encIndex) {
    uint8_t channel = encIndex + 1;
    DimmerManager& dimmer = DimmerManager::getInstance();
    dimmer.toggle(channel);

    bool state = dimmer.getState(channel);
    uint8_t level = dimmer.getLevel(channel);
    Serial.printf("[ENCODER] Encoder %d Button Clicked -> Dimmer %d State: %s (Level: %d%%)\n", 
                  channel, channel, state ? "ON" : "OFF", level);

    if (eventCallback) {
        eventCallback(channel, level, state);
    }
}

void EncoderManager::onEncoderEvent(EncoderEventCallback callback) {
    eventCallback = callback;
}

void EncoderManager::update() {
    uint32_t now = millis();

    for (int i = 0; i < 2; i++) {
        // Process rotary position steps (standard encoder generates 4 quadrature steps per physical detent)
        if (abs(encoders[i].position) >= 2) {
            int8_t dir = (encoders[i].position > 0) ? 1 : -1;
            encoders[i].position = 0;
            handleEncoderRotation(i, dir);
        }

        // Process Pushbutton with debounce
        bool reading = digitalRead(encoders[i].pinSW);
        if (reading != encoders[i].swLastReading) {
            encoders[i].lastSwDebounceTime = now;
        }

        if ((now - encoders[i].lastSwDebounceTime) > 40) {
            if (reading != encoders[i].swStableState) {
                encoders[i].swStableState = reading;
                // Button pressed (Active LOW)
                if (encoders[i].swStableState == LOW) {
                    handleSwitchClick(i);
                }
            }
        }

        encoders[i].swLastReading = reading;
    }
}

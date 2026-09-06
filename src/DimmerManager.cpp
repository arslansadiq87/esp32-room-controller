#include "DimmerManager.h"
#include "RelayManager.h"
#include <esp_rom_sys.h>

DimmerManager& DimmerManager::getInstance() {
    static DimmerManager instance;
    return instance;
}

DimmerManager::DimmerManager() {
    channels[0].pin = PIN_TRIAC_1;
    channels[0].targetLevel = 50;
    channels[0].currentLevel = 50;
    channels[0].isEnabled = false;
    channels[0].firingDelayUs = MAX_FIRING_DELAY_US;
    channels[0].timerHandle = nullptr;

    channels[1].pin = PIN_TRIAC_2;
    channels[1].targetLevel = 50;
    channels[1].currentLevel = 50;
    channels[1].isEnabled = false;
    channels[1].firingDelayUs = MAX_FIRING_DELAY_US;
    channels[1].timerHandle = nullptr;
}

void DimmerManager::begin() {
    if (initialized) return;

    // Configure Triac Output Pins
    pinMode(channels[0].pin, OUTPUT);
    digitalWrite(channels[0].pin, LOW);

    pinMode(channels[1].pin, OUTPUT);
    digitalWrite(channels[1].pin, LOW);

    // Create high-resolution hardware timers for both Triac channels
    esp_timer_create_args_t timer1Args = {
        .callback = &DimmerManager::onTriac1Timer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "triac1_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer1Args, &channels[0].timerHandle);

    esp_timer_create_args_t timer2Args = {
        .callback = &DimmerManager::onTriac2Timer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "triac2_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer2Args, &channels[1].timerHandle);

    // Initial delay calculation
    calculateDelay(0);
    calculateDelay(1);

    // Configure Zero-Cross Detection Interrupt Pin
    pinMode(PIN_ZC_DETECT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ZC_DETECT), DimmerManager::onZeroCrossISR, RISING);

    initialized = true;
    Serial.println("[DIMMER] Initialized Zero-Cross on GPIO5, Triacs on GPIO4 & GPIO18");
}

void IRAM_ATTR DimmerManager::onZeroCrossISR() {
    DimmerManager& self = DimmerManager::getInstance();
    uint32_t nowUs = (uint32_t)esp_timer_get_time();

    // Glitch filter: Ignore noise pulses shorter than 4ms (half cycle at 50Hz = 10ms, 60Hz = 8.33ms)
    if ((nowUs - self.lastZeroCrossTimeUs) < 4000) {
        return;
    }
    self.lastZeroCrossTimeUs = nowUs;

    // Channel 1 Firing Schedule
    if (self.channels[0].isEnabled && self.channels[0].currentLevel > 0) {
        esp_timer_stop(self.channels[0].timerHandle);
        esp_timer_start_once(self.channels[0].timerHandle, self.channels[0].firingDelayUs);
    } else {
        digitalWrite(self.channels[0].pin, LOW);
    }

    // Channel 2 Firing Schedule
    if (self.channels[1].isEnabled && self.channels[1].currentLevel > 0) {
        esp_timer_stop(self.channels[1].timerHandle);
        esp_timer_start_once(self.channels[1].timerHandle, self.channels[1].firingDelayUs);
    } else {
        digitalWrite(self.channels[1].pin, LOW);
    }
}

void IRAM_ATTR DimmerManager::onTriac1Timer(void* arg) {
    DimmerManager* self = static_cast<DimmerManager*>(arg);
    digitalWrite(self->channels[0].pin, HIGH);
    esp_rom_delay_us(TRIAC_PULSE_WIDTH_US);
    digitalWrite(self->channels[0].pin, LOW);
}

void IRAM_ATTR DimmerManager::onTriac2Timer(void* arg) {
    DimmerManager* self = static_cast<DimmerManager*>(arg);
    digitalWrite(self->channels[1].pin, HIGH);
    esp_rom_delay_us(TRIAC_PULSE_WIDTH_US);
    digitalWrite(self->channels[1].pin, LOW);
}

void DimmerManager::calculateDelay(uint8_t channelIndex) {
    if (channelIndex > 1) return;

    uint8_t level = channels[channelIndex].currentLevel;
    if (level == 0) {
        channels[channelIndex].firingDelayUs = AC_HALF_CYCLE_US;
    } else if (level >= 100) {
        channels[channelIndex].firingDelayUs = MIN_FIRING_DELAY_US;
    } else {
        // Inverted mapping: 100% -> MIN_FIRING_DELAY_US (fire early), 1% -> MAX_FIRING_DELAY_US (fire late)
        channels[channelIndex].firingDelayUs = map(
            100 - level, 
            0, 100, 
            MIN_FIRING_DELAY_US, 
            MAX_FIRING_DELAY_US
        );
    }
}

void DimmerManager::setLevel(uint8_t channel, uint8_t level, bool updateState) {
    if (channel < 1 || channel > 2) return;
    uint8_t idx = channel - 1;

    level = constrain(level, 0, 100);
    channels[idx].targetLevel = level;
    channels[idx].currentLevel = level; // Instant update or smooth step
    calculateDelay(idx);

    if (updateState) {
        if (level > 0 && !channels[idx].isEnabled) {
            channels[idx].isEnabled = true;
        } else if (level == 0 && channels[idx].isEnabled) {
            channels[idx].isEnabled = false;
        }
    }

    // Interlock dedicated Fan Relay (GPB0 for Fan 1, GPB1 for Fan 2)
    if (channels[idx].isEnabled && level > 0) {
        RelayManager::getInstance().setFanRelay(channel, true);
    } else {
        RelayManager::getInstance().setFanRelay(channel, false);
    }
}

uint8_t DimmerManager::getLevel(uint8_t channel) const {
    if (channel < 1 || channel > 2) return 0;
    return channels[channel - 1].targetLevel;
}

void DimmerManager::setState(uint8_t channel, bool state) {
    if (channel < 1 || channel > 2) return;
    uint8_t idx = channel - 1;
    channels[idx].isEnabled = state;

    if (!state) {
        digitalWrite(channels[idx].pin, LOW);
        RelayManager::getInstance().setFanRelay(channel, false);
    } else {
        if (channels[idx].targetLevel == 0) {
            channels[idx].targetLevel = 50;
            channels[idx].currentLevel = 50;
            calculateDelay(idx);
        }
        RelayManager::getInstance().setFanRelay(channel, true);
    }
}

bool DimmerManager::getState(uint8_t channel) const {
    if (channel < 1 || channel > 2) return false;
    return channels[channel - 1].isEnabled;
}

void DimmerManager::toggle(uint8_t channel) {
    if (channel < 1 || channel > 2) return;
    setState(channel, !getState(channel));
}

void DimmerManager::update() {
    static uint32_t lastFadeTick = 0;
    uint32_t now = millis();

    // Smooth ramp rate (every 15ms adjust currentLevel closer to targetLevel if desired)
    if (now - lastFadeTick >= 15) {
        lastFadeTick = now;
        for (int i = 0; i < 2; i++) {
            if (channels[i].currentLevel != channels[i].targetLevel) {
                if (channels[i].currentLevel < channels[i].targetLevel) {
                    channels[i].currentLevel = channels[i].currentLevel + 1;
                } else {
                    channels[i].currentLevel = channels[i].currentLevel - 1;
                }
                calculateDelay(i);
            }
        }
    }
}

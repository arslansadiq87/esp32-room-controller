#include "RelayManager.h"
#include "DimmerManager.h"
#include "AutomationSafetyManager.h"

RelayManager& RelayManager::getInstance() {
    static RelayManager instance;
    return instance;
}

RelayManager::RelayManager() {
    for (int i = 0; i < NUM_SWITCHES; i++) {
        lastDebounceTime[i] = 0;
    }
}

bool RelayManager::begin(TwoWire& wire, uint8_t address) {
    i2cWire = &wire;
    i2cAddress = address;

    // Check communication with MCP23017
    i2cWire->beginTransmission(i2cAddress);
    if (i2cWire->endTransmission() != 0) {
        Serial.printf("[RELAY] MCP23017 not found at I2C address 0x%02X!\n", i2cAddress);
        isConnected = false;
        return false;
    }

    // Configure Port A (GPA0..GPA7) as INPUTS with Pull-ups for Wall Switches
    writeRegister(REG_IODIRA, 0xFF); // 1 = Input
    writeRegister(REG_GPPUA, 0xFF); // 1 = 100k Pull-up enabled

    // Configure Port B (GPB0..GPB7) as OUTPUTS for Relays (ULN2803A)
    // GPB0 -> Fan 1 Relay (Pin 18)
    // GPB1 -> Fan 2 Relay (Pin 17)
    // GPB2..GPB7 -> Relays 1..6 (Pins 16..11)
    writeRegister(REG_IODIRB, 0x00); // 0 = Output
    writeRegister(REG_OLATB, relayStatesMask); // Initial state (all OFF)

    // Read initial switch states
    switchStableState = readRegister(REG_GPIOA);
    switchPreviousReading = switchStableState;

    isConnected = true;
    Serial.println("[RELAY] MCP23017 initialized (GPA0..7: Switches, GPB0/1: Fan 1/2 Relays [Pins 18/17], GPB2..7: 6 Relays [Pins 16..11])");
    return true;
}

bool RelayManager::writeRegister(uint8_t reg, uint8_t value) {
    if (!i2cWire) return false;
    i2cWire->beginTransmission(i2cAddress);
    i2cWire->write(reg);
    i2cWire->write(value);
    return (i2cWire->endTransmission() == 0);
}

uint8_t RelayManager::readRegister(uint8_t reg) {
    if (!i2cWire) return 0xFF;
    i2cWire->beginTransmission(i2cAddress);
    i2cWire->write(reg);
    if (i2cWire->endTransmission() != 0) {
        return 0xFF;
    }

    i2cWire->requestFrom(i2cAddress, (uint8_t)1);
    if (i2cWire->available()) {
        return i2cWire->read();
    }
    return 0xFF;
}

void RelayManager::setRelay(uint8_t relayId, bool state, bool notify) {
    if (relayId < 1 || relayId > NUM_RELAYS) return;
    uint8_t bit = (relayId - 1) + RELAY_BIT_OFFSET; // Maps relay 1..6 to GPB2..GPB7

    if (state) {
        relayStatesMask |= (1 << bit);
    } else {
        relayStatesMask &= ~(1 << bit);
    }

    writeRegister(REG_OLATB, relayStatesMask);

    if (notify && changeCallback) {
        changeCallback(relayId, state);
    }
}

bool RelayManager::getRelay(uint8_t relayId) const {
    if (relayId < 1 || relayId > NUM_RELAYS) return false;
    uint8_t bit = (relayId - 1) + RELAY_BIT_OFFSET;
    return (relayStatesMask & (1 << bit)) != 0;
}

void RelayManager::toggleRelay(uint8_t relayId, bool notify) {
    if (relayId < 1 || relayId > NUM_RELAYS) return;
    setRelay(relayId, !getRelay(relayId), notify);
}

void RelayManager::setFanRelay(uint8_t fanId, bool state, bool notify) {
    if (fanId < 1 || fanId > NUM_FAN_RELAYS) return;
    uint8_t bit = (fanId == 1) ? FAN1_RELAY_BIT : FAN2_RELAY_BIT; // GPB0 (Pin 18) or GPB1 (Pin 17)

    if (state) {
        relayStatesMask |= (1 << bit);
    } else {
        relayStatesMask &= ~(1 << bit);
    }

    writeRegister(REG_OLATB, relayStatesMask);

    if (notify && fanChangeCallback) {
        fanChangeCallback(fanId, state);
    }
}

bool RelayManager::getFanRelay(uint8_t fanId) const {
    if (fanId < 1 || fanId > NUM_FAN_RELAYS) return false;
    uint8_t bit = (fanId == 1) ? FAN1_RELAY_BIT : FAN2_RELAY_BIT;
    return (relayStatesMask & (1 << bit)) != 0;
}

void RelayManager::toggleFanRelay(uint8_t fanId, bool notify) {
    if (fanId < 1 || fanId > NUM_FAN_RELAYS) return;
    setFanRelay(fanId, !getFanRelay(fanId), notify);
}

uint8_t RelayManager::getRelaysMask() const {
    return (relayStatesMask >> RELAY_BIT_OFFSET) & 0x3F;
}

void RelayManager::setAllRelays(uint8_t mask6bit, bool notify) {
    // Preserve Fan 1 & Fan 2 bits (bits 0 & 1) and apply new 6-bit mask to bits 2..7
    relayStatesMask = (relayStatesMask & 0x03) | ((mask6bit & 0x3F) << RELAY_BIT_OFFSET);
    writeRegister(REG_OLATB, relayStatesMask);

    if (notify && changeCallback) {
        for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
            changeCallback(i, getRelay(i));
        }
    }
}

bool RelayManager::getSwitchRaw(uint8_t switchId) const {
    if (switchId < 1 || switchId > NUM_SWITCHES) return false;
    return (switchStableState & (1 << (switchId - 1))) == 0; // Active low (pulled to GND)
}

void RelayManager::onRelayChange(RelayChangeCallback callback) {
    changeCallback = callback;
}

void RelayManager::onFanRelayChange(FanRelayChangeCallback callback) {
    fanChangeCallback = callback;
}

void RelayManager::update() {
    if (!isConnected) return;

    static uint32_t lastPollTime = 0;
    uint32_t now = millis();

    // Poll MCP23017 GPA port every 15ms
    if (now - lastPollTime < 15) {
        return;
    }
    lastPollTime = now;

    uint8_t currentReading = readRegister(REG_GPIOA);

    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
        uint8_t mask = (1 << i);
        bool currentBit = (currentReading & mask);
        bool previousBit = (switchPreviousReading & mask);

        // If the reading changed, reset the debounce timer for this switch
        if (currentBit != previousBit) {
            lastDebounceTime[i] = now;
        }

        // If reading has remained stable longer than debounce threshold
        if ((now - lastDebounceTime[i]) > SWITCH_DEBOUNCE_MS) {
            bool stableBit = (switchStableState & mask);
            if (currentBit != stableBit) {
                // Switch transition confirmed!
                if (currentBit) {
                    switchStableState |= mask;
                } else {
                    switchStableState &= ~mask;
                }

                // Handle switch action depending on mapping:
                if (i == 0) {
                    // GPA0 -> Wall Switch for Fan 1 (Dimmer 1)
                    DimmerManager::getInstance().toggle(1);
                    AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, 1);
                    Serial.printf("[SWITCH] Wall Switch 1 changed state -> Toggled Fan 1 (Dimmer 1)\n");
                } else if (i == 1) {
                    // GPA1 -> Wall Switch for Fan 2 (Dimmer 2)
                    DimmerManager::getInstance().toggle(2);
                    AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::FAN, 2);
                    Serial.printf("[SWITCH] Wall Switch 2 changed state -> Toggled Fan 2 (Dimmer 2)\n");
                } else {
                    // GPA2..GPA7 -> Wall Switches for General Relays 1..6
                    uint8_t generalRelayId = (i - 2) + 1; // 1 to 6
                    toggleRelay(generalRelayId, true);
                    AutomationSafetyManager::getInstance().notifyManualIntervention(SafetyDeviceType::RELAY, generalRelayId);
                    Serial.printf("[SWITCH] Wall Switch %d changed state -> Relay %d toggled to %s\n", 
                                  i + 1, generalRelayId, getRelay(generalRelayId) ? "ON" : "OFF");
                }
            }
        }
    }

    switchPreviousReading = currentReading;
}

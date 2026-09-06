#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <functional>
#include "Config.h"

// Callback type for relay state changes: (relayId: 1..6, newState: bool)
using RelayChangeCallback = std::function<void(uint8_t, bool)>;
// Callback type for fan relay state changes: (fanId: 1..2, newState: bool)
using FanRelayChangeCallback = std::function<void(uint8_t, bool)>;

class RelayManager {
public:
    static RelayManager& getInstance();

    // Initialize MCP23017 I2C communication and pin modes
    bool begin(TwoWire& wire = Wire, uint8_t address = MCP23017_I2C_ADDR);

    // Switchable General Relays Control (relayId: 1 to 6 -> GPB2..GPB7 / ULN2803A Pins 16..11)
    void setRelay(uint8_t relayId, bool state, bool notify = true);
    bool getRelay(uint8_t relayId) const;
    void toggleRelay(uint8_t relayId, bool notify = true);

    // Dedicated Fan Relays Control (fanId: 1 to 2 -> GPB0, GPB1 / ULN2803A Pins 18, 17)
    void setFanRelay(uint8_t fanId, bool state, bool notify = true);
    bool getFanRelay(uint8_t fanId) const;
    void toggleFanRelay(uint8_t fanId, bool notify = true);

    // Bulk state
    uint8_t getRelaysMask() const; // 6-bit mask for Relays 1..6
    uint8_t getRawPortBMask() const { return relayStatesMask; } // Full 8-bit GPB output state
    void setAllRelays(uint8_t mask6bit, bool notify = true);

    // Wall Switch reading (switchId: 1 to 8 -> GPA0..GPA7)
    bool getSwitchRaw(uint8_t switchId) const;

    // Periodic non-blocking polling and debounce
    void update();

    // Event listener registration
    void onRelayChange(RelayChangeCallback callback);
    void onFanRelayChange(FanRelayChangeCallback callback);

private:
    RelayManager();
    ~RelayManager() = default;

    RelayManager(const RelayManager&) = delete;
    RelayManager& operator=(const RelayManager&) = delete;

    // MCP23017 low-level register write/read
    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);

    TwoWire* i2cWire = nullptr;
    uint8_t i2cAddress = MCP23017_I2C_ADDR;
    bool isConnected = false;

    // MCP23017 Register Addresses (IOCON.BANK = 0)
    static constexpr uint8_t REG_IODIRA   = 0x00; // Direction Port A
    static constexpr uint8_t REG_IODIRB   = 0x01; // Direction Port B
    static constexpr uint8_t REG_GPPUA    = 0x0C; // Pull-up Port A
    static constexpr uint8_t REG_GPPUB    = 0x0D; // Pull-up Port B
    static constexpr uint8_t REG_GPIOA    = 0x12; // Input values Port A
    static constexpr uint8_t REG_GPIOB    = 0x13; // Input values Port B
    static constexpr uint8_t REG_OLATA    = 0x14; // Output Latches Port A
    static constexpr uint8_t REG_OLATB    = 0x15; // Output Latches Port B

    // State masks
    uint8_t relayStatesMask = 0x00;        // Current output states for GPB0..GPB7
    uint8_t switchStableState = 0xFF;      // Debounced stable states of GPA0..GPA7
    uint8_t switchPreviousReading = 0xFF;  // Last raw reading
    uint32_t lastDebounceTime[NUM_SWITCHES] = {0};

    RelayChangeCallback changeCallback = nullptr;
    FanRelayChangeCallback fanChangeCallback = nullptr;
};

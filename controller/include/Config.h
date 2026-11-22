#pragma once

#include <Arduino.h>

// ==============================================================================
// Hardware GPIO Pin Definitions (ESP32-C6-WROOM-1)
// ==============================================================================

// I2C Bus Pins (Shared between MCP23017, ACS37800 & STCC4)
constexpr uint8_t PIN_I2C_SDA = 6;
constexpr uint8_t PIN_I2C_SCL = 7;
constexpr uint32_t I2C_FREQUENCY = 400000; // 400 kHz Fast Mode

// External SPI Flash Pins (W25Q128)
constexpr uint8_t PIN_SPI_CS = 19;
constexpr uint8_t PIN_SPI_CLK = 20;
constexpr uint8_t PIN_SPI_MOSI = 21; // GPIO21_DI
constexpr uint8_t PIN_SPI_MISO = 22; // GPIO22_DO

// AC Phase-Cut Dimming / Fan Speed Pins
constexpr uint8_t PIN_ZC_DETECT = 5;  // Zero-Cross Detection Interrupt Pin (GPIO5_ZC)
constexpr uint8_t PIN_TRIAC_1   = 4;  // Triac 1 Gate Trigger (GPIO4_TRIAC1)
constexpr uint8_t PIN_TRIAC_2   = 18; // Triac 2 Gate Trigger (GPIO18_TRIAC2)

// Rotary Encoder 1 Pins (Controls Fan 1 / Dimmer 1)
constexpr uint8_t PIN_ENC1_A  = 0;  // GPIO0_ENC_A
constexpr uint8_t PIN_ENC1_B  = 1;  // GPIO1_ENC_B
constexpr uint8_t PIN_ENC1_SW = 23; // GPIO23_ENC_SW

// Rotary Encoder 2 Pins (Controls Fan 2 / Dimmer 2)
constexpr uint8_t PIN_ENC2_A  = 2;  // GPIO2_ENC_A
constexpr uint8_t PIN_ENC2_B  = 3;  // GPIO3_ENC_B
constexpr uint8_t PIN_ENC2_SW = 10; // GPIO10_ENC_SW

// RGB Status Indicator Pin (WS2812B)
constexpr uint8_t PIN_STATUS_LED = 11; // GPIO11_FLED
constexpr uint8_t NUM_LEDS = 1;

// Fermion C4002 mmWave Radar Sensor (UART0)
constexpr uint8_t PIN_C4002_TX = 16; // GPIO16_TXD0 (ESP32 TX -> C4002 RX)
constexpr uint8_t PIN_C4002_RX = 17; // GPIO17_RXD0 (ESP32 RX <- C4002 TX)
constexpr uint32_t C4002_BAUDRATE = 115200;
constexpr uint32_t MMWAVE_POLL_INTERVAL_MS = 100; // Poll mmWave packet state every 100ms

// ==============================================================================
// Peripheral & Communication Addresses
// ==============================================================================
constexpr uint8_t MCP23017_I2C_ADDR = 0x20; // Default address (A0=A1=A2=GND)
constexpr uint8_t ACS37800_I2C_ADDR = 0x60; // Default address for ACS37800
constexpr uint8_t STCC4_I2C_ADDR    = 0x64; // Default address for Sensirion STCC4 CO2 Sensor
constexpr uint32_t CO2_POLL_INTERVAL_MS = 1000; // Poll STCC4 every 1s

// ==============================================================================
// AC Mains & Dimming Parameters
// ==============================================================================
// 50Hz AC Half-Cycle: 10,000 us (10.0 ms)
// 60Hz AC Half-Cycle:  8,333 us ( 8.3 ms)
constexpr uint32_t AC_MAINS_FREQ_HZ = 50;
constexpr uint32_t AC_HALF_CYCLE_US = (1000000 / AC_MAINS_FREQ_HZ) / 2;

// Gate pulse duration in microseconds (ensure opto-triac reliably latches)
constexpr uint16_t TRIAC_PULSE_WIDTH_US = 25;

// Firing limits (dead-bands near 0% and 100% to avoid flickering / inductive
// lockup)
constexpr uint16_t MIN_FIRING_DELAY_US = 500; // Max brightness (~95%)
constexpr uint16_t MAX_FIRING_DELAY_US =
    AC_HALF_CYCLE_US - 800; // Min brightness (~5%)

// ==============================================================================
// Relay & Fan Relay Configuration (MCP23017 Port B -> ULN2803A)
// ==============================================================================
// ULN2803A Darlington Driver Pinout:
// Pin 18 (OUT1 / GPB0) -> Fan 1 Relay (Mains isolation for Fan 1 / Triac 1)
// Pin 17 (OUT2 / GPB1) -> Fan 2 Relay (Mains isolation for Fan 2 / Triac 2)
// Pins 16..11 (OUT3..OUT8 / GPB2..GPB7) -> 6 Switchable Relays (Relay 1 to 6)
constexpr uint8_t NUM_RELAYS = 6;       // 6 General Switchable Relays
constexpr uint8_t NUM_FAN_RELAYS = 2;   // 2 Dedicated Fan Relays
constexpr uint8_t FAN1_RELAY_BIT = 0;   // GPB0 -> ULN2803A Pin 18
constexpr uint8_t FAN2_RELAY_BIT = 1;   // GPB1 -> ULN2803A Pin 17
constexpr uint8_t RELAY_BIT_OFFSET = 2; // Relays 1..6 mapped to GPB2..GPB7

// Wall Switches (MCP23017 Port A: GPA0=Fan1, GPA1=Fan2, GPA2..GPA7=Relays 1..6)
constexpr uint8_t NUM_SWITCHES = 8;
constexpr uint32_t SWITCH_DEBOUNCE_MS = 50; // Switch debounce duration

// ==============================================================================
// Network & WiFi Configuration
// ==============================================================================
#define DEFAULT_AP_SSID "ESP32-RoomController"
#define DEFAULT_AP_PASS "techpanda123"
#define DEFAULT_STA_SSID "YourWiFiSSID"
#define DEFAULT_STA_PASS "YourWiFiPassword"
#define DEFAULT_MDNS_HOSTNAME "Room-Controller"

constexpr uint16_t HTTP_SERVER_PORT = 80;
constexpr uint32_t TELEMETRY_INTERVAL_MS =
    500; // WebSocket telemetry push period

// ==============================================================================
// NVS Storage & Automation Defaults
// ==============================================================================
#define NVS_NAMESPACE_WIFI "rc_wifi"
#define NVS_NAMESPACE_AUTO "rc_auto"
#define NVS_NAMESPACE_POWER "rc_pwr"
#define NVS_NAMESPACE_MQTT "rc_mqtt"

#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_DISCOVERY_PREFIX "homeassistant"
#define DEFAULT_MQTT_NODE_ID "room_controller"

constexpr uint32_t DEFAULT_AUTO_OFF_DELAY_SEC = 120; // 2 minutes auto-off timeout
constexpr bool DEFAULT_AUTO_ON_PRESENCE = true;       // Automatically turn on loads on presence

// ==============================================================================
// Power Logging & Energy Analytics (Winbond W25Q128 LittleFS)
// ==============================================================================
#define POWER_LOG_BIN_FILE "/power_log.bin"
#define POWER_CONFIG_FILE "/power_cfg.json"

constexpr uint32_t POWER_LOG_INTERVAL_SEC = 300;     // 5-minute sampling interval
constexpr uint32_t POWER_LOG_24H_SAMPLES  = 288;     // 24 hours @ 5 min/sample
constexpr uint32_t POWER_LOG_MAX_ENTRIES  = 2016;    // 7 days rolling circular ring buffer

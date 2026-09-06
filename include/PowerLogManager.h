#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"

#pragma pack(push, 1)
struct PowerLogEntry {
    uint32_t timestamp;  // Epoch or relative seconds
    float activePower;   // Active Power (Watts)
    float voltage;       // Voltage RMS (Volts)
    float energyWh;      // Watt-hours accumulated in this sample interval
};

struct PowerLogHeader {
    uint32_t magic;          // 0x5057524C ("PWRL")
    uint32_t maxEntries;     // Maximum entries capacity in ring buffer
    uint32_t headIndex;      // Index of the next slot to write (0 to maxEntries - 1)
    uint32_t count;          // Total valid entries stored (capped at maxEntries)
    float dailyKwh;          // Energy accumulated today
    float monthlyKwh;        // Energy accumulated this calendar month
    uint32_t lastDaySec;     // Timestamp of last daily rollover
    uint32_t lastMonthDay;   // Day of last monthly rollover
};
#pragma pack(pop)

class PowerLogManager {
public:
    static PowerLogManager& getInstance();

    // Initialize storage, load configuration & buffer header from Winbond flash
    bool begin();

    // Periodic energy integration and interval disk commit
    void update();

    // Logging control
    void setLoggingEnabled(bool enable);
    bool isLoggingEnabled() const { return loggingEnabled; }

    // Metrics getters
    float getDailyKwh() const { return header.dailyKwh; }
    float getMonthlyKwh() const { return header.monthlyKwh; }
    float getPeakPower24h() const { return peakPower24h; }
    float getAvgPower24h() const { return avgPower24h; }

    // History JSON for dashboard 24h chart
    String getHistoryJson() const;

    // Reset / clear all logs
    void clearLogs();

private:
    PowerLogManager();
    ~PowerLogManager() = default;

    PowerLogManager(const PowerLogManager&) = delete;
    PowerLogManager& operator=(const PowerLogManager&) = delete;

    void loadConfig();
    void saveConfig();
    void loadHeader();
    void saveHeader();
    void recordSample();
    uint32_t getCurrentTimestamp() const;
    void recalculate24hStats();

    bool loggingEnabled = false;
    PowerLogHeader header;

    uint32_t lastSampleTime = 0;
    uint32_t lastIntegrationTime = 0;
    double currentIntervalWattSeconds = 0.0;
    float peakPower24h = 0.0f;
    float avgPower24h = 0.0f;

    static constexpr uint32_t HEADER_MAGIC = 0x5057524C; // "PWRL"
};

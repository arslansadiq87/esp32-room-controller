#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include "Config.h"

class StorageManager {
public:
    static StorageManager& getInstance();

    // Initialize SPI and LittleFS filesystem
    bool begin();

    // Filesystem information
    bool isMounted() const { return mounted; }
    size_t getTotalBytes() const;
    size_t getUsedBytes() const;
    bool fileExists(const String& path) const;

    // Helper file operations
    String readFile(const String& path);
    bool writeFile(const String& path, const String& content);

private:
    StorageManager() = default;
    ~StorageManager() = default;

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    bool mounted = false;
};

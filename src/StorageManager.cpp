#include "StorageManager.h"

StorageManager& StorageManager::getInstance() {
    static StorageManager instance;
    return instance;
}

bool StorageManager::begin() {
    // Configure SPI Pins for external Flash (W25Q128)
    SPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

    // Initialize LittleFS
    if (!LittleFS.begin(true)) { // Format if mount fails
        Serial.println("[STORAGE] LittleFS Mount Failed!");
        mounted = false;
        return false;
    }

    mounted = true;
    Serial.printf("[STORAGE] LittleFS mounted successfully. Total: %u KB, Used: %u KB\n",
                  (uint32_t)(LittleFS.totalBytes() / 1024),
                  (uint32_t)(LittleFS.usedBytes() / 1024));
    return true;
}

size_t StorageManager::getTotalBytes() const {
    return mounted ? LittleFS.totalBytes() : 0;
}

size_t StorageManager::getUsedBytes() const {
    return mounted ? LittleFS.usedBytes() : 0;
}

bool StorageManager::fileExists(const String& path) const {
    if (!mounted) return false;
    return LittleFS.exists(path);
}

String StorageManager::readFile(const String& path) {
    if (!mounted || !LittleFS.exists(path)) {
        return String();
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return String();
    }

    String content = file.readString();
    file.close();
    return content;
}

bool StorageManager::writeFile(const String& path, const String& content) {
    if (!mounted) return false;

    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }

    size_t written = file.print(content);
    file.close();
    return (written == content.length());
}

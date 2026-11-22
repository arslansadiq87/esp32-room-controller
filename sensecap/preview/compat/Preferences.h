#pragma once
#include <cstdint>
class Preferences {
public:
    bool begin(const char*,bool) { return false; }
    uint8_t getUChar(const char*,uint8_t value) { return value; }
    uint16_t getUShort(const char*,uint16_t value) { return value; }
    bool getBool(const char*,bool value) { return value; }
    void putUChar(const char*,uint8_t) {}
    void putUShort(const char*,uint16_t) {}
    void putBool(const char*,bool) {}
};

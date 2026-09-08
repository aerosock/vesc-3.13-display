#pragma once

#include <stdint.h>
#include <string.h>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) { return true; }
    void end() {}
    bool clear() { return true; }
    bool isKey(const char* key) { return false; }

    bool putBool(const char* key, bool val) { return true; }
    bool getBool(const char* key, bool default_val = false) { return default_val; }

    size_t putUChar(const char* key, uint8_t val) { return 1; }
    uint8_t getUChar(const char* key, uint8_t default_val = 0) { return default_val; }

    size_t putUShort(const char* key, uint16_t val) { return 2; }
    uint16_t getUShort(const char* key, uint16_t default_val = 0) { return default_val; }

    size_t putFloat(const char* key, float val) { return 4; }
    float getFloat(const char* key, float default_val = 0.0f) { return default_val; }

    size_t putUInt(const char* key, uint32_t val) { return 4; }
    uint32_t getUInt(const char* key, uint32_t default_val = 0) { return default_val; }
};

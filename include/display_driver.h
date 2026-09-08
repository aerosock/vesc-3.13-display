#pragma once

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>
#ifdef NATIVE_SIMULATOR
#include "../tools/simulator/include/display_driver.h"
#else
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include "board_config.h"

// Custom Panel class with Waveshare 3.16" ST7701 initialization sequences
class WaveshareST7701Panel : public lgfx::Panel_ST7701_Base {
protected:
    const uint8_t *getInitCommands(uint8_t listno) const override;
};

// Complete LGFX Device configuration for Waveshare ESP32-S3-LCD-3.16
class WaveshareLGFX : public lgfx::LGFX_Device {
    WaveshareST7701Panel _panel_instance;
    lgfx::Bus_RGB        _bus_instance;

public:
    WaveshareLGFX();
};

class DisplayDriver {
public:
    DisplayDriver();
    bool init();
    void setBrightness(uint8_t brightness); // 0 (off) to 255 (max)
    void clear(uint16_t color = 0x0000);
    
    WaveshareLGFX& getLgfx() { return _lcd; }
    LovyanGFX* getCanvas() { return &_lcd; }
    void pushCanvas() { _lcd.display(); }

private:
    WaveshareLGFX _lcd;
    uint8_t       _brightness;
};

extern DisplayDriver Display;
#endif

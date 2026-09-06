#include "display_driver.h"
#include <driver/ledc.h>

DisplayDriver Display;

// ST7701 initialization command list for Waveshare 3.16" 320x820 panel
const uint8_t *WaveshareST7701Panel::getInitCommands(uint8_t listno) const {
    static constexpr const uint8_t list0[] = {
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xEF, 1, 0x08,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
        0xC0, 2, 0xE5, 0x02,
        0xC1, 2, 0x15, 0x0A,
        0xC2, 2, 0x07, 0x02,
        0xCC, 1, 0x10,
        0xB0, 16, 0x00, 0x08, 0x51, 0x0D, 0xCE, 0x06, 0x00, 0x08,
                  0x08, 0x24, 0x05, 0xD0, 0x0F, 0x6F, 0x36, 0x1F,
        0xB1, 16, 0x00, 0x10, 0x4F, 0x0C, 0x11, 0x05, 0x00, 0x07,
                  0x07, 0x18, 0x02, 0xD3, 0x11, 0x6E, 0x34, 0x1F,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
        0xB0, 1, 0x4D,
        0xB1, 1, 0x37,
        0xB2, 1, 0x87,
        0xB3, 1, 0x80,
        0xB5, 1, 0x4A,
        0xB7, 1, 0x85,
        0xB8, 1, 0x21,
        0xB9, 2, 0x00, 0x13,
        0xC0, 1, 0x09,
        0xC1, 1, 0x78,
        0xC2, 1, 0x78,
        0xD0, 1, 0x88,
        0xE0, 3 + CMD_INIT_DELAY, 0x80, 0x00, 0x02, 100,
        0xE1, 11, 0x0F, 0xA0, 0x00, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x60, 0x60,
        0xE2, 13, 0x30, 0x30, 0x60, 0x60, 0x45, 0xA0, 0x00, 0x00, 0x46, 0xA0, 0x00, 0x00, 0x00,
        0xE3, 4, 0x00, 0x00, 0x33, 0x33,
        0xE4, 2, 0x44, 0x44,
        0xE5, 16, 0x0F, 0x4A, 0xA0, 0xA0, 0x11, 0x4A, 0xA0, 0xA0,
                  0x13, 0x4A, 0xA0, 0xA0, 0x15, 0x4A, 0xA0, 0xA0,
        0xE6, 4, 0x00, 0x00, 0x33, 0x33,
        0xE7, 2, 0x44, 0x44,
        0xE8, 16, 0x10, 0x4A, 0xA0, 0xA0, 0x12, 0x4A, 0xA0, 0xA0,
                  0x14, 0x4A, 0xA0, 0xA0, 0x16, 0x4A, 0xA0, 0xA0,
        0xEB, 7, 0x02, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x00,
        0xED, 16, 0xFF, 0xFF, 0x04, 0x56, 0x72, 0xFF, 0xFF, 0xFF,
                  0xFF, 0xFF, 0xFF, 0x27, 0x65, 0x40, 0xFF, 0xFF,
        0xEF, 6, 0x08, 0x08, 0x08, 0x40, 0x3F, 0x64,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xE8, 2, 0x00, 0x0E,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
        0x11, 0 + CMD_INIT_DELAY, 120,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xE8, 2 + CMD_INIT_DELAY, 0x00, 0x0C, 10,
        0xE8, 2, 0x00, 0x00,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
        0x3A, 1, 0x55,
        0x36, 1, 0x00,
        0x35, 1, 0x00,
        0x29, 0 + CMD_INIT_DELAY, 20,
        0xFF, 0xFF
    };
    switch (listno) {
        case 0: return list0;
        default: return nullptr;
    }
}

WaveshareLGFX::WaveshareLGFX() {
    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs        = LCD_PIN_CS;
        cfg.pin_rst       = LCD_PIN_RST;
        cfg.memory_width  = 320;
        cfg.memory_height = 820;
        cfg.panel_width   = 320;
        cfg.panel_height  = 820;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.rgb_order     = true;
        cfg.invert        = false;

        _panel_instance.config(cfg);
    }

    {
        auto cfg = _panel_instance.config_detail();
        cfg.pin_cs    = LCD_PIN_CS;
        cfg.pin_sclk  = LCD_PIN_SCK;
        cfg.pin_mosi  = LCD_PIN_MOSI;
        cfg.use_psram = 1; // 524KB frame buffer allocated in Octal PSRAM
        _panel_instance.config_detail(cfg);
    }

    {
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;

        // RGB Bus Pins (16-bit 565)
        cfg.pin_d0  = LCD_PIN_B0;
        cfg.pin_d1  = LCD_PIN_B1;
        cfg.pin_d2  = LCD_PIN_B2;
        cfg.pin_d3  = LCD_PIN_B3;
        cfg.pin_d4  = LCD_PIN_B4;
        cfg.pin_d5  = LCD_PIN_G0;
        cfg.pin_d6  = LCD_PIN_G1;
        cfg.pin_d7  = LCD_PIN_G2;
        cfg.pin_d8  = LCD_PIN_G3;
        cfg.pin_d9  = LCD_PIN_G4;
        cfg.pin_d10 = LCD_PIN_G5;
        cfg.pin_d11 = LCD_PIN_R0;
        cfg.pin_d12 = LCD_PIN_R1;
        cfg.pin_d13 = LCD_PIN_R2;
        cfg.pin_d14 = LCD_PIN_R3;
        cfg.pin_d15 = LCD_PIN_R4;

        // Timing & Sync Pins
        cfg.pin_henable = LCD_PIN_DE;
        cfg.pin_vsync   = LCD_PIN_VSYNC;
        cfg.pin_hsync   = LCD_PIN_HSYNC;
        cfg.pin_pclk    = LCD_PIN_PCLK;
        cfg.freq_write  = LCD_FREQ_HZ;

        cfg.hsync_polarity    = 0;
        cfg.hsync_front_porch = 30;
        cfg.hsync_pulse_width = 6;
        cfg.hsync_back_porch  = 30;

        cfg.vsync_polarity    = 0;
        cfg.vsync_front_porch = 20;
        cfg.vsync_pulse_width = 40;
        cfg.vsync_back_porch  = 20;

        cfg.pclk_idle_high    = 1;

        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }

    setPanel(&_panel_instance);
}

DisplayDriver::DisplayDriver() : _brightness(255) {}

bool DisplayDriver::init() {
    // 1. Initialize Inverted Backlight PWM first (GPIO 6, 50kHz, Channel 1)
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)BL_PWM_RES,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = BL_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)BL_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_3,
        // Inverted: 255 brightness is 0 duty (active-low pulls to GND)
        .duty = (uint32_t)(255 - _brightness),
        .hpoint = 0,
    };
    ledc_channel_config(&channel_conf);
    setBrightness(_brightness);
    delay(20);

    // 2. Initialize ST7701 panel & RGB bus via LovyanGFX
    if (!_lcd.init()) {
        Serial.println("[DISPLAY ERROR] _lcd.init() returned false!");
        return false;
    }

    _lcd.setColorDepth(16);
    _lcd.setRotation(1); // Landscape mode: 820 x 320
    _lcd.fillScreen(0x0000);

    Serial.printf("[DISPLAY OK] Resolution: %d x %d, Rotation: %d\n", _lcd.width(), _lcd.height(), _lcd.getRotation());
    Serial.printf("[DISPLAY OK] Free Heap: %u bytes, Free PSRAM: %u bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());
    return true;
}

void DisplayDriver::setBrightness(uint8_t brightness) {
    _brightness = brightness;
    uint32_t duty = (uint32_t)(255 - brightness);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)BL_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)BL_PWM_CHANNEL);
}

void DisplayDriver::clear(uint16_t color) {
    _lcd.fillScreen(color);
}

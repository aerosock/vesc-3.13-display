#pragma once

#define LGFX_USE_V1
#define LGFX_LINUX_FB
#include <LovyanGFX.hpp>
#include "board_config.h"

class DisplayDriver {
public:
    DisplayDriver() {
        _sprite.setColorDepth(16);
        _sprite.createSprite(LCD_WIDTH, LCD_HEIGHT);
    }

    bool init() {
        clear(0x0000);
        return true;
    }

    void setBrightness(uint8_t brightness) {}
    
    void clear(uint16_t color = 0x0000) {
        _sprite.fillScreen(color);
    }
    
    LovyanGFX* getCanvas() { return &_sprite; }
    lgfx::LGFX_Sprite& getSprite() { return _sprite; }
    void pushCanvas() {}

    void dumpBMP(const char *filename) {
        FILE *f = fopen(filename, "wb");
        if (!f) return;

        int w = LCD_WIDTH;
        int h = LCD_HEIGHT;
        uint32_t row_size = ((w * 3 + 3) / 4) * 4;
        uint32_t img_size = row_size * h;
        uint32_t file_size = 54 + img_size;

        uint8_t header[54] = {
            'B', 'M',
            (uint8_t)(file_size), (uint8_t)(file_size >> 8), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
            (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
            1, 0,
            24, 0,
            0, 0, 0, 0,
            (uint8_t)(img_size), (uint8_t)(img_size >> 8), (uint8_t)(img_size >> 16), (uint8_t)(img_size >> 24),
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
        };
        fwrite(header, 1, 54, f);

        uint16_t *buf = (uint16_t*)_sprite.getBuffer();
        uint8_t *row = (uint8_t*)malloc(row_size);
        for (int y = h - 1; y >= 0; y--) {
            for (int x = 0; x < w; x++) {
                uint16_t raw = buf[y * w + x];
                uint16_t c = (raw >> 8) | (raw << 8);
                uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
                uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
                uint8_t b = (c & 0x1F) * 255 / 31;
                row[x * 3 + 0] = b;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = r;
            }
            for (size_t p = w * 3; p < row_size; p++) row[p] = 0;
            fwrite(row, 1, row_size, f);
        }
        free(row);
        fclose(f);
    }

private:
    lgfx::LGFX_Sprite _sprite;
};

extern DisplayDriver Display;

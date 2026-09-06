#include "dash_renderer.h"
#include <math.h>

DashboardRenderer Renderer;

// Down-to-Earth OEM Automotive/Motorcycle Colors (RGB565)
#define COLOR_BG            0x0000  // Deep pure black background for high contrast
#define COLOR_SURFACE       0x10A2  // Dark charcoal panel surface (#12161f)
#define COLOR_SURFACE_LIGHT 0x18E3  // Lighter card surface (#1a2230)
#define COLOR_BORDER        0x31A6  // Crisp slate divider / border
#define COLOR_WHITE         0xFFFF  // Pure high-visibility white
#define COLOR_LIGHT_GRAY    0xCE79  // Light silver text (#d0d4dc)
#define COLOR_MUTED_GRAY    0x8410  // Muted gray for secondary units (#80848e)
#define COLOR_GREEN         0x2E65  // Clean instrument green (#2ecc71)
#define COLOR_AMBER         0xFD20  // Caution / warning amber (#f39c12)
#define COLOR_RED           0xE882  // Redline / alert red (#e74c3c)
#define COLOR_BLUE          0x341F  // Accent blue (#3498db)
#define COLOR_NEEDLE        0xFA00  // Fluorescent orange-red instrument needle

DashboardRenderer::DashboardRenderer()
    : _display(nullptr),
      _canvas(nullptr),
      _active_screen((DashboardScreen)-1),
      _screen_dirty(true),
      _last_mode((RidingMode)-1),
      _is_sweeping(false),
      _sweep_phase(0),
      _sweep_progress(0.0f),
      _sweep_rpm(0.0f),
      _last_sweep_tick_ms(0) {
    _last_needle.valid = false;
    _cache.invalidate();
}

void DashboardRenderer::begin(DisplayDriver *display) {
    _display = display;
    _canvas = _display->getCanvas();
    _screen_dirty = true;
    _last_needle.valid = false;
    triggerR1Sweep();
}

void DashboardRenderer::triggerR1Sweep() {
    _is_sweeping = true;
    _sweep_phase = 0;
    _sweep_progress = 0.0f;
    _sweep_rpm = 0.0f;
    _last_sweep_tick_ms = millis();
}

void DashboardRenderer::drawCard(int x, int y, int w, int h, uint16_t bg, uint16_t border) {
    _canvas->fillRoundRect(x, y, w, h, 6, bg);
    _canvas->drawRoundRect(x, y, w, h, 6, border);
}

void DashboardRenderer::render(const DashTelemetry &telemetry) {
    // 1. Update R1 Sweep Physics if active
    if (_is_sweeping) {
        uint32_t now = millis();
        float dt = (now - _last_sweep_tick_ms) / 1000.0f;
        _last_sweep_tick_ms = now;

        const float max_rpm = 12000.0f;

        if (_sweep_phase == 0) {
            // Fast sweep to redline (0.55s)
            _sweep_progress += dt / 0.55f;
            if (_sweep_progress >= 1.0f) {
                _sweep_progress = 1.0f;
                _sweep_phase = 1;
            }
            _sweep_rpm = _sweep_progress * max_rpm;
        } else if (_sweep_phase == 1) {
            // Pause at redline (0.12s)
            _sweep_progress += dt / 0.12f;
            if (_sweep_progress >= 2.0f) {
                _sweep_phase = 2;
                _sweep_progress = 1.0f;
            }
            _sweep_rpm = max_rpm;
        } else if (_sweep_phase == 2) {
            // Sweep back down to zero (0.55s)
            _sweep_progress -= dt / 0.55f;
            if (_sweep_progress <= 0.0f) {
                _sweep_progress = 0.0f;
                _is_sweeping = false;
            }
            _sweep_rpm = _sweep_progress * max_rpm;
        }
    }

    // 2. Check if screen changed or dirty: draw static layout once
    if (telemetry.screen != _active_screen || _screen_dirty) {
        _active_screen = telemetry.screen;
        _screen_dirty = false;
        _cache.invalidate();
        _last_mode = (RidingMode)-1;
        _last_needle.valid = false;

        _canvas->startWrite();
        _canvas->fillScreen(COLOR_BG);

        switch (_active_screen) {
            case SCREEN_HORIZONTAL_TACH:
                initHorizontalTachScreen(telemetry);
                break;
            case SCREEN_R1_ANALOG_TACH:
                initR1DialScreen(telemetry);
                break;
            case SCREEN_SETTINGS_MENU:
                initSettingsScreen(telemetry);
                break;
            default:
                initHorizontalTachScreen(telemetry);
                break;
        }
        _canvas->endWrite();
    }

    // 3. Render dynamic updates
    _canvas->startWrite();
    switch (_active_screen) {
        case SCREEN_HORIZONTAL_TACH:
            renderHorizontalTachScreen(telemetry);
            break;
        case SCREEN_R1_ANALOG_TACH:
            renderR1DialScreen(telemetry);
            break;
        case SCREEN_SETTINGS_MENU:
            renderSettingsScreen(telemetry);
            break;
        default:
            renderHorizontalTachScreen(telemetry);
            break;
    }
    _canvas->endWrite();
}

// ==============================================================================
// SCREEN 1: CLEAN OEM HORIZONTAL TACHOMETER & SPEEDOMETER
// ==============================================================================
void DashboardRenderer::initHorizontalTachScreen(const DashTelemetry &telemetry) {
    // Top Bar Frame & Scale
    int x = 20, y = 30, w = 780, h = 20;

    // Scale Label between 0 and 2 (plenty of empty horizontal space)
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("x1000 RPM", x + 48, y - 22, &fonts::Font2);

    // Exact Mathematical Ticks & Numbers (0, 2, 4, 6, 8, 10, 12)
    for (int k = 0; k <= 12; k += 2) {
        float fraction = (float)k / 12.0f;
        int markX = x + (int)(fraction * w);
        uint16_t tColor = (k >= 10) ? COLOR_RED : COLOR_MUTED_GRAY;
        _canvas->drawFastVLine(markX, y - 8, 8, tColor);
        char numBuf[4];
        snprintf(numBuf, sizeof(numBuf), "%d", k);
        _canvas->setTextColor(tColor);
        if (k == 0) {
            _canvas->drawString(numBuf, markX, y - 22, &fonts::Font2);
        } else if (k == 12) {
            _canvas->drawRightString(numBuf, markX, y - 22, &fonts::Font2);
        } else {
            _canvas->drawCenterString(numBuf, markX, y - 22, &fonts::Font2);
        }
    }

    _canvas->fillRoundRect(x, y, w, h, 4, COLOR_SURFACE);
    _canvas->drawRoundRect(x, y, w, h, 4, COLOR_BORDER);

    // Left Wing Card (Battery & Electrical)
    int leftX = 20, leftY = 62, leftW = 240, leftH = 226;
    drawCard(leftX, leftY, leftW, leftH, COLOR_SURFACE, COLOR_BORDER);
    _canvas->setTextColor(COLOR_LIGHT_GRAY);
    _canvas->drawString("BATTERY", leftX + 16, leftY + 12, &fonts::Font2);
    int bBarX = leftX + 16, bBarY = leftY + 44, bBarW = leftW - 32, bBarH = 14;
    _canvas->fillRoundRect(bBarX, bBarY, bBarW, bBarH, 3, 0x0000);
    _canvas->drawRoundRect(bBarX, bBarY, bBarW, bBarH, 3, COLOR_BORDER);
    _canvas->drawFastHLine(leftX + 16, leftY + 70, leftW - 32, COLOR_BORDER);

    // Center Speed Unit Label (crisp Font4)
    int cx = 410;
    _canvas->setTextColor(COLOR_LIGHT_GRAY);
    _canvas->drawCenterString("KM/H", cx, 172, &fonts::Font4);

    // Right Wing Card (Thermals & Distance)
    int rightX = 560, rightY = 62, rightW = 240, rightH = 226;
    drawCard(rightX, rightY, rightW, rightH, COLOR_SURFACE, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("MOTOR TEMP", rightX + 16, rightY + 12, &fonts::Font2);
    _canvas->drawString("ESC TEMP", rightX + 16, rightY + 54, &fonts::Font2);
    _canvas->drawFastHLine(rightX + 16, rightY + 90, rightW - 32, COLOR_BORDER);
    _canvas->drawString("TRIP", rightX + 16, rightY + 104, &fonts::Font2);

    // Bottom Status Ribbon
    _canvas->drawFastHLine(20, 298, 780, COLOR_BORDER);
    _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED);
    _canvas->drawString(telemetry.vesc_connected ? "● VESC UART" : "○ NO VESC", 20, 303, &fonts::Font2);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawCenterString("BTN1: NEXT SCREEN  |  BTN2: RIDE MODE", cx, 303, &fonts::Font2);
}

void DashboardRenderer::renderHorizontalTachScreen(const DashTelemetry &telemetry) {
    int cx = 410;

    // 1. Dynamic RPM Bar
    drawPreciseRpmBar(20, 30, 780, 20, telemetry.rpm, 12000.0f);

    // 2. Left Wing (Battery & Electrical)
    int leftX = 20, leftY = 62, leftW = 240;

    // Battery % (flicker-free text padding)
    int curBat = (int)roundf(telemetry.battery_pct);
    if (curBat != _cache.battery_pct) {
        _cache.battery_pct = curBat;
        char batPctStr[16];
        snprintf(batPctStr, sizeof(batPctStr), "%d%%", curBat);
        _canvas->setTextColor((curBat > 25) ? COLOR_GREEN : (curBat > 15 ? COLOR_AMBER : COLOR_RED), COLOR_SURFACE);
        _canvas->setTextPadding(80);
        _canvas->drawRightString(batPctStr, leftX + leftW - 16, leftY + 10, &fonts::Font4);
        _canvas->setTextPadding(0);

        // Battery Bar Fill
        int bBarX = leftX + 16, bBarY = leftY + 44, bBarW = leftW - 32, bBarH = 14;
        _canvas->fillRect(bBarX + 1, bBarY + 1, bBarW - 2, bBarH - 2, 0x0000);
        float bFrac = curBat / 100.0f;
        if (bFrac > 1.0f) bFrac = 1.0f;
        if (bFrac < 0.0f) bFrac = 0.0f;
        int bFillW = (int)(bFrac * (bBarW - 2));
        if (bFillW > 0) {
            uint16_t bColor = (curBat > 25) ? COLOR_GREEN : (curBat > 15 ? COLOR_AMBER : COLOR_RED);
            _canvas->fillRect(bBarX + 1, bBarY + 1, bFillW, bBarH - 2, bColor);
        }
    }

    // Voltage, Amps, Watts (independent differential updates with zero-flicker text padding)
    int v_x10 = (int)roundf(telemetry.voltage * 10.0f);
    if (v_x10 != _cache.voltage_x10) {
        _cache.voltage_x10 = v_x10;
        char vStr[20];
        snprintf(vStr, sizeof(vStr), "%.1f V", telemetry.voltage);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(leftW - 32);
        _canvas->drawString(vStr, leftX + 16, leftY + 84, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int a_x10 = (int)roundf(telemetry.current_amps * 10.0f);
    if (a_x10 != _cache.current_x10) {
        _cache.current_x10 = a_x10;
        char aStr[20];
        snprintf(aStr, sizeof(aStr), "%.1f A", telemetry.current_amps);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(leftW - 32);
        _canvas->drawString(aStr, leftX + 16, leftY + 126, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int p_w = (int)roundf(telemetry.power_watts);
    if (p_w != _cache.watts) {
        _cache.watts = p_w;
        char pStr[20];
        if (p_w >= 1000) {
            snprintf(pStr, sizeof(pStr), "%.2f kW", p_w / 1000.0f);
        } else {
            snprintf(pStr, sizeof(pStr), "%d W", p_w);
        }
        _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
        _canvas->setTextPadding(leftW - 32);
        _canvas->drawString(pStr, leftX + 16, leftY + 168, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 3. Center: Giant Speedometer (native Font8, 75px tall, crisp, no scaling blockiness)
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        _canvas->fillRect(cx - 95, 86, 190, 80, COLOR_BG);
        char speedStr[8];
        snprintf(speedStr, sizeof(speedStr), "%d", curSpeed);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->drawCenterString(speedStr, cx, 88, &fonts::Font8);
    }

    // Ride Mode Pill: only redraw if changed
    if (telemetry.mode != _last_mode) {
        _last_mode = telemetry.mode;
        const char *modeNames[] = { "ECO", "STANDARD", "SPORT", "BOOST" };
        uint16_t modeColors[]   = { COLOR_GREEN, COLOR_BLUE, COLOR_AMBER, COLOR_RED };
        int mIdx = (int)telemetry.mode;
        int pillW = 130, pillH = 32, pillX = cx - (pillW / 2), pillY = 212;
        _canvas->fillRoundRect(pillX, pillY, pillW, pillH, 5, COLOR_SURFACE);
        _canvas->drawRoundRect(pillX, pillY, pillW, pillH, 5, modeColors[mIdx]);
        _canvas->setTextColor(modeColors[mIdx], COLOR_SURFACE);
        _canvas->drawCenterString(modeNames[mIdx], cx, pillY + 7, &fonts::Font4);
    }

    // 4. Right Wing: Thermals & Trip (independent differential updates with zero-flicker text padding)
    int rightX = 560, rightY = 62, rightW = 240;
    int mTemp = (int)roundf(telemetry.temp_motor);
    if (mTemp != _cache.temp_motor) {
        _cache.temp_motor = mTemp;
        char mTempStr[16];
        snprintf(mTempStr, sizeof(mTempStr), "%d°C", mTemp);
        _canvas->setTextColor((mTemp > 75) ? COLOR_RED : COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(80);
        _canvas->drawRightString(mTempStr, rightX + rightW - 16, rightY + 10, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int eTemp = (int)roundf(telemetry.temp_esc);
    if (eTemp != _cache.temp_esc) {
        _cache.temp_esc = eTemp;
        char eTempStr[16];
        snprintf(eTempStr, sizeof(eTempStr), "%d°C", eTemp);
        _canvas->setTextColor((eTemp > 80) ? COLOR_RED : COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(80);
        _canvas->drawRightString(eTempStr, rightX + rightW - 16, rightY + 50, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Trip & ODO
    int trip_x10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (trip_x10 != _cache.trip_x10) {
        _cache.trip_x10 = trip_x10;
        char tripStr[20];
        snprintf(tripStr, sizeof(tripStr), "%.1f km", telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(rightW - 32);
        _canvas->drawString(tripStr, rightX + 16, rightY + 126, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int odo = (int)roundf(telemetry.odo_km);
    if (odo != _cache.odo) {
        _cache.odo = odo;
        char odoStr[24];
        snprintf(odoStr, sizeof(odoStr), "ODO: %d km", odo);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(rightW - 32);
        _canvas->drawString(odoStr, rightX + 16, rightY + 172, &fonts::Font2);
        _canvas->setTextPadding(0);
    }

    // 5. Status ribbon & Uptime Clock
    if (telemetry.vesc_connected != _cache.vesc_connected) {
        _cache.vesc_connected = telemetry.vesc_connected;
        _canvas->fillRect(20, 303, 140, 16, COLOR_BG);
        _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        _canvas->drawString(telemetry.vesc_connected ? "● VESC UART" : "○ NO VESC", 20, 303, &fonts::Font2);
    }

    int uptime = (int)telemetry.uptime_sec;
    if (uptime != _cache.uptime) {
        _cache.uptime = uptime;
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", uptime / 3600, (uptime % 3600) / 60, uptime % 60);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
        _canvas->setTextPadding(90);
        _canvas->drawRightString(timeStr, 800, 303, &fonts::Font2);
        _canvas->setTextPadding(0);
    }
}

void DashboardRenderer::drawPreciseRpmBar(int x, int y, int w, int h, float rpm, float max_rpm) {
    int cx = x + (w / 2); // 410

    // 1. Digital RPM Readout (centered under the bar) - only redraw if changed
    int curRpm = (int)roundf(rpm);
    if (curRpm != _cache.rpm_val) {
        _cache.rpm_val = curRpm;
        char rpmStr[20];
        snprintf(rpmStr, sizeof(rpmStr), "%d RPM", curRpm);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(140);
        _canvas->drawCenterString(rpmStr, cx, 58, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 2. Differential Bar Fill
    float fillFrac = rpm / max_rpm;
    if (fillFrac > 1.0f) fillFrac = 1.0f;
    if (fillFrac < 0.0f) fillFrac = 0.0f;

    int fillW = (int)(fillFrac * (w - 4));
    if (fillW != _cache.rpm_fill_w) {
        int oldW = _cache.rpm_fill_w;
        if (oldW < 0) {
            _canvas->fillRect(x + 2, y + 2, w - 4, h - 4, COLOR_SURFACE);
            oldW = 0;
        }
        _cache.rpm_fill_w = fillW;

        if (fillW < oldW) {
            // Bar retracted: clear only the vacated tip
            _canvas->fillRect(x + 2 + fillW, y + 2, oldW - fillW, h - 4, COLOR_SURFACE);
        } else {
            // Bar extended: draw only the new extension with appropriate zones
            int normalMax  = (int)((w - 4) * (8000.0f / 12000.0f));
            int warningMax = (int)((w - 4) * (10000.0f / 12000.0f));

            int s = oldW;
            int e = fillW;
            // Normal (White): 0 to 8,000 RPM
            if (s < normalMax) {
                int segEnd = min(e, normalMax);
                if (segEnd > s) _canvas->fillRect(x + 2 + s, y + 2, segEnd - s, h - 4, COLOR_WHITE);
            }
            // Warning (Amber): 8,000 to 10,000 RPM
            if (e > normalMax && s < warningMax) {
                int segStart = max(s, normalMax);
                int segEnd   = min(e, warningMax);
                if (segEnd > segStart) _canvas->fillRect(x + 2 + segStart, y + 2, segEnd - segStart, h - 4, COLOR_AMBER);
            }
            // Redline (Red): > 10,000 RPM
            if (e > warningMax) {
                int segStart = max(s, warningMax);
                if (e > segStart) _canvas->fillRect(x + 2 + segStart, y + 2, e - segStart, h - 4, COLOR_RED);
            }
        }
    }

    // Shift Light Flash at Redline (> 10,000 RPM)
    if (rpm >= 10000.0f) {
        bool flash = (millis() / 90) % 2;
        if (flash) {
            _canvas->fillRoundRect(x + w - 75, y + 2, 72, h - 4, 3, COLOR_RED);
            _canvas->setTextColor(COLOR_WHITE, COLOR_RED);
            _canvas->drawCenterString("SHIFT", x + w - 39, y + 3, &fonts::Font2);
        } else {
            _canvas->fillRoundRect(x + w - 75, y + 2, 72, h - 4, 3, COLOR_SURFACE);
        }
    }
}

// ==============================================================================
// SCREEN 2: YAMAHA R1 SUPERBIKE ANALOG DIAL + COCKPIT
// ==============================================================================
void DashboardRenderer::initR1DialScreen(const DashTelemetry &telemetry) {
    int cx = 160, cy = 160, radius = 132;

    // Solid Dark Outer Bezel
    _canvas->fillCircle(cx, cy, radius + 10, COLOR_SURFACE);
    _canvas->drawCircle(cx, cy, radius + 10, COLOR_BORDER);
    _canvas->drawCircle(cx, cy, radius + 8, COLOR_BG);
    _canvas->fillCircle(cx, cy, radius + 6, COLOR_SURFACE);

    // Track arc & redline arc
    const float start_angle = 2.35619f;  // 135 deg (down-left)
    const float sweep_range = 4.71239f;  // 270 deg (clockwise to 45 deg down-right)
    _canvas->drawArc(cx, cy, radius - 4, radius, (start_angle * 180.0f / M_PI), ((start_angle + sweep_range) * 180.0f / M_PI), COLOR_BORDER);
    float redline_start = start_angle + (9000.0f / 12000.0f) * sweep_range;
    _canvas->drawArc(cx, cy, radius - 6, radius, (redline_start * 180.0f / M_PI), ((start_angle + sweep_range) * 180.0f / M_PI), COLOR_RED);

    // Static Ticks & Labels
    for (int i = 0; i <= 12; i++) {
        float frac = (float)i / 12.0f;
        float angle = start_angle + frac * sweep_range;
        bool isMajor = (i % 2 == 0);
        int tickLen = isMajor ? 10 : 5;

        int x1 = cx + (int)(cosf(angle) * (radius - 4));
        int y1 = cy + (int)(sinf(angle) * (radius - 4));
        int x2 = cx + (int)(cosf(angle) * (radius - 4 - tickLen));
        int y2 = cy + (int)(sinf(angle) * (radius - 4 - tickLen));

        uint16_t tickColor = (i >= 9) ? COLOR_RED : COLOR_WHITE;
        _canvas->drawLine(x1, y1, x2, y2, tickColor);

        if (isMajor) {
            // Place numbers at radius 108 (ticks end at 118, needle tip reaches 92)
            int numX = cx + (int)(cosf(angle) * 108);
            int numY = cy + (int)(sinf(angle) * 108);
            char numBuf[4];
            snprintf(numBuf, sizeof(numBuf), "%d", i);
            _canvas->setTextColor((i >= 9) ? COLOR_RED : COLOR_WHITE, COLOR_SURFACE);
            _canvas->drawCenterString(numBuf, numX, numY - 6, &fonts::Font2);
        }
    }

    // Label in the bottom dead zone (needle never sweeps here)
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawCenterString("r/min x1000", cx, cy + 54, &fonts::Font2);

    // Right Cockpit Layout
    int rX = 320, rY = 12, rW = 480;
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("MODE", rX + 50, rY + 4, &fonts::Font2);
    _canvas->drawFastHLine(rX, 54, rW, COLOR_BORDER);

    // Unit label KM/H beside speed
    _canvas->setTextColor(COLOR_LIGHT_GRAY);
    _canvas->drawString("KM/H", rX + 175, 115, &fonts::Font4);

    // Power header
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawRightString("POWER", rX + rW, 64, &fonts::Font2);

    // 3 Bottom Telemetry Cards (y = 175 to 290, completely clear of speed above)
    int botY = 175, cardW = (rW - 24) / 3, cardH = 115;
    drawCard(rX, botY, cardW, cardH, COLOR_SURFACE, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("BATTERY", rX + 12, botY + 8, &fonts::Font2);

    drawCard(rX + cardW + 12, botY, cardW, cardH, COLOR_SURFACE, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("TEMPERATURES", rX + cardW + 24, botY + 8, &fonts::Font2);

    drawCard(rX + (cardW * 2) + 24, botY, cardW, cardH, COLOR_SURFACE, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawString("DISTANCE", rX + (cardW * 2) + 36, botY + 8, &fonts::Font2);

    // Bottom Navigation Guide
    _canvas->drawFastHLine(rX, 298, rW, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY);
    _canvas->drawCenterString("BTN1: NEXT SCREEN  |  BTN2: RIDE MODE", rX + (rW / 2), 303, &fonts::Font2);
}

void DashboardRenderer::renderR1DialScreen(const DashTelemetry &telemetry) {
    float displayRpm = _is_sweeping ? _sweep_rpm : telemetry.rpm;
    int cx = 160, cy = 160;

    // 1. Erase previous needle if valid (differential erase leaves dial face intact)
    if (_last_needle.valid) {
        _canvas->fillTriangle(_last_needle.tipX, _last_needle.tipY,
                              _last_needle.b1X, _last_needle.b1Y,
                              _last_needle.b2X, _last_needle.b2Y,
                              COLOR_SURFACE);
        _canvas->drawLine(_last_needle.b1X, _last_needle.b1Y, _last_needle.tipX, _last_needle.tipY, COLOR_SURFACE);
        _canvas->drawLine(_last_needle.b2X, _last_needle.b2Y, _last_needle.tipX, _last_needle.tipY, COLOR_SURFACE);
        _canvas->drawLine(cx, cy, _last_needle.tipX, _last_needle.tipY, COLOR_SURFACE);
    }

    // 2. Compute new needle geometry
    const float start_angle = 2.35619f;  // 135 deg
    const float sweep_range = 4.71239f;  // 270 deg
    float needle_frac = displayRpm / 12000.0f;
    if (needle_frac > 1.0f) needle_frac = 1.0f;
    if (needle_frac < 0.0f) needle_frac = 0.0f;
    float needle_angle = start_angle + needle_frac * sweep_range;

    // Length 92 px keeps tip inside scale numbers (radius 108) with 16px safety margin
    drawR1Needle(cx, cy, 92, needle_angle, COLOR_NEEDLE);

    // 3. Redraw Center Hub Cap over base of needle
    _canvas->fillCircle(cx, cy, 22, COLOR_SURFACE_LIGHT);
    _canvas->drawCircle(cx, cy, 22, COLOR_BORDER);
    _canvas->fillCircle(cx, cy, 8, COLOR_BG);

    // 4. Digital RPM Readout under hub (in dead zone where needle never goes)
    int curRpm = (int)roundf(displayRpm);
    if (curRpm != _cache.rpm_val) {
        _cache.rpm_val = curRpm;
        char digRpm[16];
        snprintf(digRpm, sizeof(digRpm), "%d", curRpm);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(90);
        _canvas->drawCenterString(digRpm, cx, cy + 30, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 5. Right Cockpit Updates
    int rX = 320, rY = 12, rW = 480;

    // Mode Box: only redraw if mode changed
    if (telemetry.mode != _last_mode) {
        _last_mode = telemetry.mode;
        int modeNum = (int)telemetry.mode + 1;
        char modeBoxStr[4];
        snprintf(modeBoxStr, sizeof(modeBoxStr), "%d", modeNum);

        _canvas->fillRoundRect(rX, rY, 38, 36, 6, COLOR_SURFACE);
        _canvas->drawRoundRect(rX, rY, 38, 36, 6, COLOR_RED);
        _canvas->setTextColor(COLOR_RED, COLOR_SURFACE);
        _canvas->drawCenterString(modeBoxStr, rX + 19, rY + 6, &fonts::Font4);

        _canvas->fillRect(rX + 48, rY + 18, 110, 24, COLOR_BG);
        const char *modeNames[] = { "ECO", "STANDARD", "SPORT", "BOOST" };
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->drawString(modeNames[(int)telemetry.mode], rX + 48, rY + 18, &fonts::Font4);
    }

    // VESC Connection Indicator
    if (telemetry.vesc_connected != _cache.vesc_connected) {
        _cache.vesc_connected = telemetry.vesc_connected;
        _canvas->fillRect(rX + 170, rY + 10, 110, 18, COLOR_BG);
        _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        _canvas->drawString(telemetry.vesc_connected ? "● VESC" : "○ NO VESC", rX + 170, rY + 10, &fonts::Font2);
    }

    // Uptime Clock
    int uptime = (int)telemetry.uptime_sec;
    if (uptime != _cache.uptime) {
        _cache.uptime = uptime;
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", uptime / 3600, (uptime % 3600) / 60, uptime % 60);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
        _canvas->setTextPadding(90);
        _canvas->drawRightString(timeStr, rX + rW, rY + 10, &fonts::Font2);
        _canvas->setTextPadding(0);
    }

    // Speed (crisp native Font8, 75px tall, 1:1 scale, zero blockiness, clean clear rect)
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        _canvas->fillRect(rX + 10, 68, 160, 78, COLOR_BG);
        char speedStr[8];
        snprintf(speedStr, sizeof(speedStr), "%d", curSpeed);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->drawString(speedStr, rX + 15, 70, &fonts::Font8);
    }

    // Power & Current (independent differential updates with zero-flicker text padding)
    int p_w = (int)roundf(telemetry.power_watts);
    if (p_w != _cache.watts) {
        _cache.watts = p_w;
        char pwrStr[24];
        if (p_w >= 1000) {
            snprintf(pwrStr, sizeof(pwrStr), "%.2f kW", p_w / 1000.0f);
        } else {
            snprintf(pwrStr, sizeof(pwrStr), "%d W", p_w);
        }
        _canvas->setTextColor(COLOR_AMBER, COLOR_BG);
        _canvas->setTextPadding(160);
        _canvas->drawRightString(pwrStr, rX + rW, 80, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int a_x10 = (int)roundf(telemetry.current_amps * 10.0f);
    if (a_x10 != _cache.current_x10) {
        _cache.current_x10 = a_x10;
        char ampStr[16];
        snprintf(ampStr, sizeof(ampStr), "%.1f A", telemetry.current_amps);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
        _canvas->setTextPadding(160);
        _canvas->drawRightString(ampStr, rX + rW, 116, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 3 Bottom Telemetry Cards
    int botY = 175, cardW = (rW - 24) / 3;

    // Card 1: Battery % and Voltage
    int curBat = (int)roundf(telemetry.battery_pct);
    if (curBat != _cache.battery_pct) {
        _cache.battery_pct = curBat;
        char batStr[16];
        snprintf(batStr, sizeof(batStr), "%d%%", curBat);
        _canvas->setTextColor((curBat > 25) ? COLOR_GREEN : (curBat > 15 ? COLOR_AMBER : COLOR_RED), COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(batStr, rX + 12, botY + 30, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    if (curVoltX10 != _cache.voltage_x10) {
        _cache.voltage_x10 = curVoltX10;
        char voltStr[16];
        snprintf(voltStr, sizeof(voltStr), "%.1f V", telemetry.voltage);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(voltStr, rX + 12, botY + 68, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 2: Motor & ESC Temperatures
    int mTemp = (int)roundf(telemetry.temp_motor);
    if (mTemp != _cache.temp_motor) {
        _cache.temp_motor = mTemp;
        char mTempStr[16];
        snprintf(mTempStr, sizeof(mTempStr), "M %d°C", mTemp);
        _canvas->setTextColor((mTemp > 75) ? COLOR_RED : COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(mTempStr, rX + cardW + 24, botY + 30, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int eTemp = (int)roundf(telemetry.temp_esc);
    if (eTemp != _cache.temp_esc) {
        _cache.temp_esc = eTemp;
        char eTempStr[16];
        snprintf(eTempStr, sizeof(eTempStr), "E %d°C", eTemp);
        _canvas->setTextColor((eTemp > 80) ? COLOR_RED : COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(eTempStr, rX + cardW + 24, botY + 68, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 3: Trip Distance & ODO
    int tripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (tripX10 != _cache.trip_x10) {
        _cache.trip_x10 = tripX10;
        char tripStr[16];
        snprintf(tripStr, sizeof(tripStr), "%.1f km", telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(tripStr, rX + (cardW * 2) + 36, botY + 30, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int odo = (int)roundf(telemetry.odo_km);
    if (odo != _cache.odo) {
        _cache.odo = odo;
        char odoStr[20];
        snprintf(odoStr, sizeof(odoStr), "ODO %d", odo);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(cardW - 20);
        _canvas->drawString(odoStr, rX + (cardW * 2) + 36, botY + 68, &fonts::Font2);
        _canvas->setTextPadding(0);
    }
}

void DashboardRenderer::drawR1Needle(int cx, int cy, int length, float angle_rad, uint16_t color) {
    int tipX = cx + (int)(cosf(angle_rad) * length);
    int tipY = cy + (int)(sinf(angle_rad) * length);

    float perp = angle_rad + (M_PI / 2.0f);
    int b1X = cx + (int)(cosf(perp) * 5) - (int)(cosf(angle_rad) * 12);
    int b1Y = cy + (int)(sinf(perp) * 5) - (int)(sinf(angle_rad) * 12);
    int b2X = cx - (int)(cosf(perp) * 5) - (int)(cosf(angle_rad) * 12);
    int b2Y = cy - (int)(sinf(perp) * 5) - (int)(sinf(angle_rad) * 12);

    _canvas->fillTriangle(tipX, tipY, b1X, b1Y, b2X, b2Y, color);

    _last_needle.tipX = tipX;
    _last_needle.tipY = tipY;
    _last_needle.b1X  = b1X;
    _last_needle.b1Y  = b1Y;
    _last_needle.b2X  = b2X;
    _last_needle.b2Y  = b2Y;
    _last_needle.valid = true;
}

// ==============================================================================
// SCREEN 3: SETTINGS & DIAGNOSTICS MENU
// ==============================================================================
void DashboardRenderer::initSettingsScreen(const DashTelemetry &telemetry) {
    // Header
    _canvas->fillRoundRect(20, 15, 780, 42, 6, COLOR_SURFACE);
    _canvas->drawRoundRect(20, 15, 780, 42, 6, COLOR_BORDER);
    _canvas->setTextColor(COLOR_WHITE);
    _canvas->drawString("SYSTEM SETTINGS & TELEMETRY DIAGNOSTICS", 36, 26, &fonts::Font4);

    // Menu Card Container
    drawCard(20, 68, 780, 215, COLOR_SURFACE, COLOR_BORDER);

    const char* items[] = {
        "1. Active Power Map",
        "2. Motor Pole Pairs",
        "3. Wheel Diameter",
        "4. Display Brightness",
        "5. VESC UART Baudrate",
        "6. Re-trigger R1 Needle Sweep Test"
    };

    const char* values[] = {
        "SPORT (3)",
        "15 Pairs (Hub Motor)",
        "26 Inch (660 mm)",
        "85 %",
        "115200 bps",
        "[ RUN TEST ]"
    };

    for (int i = 0; i < 6; i++) {
        int rowY = 82 + (i * 32);
        _canvas->setTextColor(COLOR_LIGHT_GRAY);
        _canvas->drawString(items[i], 40, rowY, &fonts::Font4);

        _canvas->setTextColor(COLOR_AMBER);
        _canvas->drawRightString(values[i], 760, rowY, &fonts::Font4);

        if (i < 5) {
            _canvas->drawFastHLine(40, rowY + 28, 740, COLOR_BORDER);
        }
    }

    // Bottom Navigation Guide
    _canvas->drawFastHLine(20, 296, 780, COLOR_BORDER);
    _canvas->setTextColor(COLOR_WHITE);
    _canvas->drawCenterString("BTN 1: NAVIGATE / BACK   |   BTN 2: SELECT / CHANGE", 410, 303, &fonts::Font2);
}

void DashboardRenderer::renderSettingsScreen(const DashTelemetry &telemetry) {
    // Static settings screen - updates only when interactive menu items are selected
}

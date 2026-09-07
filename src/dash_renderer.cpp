#include "dash_renderer.h"
#include "buttons.h"
#include <math.h>

DashboardRenderer Renderer;

// Modern High-Contrast Color Palette
static const uint16_t COLOR_BG            = 0x0821; // Deep Obsidian Black
static const uint16_t COLOR_SURFACE       = 0x10A2; // Card Background
static const uint16_t COLOR_SURFACE_LIGHT = 0x2124; // Card Highlight / Active Item
static const uint16_t COLOR_BORDER        = 0x2965; // High-definition Border
static const uint16_t COLOR_WHITE         = 0xFFFF; // Pure White
static const uint16_t COLOR_LIGHT_GRAY    = 0xCE79; // Crisp Secondary Gray
static const uint16_t COLOR_MUTED_GRAY    = 0x8410; // Tertiary Gray
static const uint16_t COLOR_AMBER         = 0xFD20; // High Power / Warning
static const uint16_t COLOR_RED           = 0xF800; // Redline / Field Weakening / Alarm
static const uint16_t COLOR_GREEN         = 0x07E0; // Connected / Safe / Efficient
static const uint16_t COLOR_CYAN          = 0x07FF; // Electric Accent / Profile
static const uint16_t COLOR_NEEDLE        = 0xFC00; // High-Visibility Sport Orange-Red

DashboardRenderer::DashboardRenderer()
    : _display(nullptr),
      _canvas(nullptr),
      _active_screen(SCREEN_RIDE_DASH),
      _active_style(STYLE_ANALOG_DIAL),
      _screen_dirty(true),
      _menu_in_sub(false),
      _menu_root_idx(0),
      _menu_sub_idx(0),
      _menu_edit_mode(false),
      _menu_last_activity_ms(0),
      _menu_dirty(true),
      _is_sweeping(false),
      _sweep_phase(0),
      _sweep_progress(0.0f),
      _sweep_duty(0.0f),
      _last_sweep_tick_ms(0) {
    _last_needle.valid = false;
    _cache.invalidate();
}

void DashboardRenderer::begin(DisplayDriver *display) {
    _display = display;
    _canvas = display->getCanvas();
    _cache.invalidate();
    _screen_dirty = true;
    _active_style = (DashboardStyle)Settings.get().dash_style;
}

void DashboardRenderer::drawCard(int x, int y, int w, int h, uint16_t bg, uint16_t border) {
    _canvas->fillRoundRect(x, y, w, h, 8, bg);
    _canvas->drawRoundRect(x, y, w, h, 8, border);
}

void DashboardRenderer::triggerNeedleSweep() {
    _is_sweeping = true;
    _sweep_phase = 0;
    _sweep_progress = 0.0f;
    _sweep_duty = 0.0f;
    _last_sweep_tick_ms = millis();
}

// Top-Level Dispatcher
void DashboardRenderer::render(const DashTelemetry &telemetry) {
    // 1. Needle sweep animation physics (120% duty cycle sweep)
    if (_is_sweeping) {
        uint32_t now = millis();
        float dt = (now - _last_sweep_tick_ms) / 1000.0f;
        _last_sweep_tick_ms = now;

        if (_sweep_phase == 0) { // Sweep up: 0 -> 120% in 0.6s
            _sweep_progress += dt / 0.6f;
            if (_sweep_progress >= 1.0f) {
                _sweep_progress = 1.0f;
                _sweep_phase = 1;
            }
            _sweep_duty = _sweep_progress * 120.0f;
        } else if (_sweep_phase == 1) { // Sweep down: 120 -> 0% in 0.5s
            _sweep_progress -= dt / 0.5f;
            if (_sweep_progress <= 0.0f) {
                _sweep_progress = 0.0f;
                _sweep_duty = 0.0f;
                _is_sweeping = false;
            } else {
                _sweep_duty = _sweep_progress * 120.0f;
            }
        }
    }

    // 2. Detect Screen Switch
    if (telemetry.screen != _active_screen) {
        _active_screen = telemetry.screen;
        _screen_dirty = true;
        _cache.invalidate();
        _last_needle.valid = false;
        if (_active_screen == SCREEN_RIDE_DASH && Settings.get().dash_style == STYLE_ANALOG_DIAL) {
            triggerNeedleSweep();
        }
    }

    // 3. Detect Style Switch in Ride Dashboard
    if (_active_screen == SCREEN_RIDE_DASH) {
        DashboardStyle st = (DashboardStyle)Settings.get().dash_style;
        if (st != _active_style) {
            _active_style = st;
            _screen_dirty = true;
            _cache.invalidate();
            _last_needle.valid = false;
        }
    }

    // 4. Dispatch Screen Rendering
    switch (_active_screen) {
        case SCREEN_RIDE_DASH:
            if (_screen_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initRideDashboard(telemetry);
                _screen_dirty = false;
            }
            renderRideDashboard(telemetry);
            break;

        case SCREEN_TRIP_STATS:
            if (_screen_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initTripStatsScreen(telemetry);
                _screen_dirty = false;
            }
            renderTripStatsScreen(telemetry);
            break;

        case SCREEN_SETTINGS_MENU:
            if (_screen_dirty || _menu_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initSettingsScreen(telemetry);
                _screen_dirty = false;
                _menu_dirty = false;
            }
            renderSettingsScreen(telemetry);
            break;

        default:
            break;
    }

    _display->pushCanvas();
}

// ==============================================================================
// SCREEN 0: MAIN RIDE DASHBOARD
// ==============================================================================
void DashboardRenderer::initRideDashboard(const DashTelemetry &telemetry) {
    if (_active_style == STYLE_ANALOG_DIAL) {
        initLeftHugAnalogStyle(telemetry);
    } else {
        initHorizontalBarStyle(telemetry);
    }
}

void DashboardRenderer::renderRideDashboard(const DashTelemetry &telemetry) {
    if (_active_style == STYLE_ANALOG_DIAL) {
        renderLeftHugAnalogStyle(telemetry);
    } else {
        renderHorizontalBarStyle(telemetry);
    }
}

// ==============================================================================
// STYLE A: LEFT-HUG ANALOG DUTY CLUSTER (Based on User Gauge Photo)
// ==============================================================================
void DashboardRenderer::initLeftHugAnalogStyle(const DashTelemetry &telemetry) {
    int cx = 140, cy = 160, radius = 145;

    // 1. Dark Gauge Bezel & Face
    _canvas->fillCircle(cx, cy, radius, COLOR_SURFACE);
    _canvas->drawCircle(cx, cy, radius, COLOR_BORDER);
    _canvas->drawCircle(cx, cy, radius - 2, COLOR_BG);

    // 2. ~200° Clockwise Arc from ~5:00 o'clock (80.2°) to ~1:00 o'clock (279.8°)
    const float start_angle = 1.40f;   // 80.2° (bottom-right)
    const float sweep_range = 3.48f;   // 200° clockwise sweep

    // Normal Duty Arc (0% to 100%)
    float fw_start_angle = start_angle + (100.0f / 120.0f) * sweep_range; // 246.7°
    _canvas->drawArc(cx, cy, radius - 8, radius - 5,
                     (start_angle * 180.0f / M_PI),
                     (fw_start_angle * 180.0f / M_PI),
                     COLOR_BORDER);

    // Field Weakening Redline Arc (100% to 120%)
    _canvas->drawArc(cx, cy, radius - 10, radius - 5,
                     (fw_start_angle * 180.0f / M_PI),
                     ((start_angle + sweep_range) * 180.0f / M_PI),
                     COLOR_RED);

    // 3. Dial Ticks & Bold Labels
    for (int i = 0; i <= 24; i++) {
        float frac = (float)i / 24.0f; // every 5%
        float angle = start_angle + (frac * sweep_range);
        bool isMajor = (i % 4 == 0);   // every 20% (0, 20, 40, 60, 80, 100, 120)
        int tickLen = isMajor ? 14 : 7;

        int x1 = cx + (int)(cosf(angle) * (radius - 5));
        int y1 = cy + (int)(sinf(angle) * (radius - 5));
        int x2 = cx + (int)(cosf(angle) * (radius - 5 - tickLen));
        int y2 = cy + (int)(sinf(angle) * (radius - 5 - tickLen));

        uint16_t tColor = (i >= 20) ? COLOR_RED : ((i >= 16) ? COLOR_AMBER : COLOR_WHITE);
        _canvas->drawLine(x1, y1, x2, y2, tColor);

        if (isMajor) {
            int duty_val = i * 5;
            int numRadius = (duty_val >= 100) ? 96 : 102;
            int numX = cx + (int)(cosf(angle) * numRadius);
            int numY = cy + (int)(sinf(angle) * numRadius);

            if (duty_val == 120) {
                // Redline Field Weakening label
                _canvas->setTextColor(COLOR_RED, COLOR_SURFACE);
                _canvas->drawCenterString("FW", numX, numY - 10, &fonts::Font4);
            } else {
                char buf[6];
                snprintf(buf, sizeof(buf), "%d", duty_val);
                _canvas->setTextColor(tColor, COLOR_SURFACE);
                _canvas->drawCenterString(buf, numX, numY - 8, &fonts::Font2);
            }
        }
    }

    // 4. Dial Subtext
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->drawString("DUTY %", cx - 28, cy + 42, &fonts::Font2);

    // 5. Right Cockpit Background & Top Bar
    int rX = 280;
    _canvas->drawFastHLine(rX, 48, 820 - rX - 15, COLOR_BORDER);

    // Top Bar static labels
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
    _canvas->drawString(pName, rX, 16, &fonts::Font4);

    // Center Speed Unit Label
    int spdCenter = 550;
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
    _canvas->drawCenterString("KM/H", spdCenter, 140, &fonts::Font4);

    // Bottom Two Prominent Telemetry Cards
    int cardY = 172, cardH = 138;
    int c1W = 265, c2W = 250;
    drawCard(rX, cardY, c1W, cardH, COLOR_SURFACE, COLOR_BORDER);
    drawCard(rX + c1W + 10, cardY, c2W, cardH, COLOR_SURFACE, COLOR_BORDER);

    // Card 1 Header
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("ELECTRICAL / POWER", rX + 14, cardY + 8, &fonts::Font2);

    // Card 2 Header
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("BATTERY & THERMALS", rX + c1W + 24, cardY + 8, &fonts::Font2);
}

void DashboardRenderer::drawAnalogNeedle(int cx, int cy, int length, float angle_rad, uint16_t color) {
    int tipX = cx + (int)(cosf(angle_rad) * length);
    int tipY = cy + (int)(sinf(angle_rad) * length);

    float perp = angle_rad + (M_PI / 2.0f);
    int b1X = cx + (int)(cosf(perp) * 6) - (int)(cosf(angle_rad) * 14);
    int b1Y = cy + (int)(sinf(perp) * 6) - (int)(sinf(angle_rad) * 14);
    int b2X = cx - (int)(cosf(perp) * 6) - (int)(cosf(angle_rad) * 14);
    int b2Y = cy - (int)(sinf(perp) * 6) - (int)(sinf(angle_rad) * 14);

    _canvas->fillTriangle(tipX, tipY, b1X, b1Y, b2X, b2Y, color);

    _last_needle.tipX = tipX;
    _last_needle.tipY = tipY;
    _last_needle.b1X  = b1X;
    _last_needle.b1Y  = b1Y;
    _last_needle.b2X  = b2X;
    _last_needle.b2Y  = b2Y;
    _last_needle.valid = true;
}

void DashboardRenderer::renderLeftHugAnalogStyle(const DashTelemetry &telemetry) {
    int cx = 140, cy = 160;
    float currentDuty = _is_sweeping ? _sweep_duty : telemetry.duty_cycle_pct;

    // 1. Erase Previous Needle (Zero-Flicker Differential Triangle Fill)
    if (_last_needle.valid) {
        _canvas->fillTriangle(_last_needle.tipX, _last_needle.tipY,
                              _last_needle.b1X, _last_needle.b1Y,
                              _last_needle.b2X, _last_needle.b2Y,
                              COLOR_SURFACE);
        _canvas->drawLine(_last_needle.b1X, _last_needle.b1Y, _last_needle.tipX, _last_needle.tipY, COLOR_SURFACE);
        _canvas->drawLine(_last_needle.b2X, _last_needle.b2Y, _last_needle.tipX, _last_needle.tipY, COLOR_SURFACE);
    }

    // 2. Calculate New Needle Angle
    const float start_angle = 1.40f;   // 80.2° (5:00 o'clock)
    const float sweep_range = 3.48f;   // 200° sweep
    float frac = currentDuty / 120.0f;
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    float needle_angle = start_angle + (frac * sweep_range);

    // Draw needle
    drawAnalogNeedle(cx, cy, 98, needle_angle, COLOR_NEEDLE);

    // 3. Redraw Center Hub Cap over needle pivot
    _canvas->fillCircle(cx, cy, 22, COLOR_SURFACE_LIGHT);
    _canvas->drawCircle(cx, cy, 22, COLOR_BORDER);
    _canvas->fillCircle(cx, cy, 8, COLOR_BG);

    // 4. Digital Duty Cycle Readout in Hub Deadzone
    int duty_int = (int)roundf(currentDuty);
    if (duty_int != _cache.duty_x10) {
        _cache.duty_x10 = duty_int;
        char dutyBuf[12];
        if (duty_int > 100) {
            snprintf(dutyBuf, sizeof(dutyBuf), "%d%% FW", duty_int);
            _canvas->setTextColor(COLOR_RED, COLOR_SURFACE);
        } else {
            snprintf(dutyBuf, sizeof(dutyBuf), "%d%%", duty_int);
            _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        }
        _canvas->setTextPadding(86);
        _canvas->drawCenterString(dutyBuf, cx, cy + 24, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 5. Right Cockpit - Top Ribbon Updates
    int rX = 280;

    // Battery Profile tag update
    if (telemetry.battery_profile_id != _cache.batt_prof) {
        _cache.batt_prof = telemetry.battery_profile_id;
        const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(140);
        _canvas->drawString(pName, rX, 16, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Voltage & Est Range update
    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    int curRange = (int)roundf(telemetry.est_range_km);
    if (curVoltX10 != _cache.voltage_x10 || curRange != _cache.est_range) {
        _cache.voltage_x10 = curVoltX10;
        _cache.est_range = curRange;
        char vStr[32];
        snprintf(vStr, sizeof(vStr), "%.1fV | EST %dkm", telemetry.voltage, curRange);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(210);
        _canvas->drawCenterString(vStr, 530, 16, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // VESC Indicator
    if (telemetry.vesc_connected != _cache.vesc_connected) {
        _cache.vesc_connected = telemetry.vesc_connected;
        _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        _canvas->setTextPadding(110);
        _canvas->drawRightString(telemetry.vesc_connected ? "● VESC" : "○ NO VESC", 805, 16, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 6. Giant Speedometer (native Font8, 75px, zero-flicker text padding)
    int spdCenter = 550;
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        char spdStr[8];
        snprintf(spdStr, sizeof(spdStr), "%d", curSpeed);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(200);
        _canvas->drawCenterString(spdStr, spdCenter, 60, &fonts::Font8);
        _canvas->setTextPadding(0);
    }

    // 7. Card 1: Electrical / Power Telemetry
    int cardY = 172, c1W = 265;

    // Battery Current (Giant Font6 / Font4)
    int curAmpsX10 = (int)roundf(telemetry.current_amps * 10.0f);
    int curPhaseX10 = (int)roundf(telemetry.phase_amps * 10.0f);
    if (curAmpsX10 != _cache.current_x10 || curPhaseX10 != _cache.phase_x10) {
        _cache.current_x10 = curAmpsX10;
        _cache.phase_x10 = curPhaseX10;
        char aStr[20], pStr[24];
        snprintf(aStr, sizeof(aStr), "%.1f A", telemetry.current_amps);
        snprintf(pStr, sizeof(pStr), "Phase: %.0fA", telemetry.phase_amps);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(aStr, rX + 14, cardY + 30, &fonts::Font6);
        _canvas->setTextPadding(0);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(pStr, rX + 14, cardY + 70, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Power (Watts / kW)
    int curWatts = (int)roundf(telemetry.power_watts);
    if (curWatts != _cache.watts) {
        _cache.watts = curWatts;
        char wStr[24];
        if (curWatts >= 1000) {
            snprintf(wStr, sizeof(wStr), "%.2f kW", curWatts / 1000.0f);
        } else {
            snprintf(wStr, sizeof(wStr), "%d W", curWatts);
        }
        _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(wStr, rX + 14, cardY + 102, &fonts::Font6);
        _canvas->setTextPadding(0);
    }

    // 8. Card 2: Battery & Motor Telemetry
    int c2X = rX + c1W + 10, c2W = 250;

    // Battery State of Charge & Wh
    int curBat = (int)roundf(telemetry.battery_pct);
    int curRemWh = (int)roundf(telemetry.remaining_wh);
    if (curBat != _cache.battery_pct || curRemWh != _cache.remaining_wh) {
        _cache.battery_pct = curBat;
        _cache.remaining_wh = curRemWh;
        char bStr[24], whStr[24];
        snprintf(bStr, sizeof(bStr), "%d%%", curBat);
        snprintf(whStr, sizeof(whStr), "%d Wh left", curRemWh);
        uint16_t bCol = (curBat > 25) ? COLOR_GREEN : (curBat > 15 ? COLOR_AMBER : COLOR_RED);
        _canvas->setTextColor(bCol, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(bStr, c2X + 14, cardY + 30, &fonts::Font6);
        _canvas->setTextPadding(0);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(whStr, c2X + 14, cardY + 70, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Thermals & Trip
    int mTemp = (int)roundf(telemetry.temp_motor);
    int eTemp = (int)roundf(telemetry.temp_esc);
    int curTripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (mTemp != _cache.temp_motor || eTemp != _cache.temp_esc || curTripX10 != _cache.trip_x10) {
        _cache.temp_motor = mTemp;
        _cache.temp_esc = eTemp;
        _cache.trip_x10 = curTripX10;
        char tStr[32];
        snprintf(tStr, sizeof(tStr), "M:%d°C E:%d°C | %.1fkm", mTemp, eTemp, telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(tStr, c2X + 14, cardY + 104, &fonts::Font4);
        _canvas->setTextPadding(0);
    }
}

// ==============================================================================
// STYLE B: HORIZONTAL DUTY BAR CLUSTER
// ==============================================================================
void DashboardRenderer::initHorizontalBarStyle(const DashTelemetry &telemetry) {
    // 1. Top Ribbon
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
    _canvas->drawString(pName, 20, 14, &fonts::Font4);

    char vStr[32];
    snprintf(vStr, sizeof(vStr), "%.1fV | EST %dkm", telemetry.voltage, (int)roundf(telemetry.est_range_km));
    _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas->drawCenterString(vStr, 410, 14, &fonts::Font4);

    _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
    _canvas->drawRightString(telemetry.vesc_connected ? "● VESC" : "○ NO VESC", 800, 14, &fonts::Font4);

    // 2. Horizontal Duty Bar Track (x=20, y=48, w=780, h=26)
    int bx = 20, by = 48, bw = 780, bh = 26;
    _canvas->fillRoundRect(bx, by, bw, bh, 4, COLOR_SURFACE);
    _canvas->drawRoundRect(bx, by, bw, bh, 4, COLOR_BORDER);

    // Field Weakening Boundary (100% / 120% * 780 = 650px)
    int fw_mark = bx + 650;
    _canvas->drawFastVLine(fw_mark, by, bh, COLOR_RED);
    _canvas->setTextColor(COLOR_RED, COLOR_BG);
    _canvas->drawRightString("FW ZONE", bx + bw, by - 16, &fonts::Font2);

    // Scale Ticks (0%, 20%, 40%, 60%, 80%, 100%)
    for (int i = 0; i <= 100; i += 20) {
        int tx = bx + (int)((float)i / 120.0f * (float)bw);
        _canvas->drawFastVLine(tx, by + bh, 4, COLOR_MUTED_GRAY);
        char sbuf[8];
        snprintf(sbuf, sizeof(sbuf), "%d%%", i);
        _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_BG);
        _canvas->drawCenterString(sbuf, tx, by + bh + 6, &fonts::Font2);
    }
    _canvas->drawFastVLine(fw_mark, by + bh, 4, COLOR_RED);
    _canvas->setTextColor(COLOR_RED, COLOR_BG);
    _canvas->drawCenterString("100%", fw_mark, by + bh + 6, &fonts::Font2);

    // 3. Center Speed Unit Label
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
    _canvas->drawCenterString("KM/H", 410, 156, &fonts::Font4);

    // 4. Three Bottom Telemetry Cards (y=188, h=122)
    int cardY = 188, cardH = 122;
    int c1W = 250, c2W = 260, c3W = 250;
    int c1X = 20, c2X = 280, c3X = 550;

    drawCard(c1X, cardY, c1W, cardH, COLOR_SURFACE, COLOR_BORDER);
    drawCard(c2X, cardY, c2W, cardH, COLOR_SURFACE, COLOR_BORDER);
    drawCard(c3X, cardY, c3W, cardH, COLOR_SURFACE, COLOR_BORDER);

    // Card Titles
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("ELECTRICAL / POWER", c1X + 12, cardY + 8, &fonts::Font2);
    _canvas->drawString("BATTERY & RANGE", c2X + 12, cardY + 8, &fonts::Font2);
    _canvas->drawString("MOTOR & HARDWARE", c3X + 12, cardY + 8, &fonts::Font2);

    _cache.duty_fill_w = -1;
}

void DashboardRenderer::drawPreciseDutyBar(int x, int y, int w, int h, float duty, float max_duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > max_duty) duty = max_duty;
    int target_w = (int)roundf((duty / max_duty) * (w - 4));
    if (target_w < 0) target_w = 0;
    if (target_w > w - 4) target_w = w - 4;

    if (target_w != _cache.duty_fill_w) {
        int fw_px = (int)roundf((100.0f / max_duty) * (w - 4));
        int last_w = (_cache.duty_fill_w >= 0) ? _cache.duty_fill_w : 0;

        if (target_w > last_w) {
            for (int px = last_w; px < target_w; px++) {
                uint16_t col = (px >= fw_px) ? COLOR_RED : COLOR_CYAN;
                _canvas->drawFastVLine(x + 2 + px, y + 2, h - 4, col);
            }
        } else if (target_w < last_w) {
            _canvas->fillRect(x + 2 + target_w, y + 2, last_w - target_w, h - 4, COLOR_SURFACE);
        }
        _cache.duty_fill_w = target_w;
    }
}

void DashboardRenderer::renderHorizontalBarStyle(const DashTelemetry &telemetry) {
    // 1. Top Ribbon Updates
    if (telemetry.battery_profile_id != _cache.batt_prof) {
        _cache.batt_prof = telemetry.battery_profile_id;
        const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(140);
        _canvas->drawString(pName, 20, 14, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    int curRange = (int)roundf(telemetry.est_range_km);
    if (curVoltX10 != _cache.voltage_x10 || curRange != _cache.est_range) {
        _cache.voltage_x10 = curVoltX10;
        _cache.est_range = curRange;
        char vStr[32];
        snprintf(vStr, sizeof(vStr), "%.1fV | EST %dkm", telemetry.voltage, curRange);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(220);
        _canvas->drawCenterString(vStr, 410, 14, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    if (telemetry.vesc_connected != _cache.vesc_connected) {
        _cache.vesc_connected = telemetry.vesc_connected;
        _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        _canvas->setTextPadding(110);
        _canvas->drawRightString(telemetry.vesc_connected ? "● VESC" : "○ NO VESC", 800, 14, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 2. Duty Cycle Bar & Readout
    float currentDuty = _is_sweeping ? _sweep_duty : telemetry.duty_cycle_pct;
    drawPreciseDutyBar(20, 48, 780, 26, currentDuty, 120.0f);

    int duty_int = (int)roundf(currentDuty);
    if (duty_int != _cache.duty_x10) {
        _cache.duty_x10 = duty_int;
        char dBuf[16];
        if (duty_int > 100) {
            snprintf(dBuf, sizeof(dBuf), "DUTY %d%% FW", duty_int);
            _canvas->setTextColor(COLOR_RED, COLOR_BG);
        } else {
            snprintf(dBuf, sizeof(dBuf), "DUTY %d%%", duty_int);
            _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        }
        _canvas->setTextPadding(140);
        _canvas->drawCenterString(dBuf, 410, 30, &fonts::Font2);
        _canvas->setTextPadding(0);
    }

    // 3. Giant Speedometer (Font8, 75px)
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        char spdStr[8];
        snprintf(spdStr, sizeof(spdStr), "%d", curSpeed);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(220);
        _canvas->drawCenterString(spdStr, 410, 80, &fonts::Font8);
        _canvas->setTextPadding(0);
    }

    // 4. Card 1: Electrical & Power
    int cardY = 188;
    int c1X = 20, c1W = 250;
    int curAmpsX10 = (int)roundf(telemetry.current_amps * 10.0f);
    int curPhaseX10 = (int)roundf(telemetry.phase_amps * 10.0f);
    if (curAmpsX10 != _cache.current_x10 || curPhaseX10 != _cache.phase_x10) {
        _cache.current_x10 = curAmpsX10;
        _cache.phase_x10 = curPhaseX10;
        char aStr[20], pStr[24];
        snprintf(aStr, sizeof(aStr), "%.1f A", telemetry.current_amps);
        snprintf(pStr, sizeof(pStr), "Phase: %.0fA", telemetry.phase_amps);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(aStr, c1X + 12, cardY + 28, &fonts::Font6);
        _canvas->setTextPadding(0);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(pStr, c1X + 12, cardY + 68, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curWatts = (int)roundf(telemetry.power_watts);
    if (curWatts != _cache.watts) {
        _cache.watts = curWatts;
        char wStr[24];
        if (curWatts >= 1000) {
            snprintf(wStr, sizeof(wStr), "%.2f kW", curWatts / 1000.0f);
        } else {
            snprintf(wStr, sizeof(wStr), "%d W", curWatts);
        }
        _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
        _canvas->setTextPadding(c1W - 24);
        _canvas->drawString(wStr, c1X + 12, cardY + 94, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 5. Card 2: Battery & Range
    int c2X = 280, c2W = 260;
    int curBat = (int)roundf(telemetry.battery_pct);
    int curRemWh = (int)roundf(telemetry.remaining_wh);
    if (curBat != _cache.battery_pct || curRemWh != _cache.remaining_wh) {
        _cache.battery_pct = curBat;
        _cache.remaining_wh = curRemWh;
        char bStr[20], whStr[24];
        snprintf(bStr, sizeof(bStr), "%d%%", curBat);
        snprintf(whStr, sizeof(whStr), "%d Wh left", curRemWh);
        uint16_t bCol = (curBat > 25) ? COLOR_GREEN : (curBat > 15 ? COLOR_AMBER : COLOR_RED);
        _canvas->setTextColor(bCol, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(bStr, c2X + 12, cardY + 28, &fonts::Font6);
        _canvas->setTextPadding(0);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(whStr, c2X + 12, cardY + 68, &fonts::Font4);
        _canvas->setTextPadding(0);

        char sohStr[24];
        snprintf(sohStr, sizeof(sohStr), "SoH: %.0f%% (Health)", telemetry.battery_health_soh);
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->setTextPadding(c2W - 24);
        _canvas->drawString(sohStr, c2X + 12, cardY + 94, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 6. Card 3: Motor & Trip
    int c3X = 550, c3W = 250;
    int mTemp = (int)roundf(telemetry.temp_motor);
    int eTemp = (int)roundf(telemetry.temp_esc);
    int curTripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (mTemp != _cache.temp_motor || eTemp != _cache.temp_esc || curTripX10 != _cache.trip_x10) {
        _cache.temp_motor = mTemp;
        _cache.temp_esc = eTemp;
        _cache.trip_x10 = curTripX10;
        char tStr[24], trStr[24];
        snprintf(tStr, sizeof(tStr), "M:%d°C  E:%d°C", mTemp, eTemp);
        snprintf(trStr, sizeof(trStr), "%.1f km", telemetry.trip_km);

        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(c3W - 24);
        _canvas->drawString(trStr, c3X + 12, cardY + 28, &fonts::Font6);
        _canvas->setTextPadding(0);

        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(c3W - 24);
        _canvas->drawString(tStr, c3X + 12, cardY + 68, &fonts::Font4);
        _canvas->setTextPadding(0);

        char rpmStr[24];
        snprintf(rpmStr, sizeof(rpmStr), "Motor: %.0f RPM", telemetry.rpm);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(c3W - 24);
        _canvas->drawString(rpmStr, c3X + 12, cardY + 94, &fonts::Font4);
        _canvas->setTextPadding(0);
    }
}

// ==============================================================================
// SCREEN 1: TRIP & ENERGY STATISTICS DASHBOARD
// ==============================================================================
void DashboardRenderer::initTripStatsScreen(const DashTelemetry &telemetry) {
    // Top Bar
    _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas->drawString("TRIP & ENERGY ANALYTICS", 20, 14, &fonts::Font4);

    _canvas->drawFastHLine(20, 44, 780, COLOR_BORDER);

    // Three Large Cards (y=52, h=256)
    int cW = 250, cH = 256;
    drawCard(20,  52, cW, cH, COLOR_SURFACE, COLOR_BORDER);
    drawCard(285, 52, cW, cH, COLOR_SURFACE, COLOR_BORDER);
    drawCard(550, 52, cW, cH, COLOR_SURFACE, COLOR_BORDER);

    // Card 1 Headers & Labels
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("ENERGY EFFICIENCY", 34, 62, &fonts::Font2);
    _canvas->drawCenterString("Wh / km", 145, 146, &fonts::Font4);

    // Card 2 Headers & Labels
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("PEAK DYNAMICS", 299, 62, &fonts::Font2);
    _canvas->drawCenterString("Peak Power", 410, 134, &fonts::Font2);

    // Card 3 Headers & Labels
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawString("BATTERY HEALTH & TEMPS", 564, 62, &fonts::Font2);
    _canvas->drawCenterString("[Hold BTN2 to Reset]", 675, 284, &fonts::Font2);
}

void DashboardRenderer::renderTripStatsScreen(const DashTelemetry &telemetry) {
    // Top Bar: Ride Time
    uint32_t rTime = telemetry.stats.ride_time_sec;
    int hrs = rTime / 3600;
    int mins = (rTime % 3600) / 60;
    int secs = rTime % 60;
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "MOVING: %02d:%02d:%02d", hrs, mins, secs);
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    _canvas->setTextPadding(240);
    _canvas->drawRightString(timeStr, 800, 14, &fonts::Font4);
    _canvas->setTextPadding(0);

    // Card 1: Energy & Efficiency
    char effStr[16];
    if (telemetry.stats.trip_wh_km > 0.0f) {
        snprintf(effStr, sizeof(effStr), "%.1f", telemetry.stats.trip_wh_km);
    } else {
        snprintf(effStr, sizeof(effStr), "--.-");
    }
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(200);
    _canvas->drawCenterString(effStr, 145, 80, &fonts::Font8);
    _canvas->setTextPadding(0);

    char buf[32];
    snprintf(buf, sizeof(buf), "Trip Wh: %.0f Wh", telemetry.stats.trip_wh);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 34, 178, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Charge: %.2f Ah", telemetry.amphours_used);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 34, 208, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Pack Wh: %.0f Wh", telemetry.remaining_wh);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 34, 238, &fonts::Font4);

    snprintf(buf, sizeof(buf), "EST Range: %d km", (int)roundf(telemetry.est_range_km));
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 34, 268, &fonts::Font4);
    _canvas->setTextPadding(0);

    // Card 2: Peak Dynamics
    char pStr[24];
    if (telemetry.stats.peak_power_watts >= 1000.0f) {
        snprintf(pStr, sizeof(pStr), "%.2f kW", telemetry.stats.peak_power_watts / 1000.0f);
    } else {
        snprintf(pStr, sizeof(pStr), "%.0f W", telemetry.stats.peak_power_watts);
    }
    _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawCenterString(pStr, 410, 86, &fonts::Font6);
    _canvas->setTextPadding(0);

    snprintf(buf, sizeof(buf), "Peak Batt: %.1f A", telemetry.stats.peak_current_amps);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 299, 160, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak Phase: %.1f A", telemetry.stats.peak_phase_amps);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 299, 190, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Max Speed: %.1f km/h", telemetry.stats.max_speed_kmh);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 299, 220, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Avg Speed: %.1f km/h", telemetry.stats.avg_speed_kmh);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 299, 250, &fonts::Font4);
    _canvas->setTextPadding(0);

    // Card 3: Battery Health & Temps
    const char *pName = (telemetry.battery_profile_id == 0) ? "Profile 1 (Fresh)" : "Profile 2 (Daily)";
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(pName, 564, 86, &fonts::Font4);

    snprintf(buf, sizeof(buf), "SoH: %.1f%%", telemetry.battery_health_soh);
    _canvas->setTextColor(COLOR_GREEN, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 564, 116, &fonts::Font6);

    const BatteryProfile &prof = Battery.getProfile(telemetry.battery_profile_id);
    snprintf(buf, sizeof(buf), "Learned: %.0f Wh", prof.learned_wh);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 564, 166, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak Mot: %.1f°C", telemetry.stats.max_temp_motor);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 564, 196, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak ESC: %.1f°C", telemetry.stats.max_temp_esc);
    _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 564, 226, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Trip: %.2f km", telemetry.trip_km);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->setTextPadding(220);
    _canvas->drawString(buf, 564, 256, &fonts::Font4);
    _canvas->setTextPadding(0);
}

// ==============================================================================
// SCREEN 2: INTERACTIVE SETTINGS MENU SYSTEM
// ==============================================================================
static const char *ROOT_CATEGORIES[5] = {
    "1. POWER & DRIVE",
    "2. DISPLAY & UI",
    "3. BATTERY PROFILES",
    "4. BIKE SETUP",
    "5. SYSTEM & DIAG"
};

static int getSubItemCount(uint8_t sub_id) {
    switch (sub_id) {
        case 0: return 5;
        case 1: return 2;
        case 2: return 4;
        case 3: return 3;
        case 4: return 3;
        default: return 0;
    }
}

static void getSubItemDetails(uint8_t sub_id, uint8_t item_id,
                              char *title, size_t title_sz,
                              char *val, size_t val_sz,
                              char *desc, size_t desc_sz,
                              const DashTelemetry &telemetry) {
    DashSettings &s = Settings.get();
    title[0] = 0;
    val[0] = 0;
    desc[0] = 0;

    switch (sub_id) {
        case 0: // POWER & DRIVE
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Max Battery Current");
                    snprintf(val, val_sz, "%d A", s.max_battery_amps);
                    snprintf(desc, desc_sz, "Battery pack draw limit. Range: 5A - 35A (step 1A).\nBBS mid-drive safe max: 35A.");
                    break;
                case 1:
                    snprintf(title, title_sz, "Max Phase Current");
                    snprintf(val, val_sz, "%d A", s.max_phase_amps);
                    snprintf(desc, desc_sz, "Motor phase torque limit. Range: 10A - 70A (step 1A).\nBBS mid-drive safe max: 70A.");
                    break;
                case 2:
                    snprintf(title, title_sz, "Field Weakening (FW)");
                    snprintf(val, val_sz, "%d A", s.field_weak_amps);
                    snprintf(desc, desc_sz, "High-RPM top speed boost current. Range: 0A - 20A.\nDuty cycle expands into 100%%-120%% zone.");
                    break;
                case 3:
                    snprintf(title, title_sz, "Speed Limit");
                    if (s.max_speed_kmh == 0) {
                        snprintf(val, val_sz, "OFF (Unlimited)");
                    } else {
                        snprintf(val, val_sz, "%d km/h", s.max_speed_kmh);
                    }
                    snprintf(desc, desc_sz, "Maximum motor assist speed cutoff.\nPresets: OFF, 25, 32, 45, 60 km/h.");
                    break;
                case 4:
                    snprintf(title, title_sz, "Throttle Ramp Time");
                    snprintf(val, val_sz, "%.2f s", s.throttle_ramp_sec);
                    snprintf(desc, desc_sz, "Throttle response filter for smooth takeoff.\nRange: 0.10s to 1.00s.");
                    break;
            }
            break;

        case 1: // DISPLAY & UI
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Dashboard Style");
                    snprintf(val, val_sz, (s.dash_style == 0) ? "Analog Dial" : "Duty Bar");
                    snprintf(desc, desc_sz, "Instrument cluster theme: Left-Hug Analog Dial\nor Horizontal Duty Bar.");
                    break;
                case 1:
                    snprintf(title, title_sz, "Screen Brightness");
                    snprintf(val, val_sz, "%d%%", s.brightness_pct);
                    snprintf(desc, desc_sz, "ST7701 display backlight brightness.\nRange: 20% to 100% (step 5%).");
                    break;
            }
            break;

        case 2: // BATTERY PROFILES
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Active Profile");
                    snprintf(val, val_sz, (s.active_battery_profile == 0) ? "Profile 1 (Fresh)" : "Profile 2 (Daily)");
                    snprintf(desc, desc_sz, "Select between two distinct 52V battery packs\nwith independent learned health and capacity.");
                    break;
                case 1:
                    snprintf(title, title_sz, "Profile 1 Nominal Wh");
                    snprintf(val, val_sz, "%.0f Wh", Battery.getProfile(0).nominal_wh);
                    snprintf(desc, desc_sz, "Rated energy capacity of Battery 1.\nRange: 500Wh to 2500Wh (step 50Wh).");
                    break;
                case 2:
                    snprintf(title, title_sz, "Profile 2 Nominal Wh");
                    snprintf(val, val_sz, "%.0f Wh", Battery.getProfile(1).nominal_wh);
                    snprintf(desc, desc_sz, "Rated energy capacity of Battery 2.\nRange: 500Wh to 2500Wh (step 50Wh).");
                    break;
                case 3:
                    snprintf(title, title_sz, "Reset SoH Learning");
                    snprintf(val, val_sz, "[SELECT TO RESET]");
                    snprintf(desc, desc_sz, "Reset self-learning capacity algorithm\nfor active battery profile.");
                    break;
            }
            break;

        case 3: // BIKE SETUP
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Wheel Diameter");
                    snprintf(val, val_sz, "%.1f in", s.wheel_diameter_in);
                    snprintf(desc, desc_sz, "Tire outer diameter for speed calibration.\nRange: 20.0\" to 29.0\" (step 0.5\").");
                    break;
                case 1:
                    snprintf(title, title_sz, "Motor Pole Pairs");
                    snprintf(val, val_sz, "%d", s.motor_pole_pairs);
                    snprintf(desc, desc_sz, "VESC ERPM to RPM divisor. Range: 8 to 24.\nBafang BBS standard: 15.");
                    break;
                case 2:
                    snprintf(title, title_sz, "Internal Gear Ratio");
                    snprintf(val, val_sz, "%.2f : 1", s.gear_ratio);
                    snprintf(desc, desc_sz, "Reduction gear ratio from rotor to output.\nRange: 1.00 to 4.00.");
                    break;
            }
            break;

        case 4: // SYSTEM & DIAGNOSTICS
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Reset Trip Statistics");
                    snprintf(val, val_sz, "[SELECT TO RESET]");
                    snprintf(desc, desc_sz, "Clears current trip distance, Wh/km efficiency,\nand peak telemetry records.");
                    break;
                case 1:
                    snprintf(title, title_sz, "Factory Reset");
                    snprintf(val, val_sz, "[SELECT TO RESET]");
                    snprintf(desc, desc_sz, "Restores all settings and battery configurations\nto factory defaults.");
                    break;
                case 2:
                    snprintf(title, title_sz, "VESC Hardware Status");
                    snprintf(val, val_sz, telemetry.vesc_connected ? "COMM HEALTHY" : "NO COMM / OFFLINE");
                    snprintf(desc, desc_sz, "Flipsky 75100 V1 UART packet status\n(TX: GPIO 43, RX: GPIO 44, 115200 baud).");
                    break;
            }
            break;
    }
}

void DashboardRenderer::initSettingsScreen(const DashTelemetry &telemetry) {
    // Header Bar
    _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas->drawString("SETTINGS", 20, 14, &fonts::Font4);

    _canvas->drawFastHLine(20, 44, 780, COLOR_BORDER);

    // Left Navigation Box
    drawCard(20, 52, 310, 256, COLOR_SURFACE, COLOR_BORDER);

    // Right Content Box
    drawCard(345, 52, 455, 256, COLOR_SURFACE, COLOR_BORDER);
}

void DashboardRenderer::renderRootMenu() {
    for (int i = 0; i < 5; i++) {
        int iy = 56 + i * 49;
        bool isSel = (i == _menu_root_idx);
        if (isSel) {
            _canvas->fillRoundRect(24, iy, 302, 45, 6, COLOR_SURFACE_LIGHT);
            _canvas->fillRoundRect(24, iy, 6, 45, 2, COLOR_CYAN);
            _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE_LIGHT);
        } else {
            _canvas->fillRoundRect(24, iy, 302, 45, 6, COLOR_SURFACE);
            _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        }
        _canvas->drawString(ROOT_CATEGORIES[i], 38, iy + 10, &fonts::Font4);
    }
}

void DashboardRenderer::renderSubmenu(uint8_t sub_id, const DashTelemetry &telemetry) {
    int count = getSubItemCount(sub_id);
    for (int j = 0; j < count; j++) {
        int iy = 56 + j * 49;
        bool isSel = (j == _menu_sub_idx);

        char title[48], val[32], desc[128];
        getSubItemDetails(sub_id, j, title, sizeof(title), val, sizeof(val), desc, sizeof(desc), telemetry);

        if (isSel) {
            uint16_t bgCol = _menu_edit_mode ? 0x3186 : COLOR_SURFACE_LIGHT;
            uint16_t barCol = _menu_edit_mode ? COLOR_AMBER : COLOR_CYAN;
            _canvas->fillRoundRect(24, iy, 302, 45, 6, bgCol);
            _canvas->fillRoundRect(24, iy, 6, 45, 2, barCol);
            _canvas->setTextColor(COLOR_WHITE, bgCol);
        } else {
            _canvas->fillRoundRect(24, iy, 302, 45, 6, COLOR_SURFACE);
            _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        }
        _canvas->drawString(title, 38, iy + 10, &fonts::Font4);
    }
}

void DashboardRenderer::renderSettingsScreen(const DashTelemetry &telemetry) {
    // 1. Auto-exit edit mode after 2.5s of inactivity
    if (_menu_edit_mode && (millis() - _menu_last_activity_ms > 2500)) {
        _menu_edit_mode = false;
        Buttons.setEditMode(false);
        Settings.save();
        _menu_dirty = true;
    }

    if (!_menu_dirty) return;
    _menu_dirty = false;

    // 2. Clear inner card contents
    _canvas->fillRect(22, 54, 306, 252, COLOR_SURFACE);
    _canvas->fillRect(347, 54, 451, 252, COLOR_SURFACE);

    // 3. Render Left Pane
    if (!_menu_in_sub) {
        renderRootMenu();
    } else {
        renderSubmenu(_menu_root_idx, telemetry);
    }

    // 4. Render Right Pane
    char curTitle[48] = {0};
    if (!_menu_in_sub) {
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->drawString(ROOT_CATEGORIES[_menu_root_idx], 365, 68, &fonts::Font6);

        const char *descs[5] = {
            "Configure battery current limit (max 35A),\nphase current (max 70A), field weakening (20A),\nspeed limit, and throttle ramp.",
            "Select primary cluster: Left-Hug Analog Dial\nor Horizontal Duty Bar. Set ST7701 backlight.",
            "Switch between 52V battery packs (Fresh/Daily).\nSet nominal capacity and view self-learned health.",
            "Calibrate wheel diameter, motor pole pairs,\nand gear ratio for accurate speed calculation.",
            "Reset trip distance and peak statistics,\nrestore factory defaults, or check VESC link."
        };
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString(descs[_menu_root_idx], 365, 126, &fonts::Font4);

        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->drawString("Press [BTN2] to Enter Submenu", 365, 230, &fonts::Font4);
        _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
        _canvas->drawString("Hold [BTN1] to Return to Ride Dashboard", 365, 268, &fonts::Font2);
    } else {
        char val[32], desc[128];
        getSubItemDetails(_menu_root_idx, _menu_sub_idx, curTitle, sizeof(curTitle), val, sizeof(val), desc, sizeof(desc), telemetry);

        // Setting Title
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString(curTitle, 365, 66, &fonts::Font4);

        // Big Value Box
        int boxY = 100, boxH = 74;
        uint16_t vBg = _menu_edit_mode ? 0x2124 : COLOR_SURFACE;
        uint16_t vBorder = _menu_edit_mode ? COLOR_AMBER : COLOR_BORDER;
        uint16_t vColor = _menu_edit_mode ? COLOR_AMBER : COLOR_WHITE;

        _canvas->fillRoundRect(365, boxY, 415, boxH, 8, vBg);
        _canvas->drawRoundRect(365, boxY, 415, boxH, 8, vBorder);

        _canvas->setTextColor(vColor, vBg);
        _canvas->drawCenterString(val, 365 + (415 / 2), boxY + 14, &fonts::Font6);

        // Description
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString(desc, 365, 188, &fonts::Font2);

        // Help Hints
        if (_menu_edit_mode) {
            _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
            _canvas->drawString("[-] BTN1    [+] BTN2    (Hold accelerates)", 365, 268, &fonts::Font4);
        } else {
            _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
            _canvas->drawString("[BTN1] Next    [BTN2] Edit/Select    [Hold 1] Back", 365, 268, &fonts::Font2);
        }
    }

    // 5. Header Breadcrumb
    char topBuf[64];
    if (_menu_edit_mode) {
        snprintf(topBuf, sizeof(topBuf), "EDITING VALUE - AUTO SAVES IN 2.5s");
        _canvas->setTextColor(COLOR_AMBER, COLOR_BG);
    } else if (_menu_in_sub) {
        snprintf(topBuf, sizeof(topBuf), "%s > %s", ROOT_CATEGORIES[_menu_root_idx] + 3, curTitle);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    } else {
        snprintf(topBuf, sizeof(topBuf), "SETTINGS > ROOT MENU");
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
    }
    _canvas->setTextPadding(450);
    _canvas->drawRightString(topBuf, 800, 14, &fonts::Font4);
    _canvas->setTextPadding(0);
}

void DashboardRenderer::adjustCurrentSetting(int direction, DashTelemetry &telemetry) {
    DashSettings &s = Settings.get();
    _menu_last_activity_ms = millis();
    _menu_dirty = true;

    switch (_menu_root_idx) {
        case 0: // POWER & DRIVE
            if (_menu_sub_idx == 0) { // max battery amps (5..35)
                int v = (int)s.max_battery_amps + direction;
                if (v < 5) v = 5;
                if (v > 35) v = 35;
                s.max_battery_amps = v;
            } else if (_menu_sub_idx == 1) { // max phase amps (10..70)
                int v = (int)s.max_phase_amps + direction;
                if (v < 10) v = 10;
                if (v > 70) v = 70;
                s.max_phase_amps = v;
            } else if (_menu_sub_idx == 2) { // field weakening (0..20)
                int v = (int)s.field_weak_amps + direction;
                if (v < 0) v = 0;
                if (v > 20) v = 20;
                s.field_weak_amps = v;
            } else if (_menu_sub_idx == 3) { // speed limit presets: 0, 25, 32, 45, 60
                static const uint8_t presets[] = { 0, 25, 32, 45, 60 };
                int idx = 0;
                for (int i = 0; i < 5; i++) {
                    if (s.max_speed_kmh == presets[i]) { idx = i; break; }
                }
                idx = (idx + direction + 5) % 5;
                s.max_speed_kmh = presets[idx];
            } else if (_menu_sub_idx == 4) { // ramp time (0.10..1.00s)
                float v = s.throttle_ramp_sec + (direction * 0.05f);
                if (v < 0.10f) v = 0.10f;
                if (v > 1.00f) v = 1.00f;
                s.throttle_ramp_sec = v;
            }
            break;

        case 1: // DISPLAY & UI
            if (_menu_sub_idx == 0) { // dash style
                s.dash_style = (s.dash_style == 0) ? 1 : 0;
                _active_style = (DashboardStyle)s.dash_style;
            } else if (_menu_sub_idx == 1) { // brightness (20..100)
                int b = (int)s.brightness_pct + (direction * 5);
                if (b < 20) b = 20;
                if (b > 100) b = 100;
                s.brightness_pct = b;
                _display->setBrightness(map(s.brightness_pct, 0, 100, 0, 255));
            }
            break;

        case 2: // BATTERY PROFILES
            if (_menu_sub_idx == 0) { // active profile
                s.active_battery_profile = (s.active_battery_profile == 0) ? 1 : 0;
                Battery.setActiveProfile(s.active_battery_profile);
            } else if (_menu_sub_idx == 1) { // B1 nominal Wh
                float wh = Battery.getProfile(0).nominal_wh + (direction * 50.0f);
                if (wh < 500.0f) wh = 500.0f;
                if (wh > 2500.0f) wh = 2500.0f;
                Battery.getProfile(0).nominal_wh = wh;
                Battery.saveProfiles();
            } else if (_menu_sub_idx == 2) { // B2 nominal Wh
                float wh = Battery.getProfile(1).nominal_wh + (direction * 50.0f);
                if (wh < 500.0f) wh = 500.0f;
                if (wh > 2500.0f) wh = 2500.0f;
                Battery.getProfile(1).nominal_wh = wh;
                Battery.saveProfiles();
            }
            break;

        case 3: // BIKE SETUP
            if (_menu_sub_idx == 0) { // wheel diameter (20..29 in)
                float d = s.wheel_diameter_in + (direction * 0.5f);
                if (d < 20.0f) d = 20.0f;
                if (d > 29.0f) d = 29.0f;
                s.wheel_diameter_in = d;
            } else if (_menu_sub_idx == 1) { // pole pairs (8..24)
                int p = (int)s.motor_pole_pairs + direction;
                if (p < 8) p = 8;
                if (p > 24) p = 24;
                s.motor_pole_pairs = p;
            } else if (_menu_sub_idx == 2) { // gear ratio (1.00..4.00)
                float g = s.gear_ratio + (direction * 0.05f);
                if (g < 1.00f) g = 1.00f;
                if (g > 4.00f) g = 4.00f;
                s.gear_ratio = g;
            }
            break;

        default:
            break;
    }
}

void DashboardRenderer::handleMenuNav(NavAction action, DashTelemetry &telemetry) {
    if (action == NAV_NONE) return;

    if (_menu_edit_mode) {
        _menu_last_activity_ms = millis();
        if (action == NAV_EDIT_DEC || action == NAV_BTN1_SHORT) {
            adjustCurrentSetting(-1, telemetry);
        } else if (action == NAV_EDIT_INC || action == NAV_BTN2_SHORT) {
            adjustCurrentSetting(+1, telemetry);
        } else if (action == NAV_BTN1_LONG || action == NAV_BTN2_LONG) {
            _menu_edit_mode = false;
            Buttons.setEditMode(false);
            Settings.save();
            _menu_dirty = true;
        }
        return;
    }

    // Normal menu navigation mode
    if (_menu_in_sub) {
        if (action == NAV_BTN1_SHORT) {
            int total = getSubItemCount(_menu_root_idx);
            if (total > 0) {
                _menu_sub_idx = (_menu_sub_idx + 1) % total;
                _menu_dirty = true;
            }
        } else if (action == NAV_BTN2_SHORT) {
            if (_menu_root_idx == 2 && _menu_sub_idx == 3) {
                // Reset Learning
                Battery.resetProfileLearning(Settings.get().active_battery_profile);
                _menu_dirty = true;
            } else if (_menu_root_idx == 4 && _menu_sub_idx == 0) {
                // Reset Trip Stats
                telemetry.trip_km = 0.0f;
                telemetry.stats.reset();
                _menu_dirty = true;
            } else if (_menu_root_idx == 4 && _menu_sub_idx == 1) {
                // Restore Factory Defaults
                Settings.resetToDefaults();
                _display->setBrightness(map(Settings.get().brightness_pct, 0, 100, 0, 255));
                _menu_dirty = true;
            } else if (_menu_root_idx == 4 && _menu_sub_idx == 2) {
                // VESC Status is info-only
            } else {
                _menu_edit_mode = true;
                Buttons.setEditMode(true);
                _menu_last_activity_ms = millis();
                _menu_dirty = true;
            }
        } else if (action == NAV_BTN1_LONG) {
            _menu_in_sub = false;
            _menu_dirty = true;
        }
    } else {
        // In Root Menu
        if (action == NAV_BTN1_SHORT) {
            _menu_root_idx = (_menu_root_idx + 1) % 5;
            _menu_dirty = true;
        } else if (action == NAV_BTN2_SHORT) {
            _menu_in_sub = true;
            _menu_sub_idx = 0;
            _menu_dirty = true;
        } else if (action == NAV_BTN1_LONG) {
            telemetry.screen = SCREEN_RIDE_DASH;
            _screen_dirty = true;
            _cache.invalidate();
        }
    }
}

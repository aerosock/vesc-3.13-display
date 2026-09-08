#include "dash_renderer.h"
#include "buttons.h"
#include <math.h>

DashboardRenderer Renderer;

// Modern High-Contrast Color Palette (Optimized for ST7701 3.16" LCD)
static const uint16_t COLOR_BG            = 0x0821; // Deep Obsidian Black
static const uint16_t COLOR_SURFACE       = 0x10A2; // Card Background
static const uint16_t COLOR_SURFACE_LIGHT = 0x2124; // Card Highlight / Active Item
static const uint16_t COLOR_BORDER        = 0x39E7; // Crisp Border
static const uint16_t COLOR_WHITE         = 0xFFFF; // Pure White
static const uint16_t COLOR_LIGHT_GRAY    = 0xDEFB; // Crisp Bright Silver (High Visibility)
static const uint16_t COLOR_MUTED_GRAY    = 0x9CD3; // Medium Silver-Gray
static const uint16_t COLOR_AMBER         = 0xFD20; // High Power / Warning
static const uint16_t COLOR_RED           = 0xF800; // Redline / Field Weakening / Alarm
static const uint16_t COLOR_GREEN         = 0x07E0; // Connected / Safe / Efficient
static const uint16_t COLOR_CYAN          = 0x07FF; // Electric Accent / Profile
static const uint16_t COLOR_NEEDLE        = 0xFC00; // High-Visibility Sport Orange-Red

// Dynamic Color Helpers
static inline uint16_t getPowerColor(float watts) {
    if (watts >= 1500.0f) return COLOR_RED;
    if (watts >= 750.0f)  return COLOR_AMBER;
    return COLOR_CYAN;
}

static inline uint16_t getPhaseColor(float amps) {
    if (amps >= 55.0f) return COLOR_RED;
    if (amps >= 30.0f) return COLOR_AMBER;
    return COLOR_WHITE;
}

static inline uint16_t getBatteryColor(float pct) {
    if (pct < 20.0f) return COLOR_RED;
    if (pct < 40.0f) return COLOR_AMBER;
    return COLOR_GREEN;
}

static inline uint16_t getDutyColor(float duty) {
    if (duty >= 100.0f) return COLOR_RED;
    if (duty >= 75.0f)  return COLOR_AMBER;
    return COLOR_CYAN;
}

static inline uint16_t getTempColor(float temp) {
    if (temp >= 80.0f) return COLOR_RED;
    if (temp >= 60.0f) return COLOR_AMBER;
    return COLOR_WHITE;
}

static inline uint16_t getSpeedColor(float spd) {
    if (spd >= 45.0f) return COLOR_AMBER;
    if (spd >= 25.0f) return COLOR_CYAN;
    return COLOR_WHITE;
}

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
    _metric_sprite.setColorDepth(16);
    _metric_sprite.setPsram(false); // Allocate in internal SRAM for fast push
    _metric_sprite.createSprite(220, 52);
    _cache.invalidate();
    _screen_dirty = true;
    _active_style = (DashboardStyle)Settings.get().dash_style;
}

void DashboardRenderer::drawCard(int x, int y, int w, int h, uint16_t bg, uint16_t border) {
    _canvas->fillRoundRect(x, y, w, h, 8, bg);
    _canvas->drawRoundRect(x, y, w, h, 8, border);
}

// Zero-flicker metric rendering using off-screen SRAM sprite
void DashboardRenderer::drawValWithUnit(int x, int y, int w, int h, const char *numStr, const char *unitStr, uint16_t numCol, uint16_t unitCol, uint16_t bgCol) {
    if (w > 220) w = 220;
    if (h > 52) h = 52;
    _metric_sprite.fillSprite(bgCol);
    _metric_sprite.setTextColor(numCol, bgCol);
    _metric_sprite.drawString(numStr, 0, 0, &fonts::Font6);
    int nw = _metric_sprite.textWidth(numStr, &fonts::Font6);
    _metric_sprite.setTextColor(unitCol, bgCol);
    _metric_sprite.drawString(unitStr, nw + 6, 16, &fonts::Font4);
    _metric_sprite.pushSprite(x, y);
}

void DashboardRenderer::triggerNeedleSweep() {
    _is_sweeping = true;
    _sweep_phase = 0;
    _sweep_progress = 0.0f;
    _sweep_duty = 0.0f;
    _last_sweep_tick_ms = millis();
}

// Top-Level Screen Dispatcher
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

        case SCREEN_ENERGY_STATS:
            if (_screen_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initEnergyStatsScreen(telemetry);
                _screen_dirty = false;
            }
            renderEnergyStatsScreen(telemetry);
            break;

        case SCREEN_PERF_STATS:
            if (_screen_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initPerfStatsScreen(telemetry);
                _screen_dirty = false;
            }
            renderPerfStatsScreen(telemetry);
            break;

        case SCREEN_SETTINGS_MENU:
            if (_screen_dirty) {
                _canvas->fillScreen(COLOR_BG);
                initSettingsScreen(telemetry);
                _screen_dirty = false;
                _menu_dirty = true;
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
// STYLE A: LEFT-HUG ANALOG DUTY CLUSTER
// ==============================================================================
// Center: cx = 45, cy = 160. Hub radius: 38.
// Digital duty readout is mounted INSIDE the hub circle!
// Needle radiates from outer rim of the hub (r=38) outward to r=145.
// Zero clipping between needle and hub! Zero overlap with top ticks!
// Full vertical screen usage: bottom strip moved down to y=282 (no 4-5mm dead space).
// ==============================================================================
void DashboardRenderer::initLeftHugAnalogStyle(const DashTelemetry &telemetry) {
    int cx = 45, cy = 160;

    // 1. Draw Gauge Bezel Arc (Convex arc to the right)
    for (int i = 0; i < 24; i++) {
        float frac1 = (float)i / 24.0f;
        float frac2 = (float)(i + 1) / 24.0f;
        float ang1 = 1.10f - (frac1 * 2.20f);
        float ang2 = 1.10f - (frac2 * 2.20f);

        int ax1 = cx + (int)(cosf(ang1) * 175);
        int ay1 = cy + (int)(sinf(ang1) * 175);
        int ax2 = cx + (int)(cosf(ang2) * 175);
        int ay2 = cy + (int)(sinf(ang2) * 175);

        uint16_t arcCol = (i >= 20) ? COLOR_RED : COLOR_BORDER;
        _canvas->drawLine(ax1, ay1, ax2, ay2, arcCol);
        _canvas->drawLine(ax1 + 1, ay1, ax2 + 1, ay2, arcCol);
    }

    // 2. Draw Ticks & Prominent Font4 Scale Numbers
    for (int i = 0; i <= 24; i++) {
        float frac = (float)i / 24.0f;
        float ang = 1.10f - (frac * 2.20f);
        bool isMajor = (i % 4 == 0); // 0, 20, 40, 60, 80, 100, 120
        int tickLen = isMajor ? 16 : 8;

        int x1 = cx + (int)(cosf(ang) * 175);
        int y1 = cy + (int)(sinf(ang) * 175);
        int x2 = cx + (int)(cosf(ang) * (175 - tickLen));
        int y2 = cy + (int)(sinf(ang) * (175 - tickLen));

        uint16_t tColor = (i >= 20) ? COLOR_RED : ((i >= 16) ? COLOR_AMBER : COLOR_WHITE);
        _canvas->drawLine(x1, y1, x2, y2, tColor);
        if (isMajor) {
            _canvas->drawLine(x1, y1 + 1, x2, y2 + 1, tColor);

            int duty_val = i * 5;
            int numX = cx + (int)(cosf(ang) * 136);
            int numY = cy + (int)(sinf(ang) * 136);

            if (duty_val == 120) {
                _canvas->setTextColor(COLOR_RED, COLOR_BG);
                _canvas->drawCenterString("FW", numX, numY - 12, &fonts::Font4);
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", duty_val);
                _canvas->setTextColor(tColor, COLOR_BG);
                _canvas->drawCenterString(buf, numX, numY - 12, &fonts::Font4);
            }
        }
    }

    // 3. Central Hub Circle (Contains digital duty readout inside)
    _canvas->fillCircle(cx, cy, 38, COLOR_SURFACE);
    _canvas->drawCircle(cx, cy, 38, COLOR_BORDER);
    _canvas->drawCircle(cx, cy, 37, COLOR_BORDER);
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_SURFACE);
    _canvas->drawCenterString("DUTY", cx, cy - 18, &fonts::Font2);

    // 4. Right Cockpit - Top Ribbon
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
    _canvas->drawString(pName, 245, 12, &fonts::Font4);

    _canvas->drawFastHLine(235, 38, 575, COLOR_BORDER);

    // 5. Right Cockpit - Mid-Deck Static Labels (Optimized vertical positions)
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_BG);
    _canvas->drawString("POWER", 245, 48, &fonts::Font4);
    _canvas->drawString("BATTERY", 660, 48, &fonts::Font4);

    _canvas->drawCenterString("KM/H", 525, 154, &fonts::Font4);

    _canvas->drawString("PHASE", 245, 146, &fonts::Font4);
    _canvas->drawString("ENERGY", 660, 146, &fonts::Font4);

    // 6. Right Cockpit - Bottom Strip Divider (Moved down to y=270, eliminating 4-5mm dead space)
    _canvas->drawFastHLine(235, 270, 575, COLOR_BORDER);
}

void DashboardRenderer::drawAnalogNeedle(int cx, int cy, int length, float angle_rad, uint16_t color) {
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    float perp = angle_rad + (M_PI / 2.0f);
    float cos_p = cosf(perp);
    float sin_p = sinf(perp);

    // Needle starts at outer rim of hub circle (r=38) and extends to length
    int b1X = cx + (int)(cos_a * 38 + cos_p * 4);
    int b1Y = cy + (int)(sin_a * 38 + sin_p * 4);
    int b2X = cx + (int)(cos_a * 38 - cos_p * 4);
    int b2Y = cy + (int)(sin_a * 38 - sin_p * 4);
    int tipX = cx + (int)(cos_a * length);
    int tipY = cy + (int)(sin_a * length);

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
    int cx = 45, cy = 160;
    float currentDuty = _is_sweeping ? _sweep_duty : telemetry.duty_cycle_pct;
    if (currentDuty < 0.0f) currentDuty = 0.0f;
    if (currentDuty > 120.0f) currentDuty = 120.0f;

    // 1. Erase Previous Needle (Zero-Flicker Differential Triangle outside hub)
    if (_last_needle.valid) {
        _canvas->fillTriangle(_last_needle.tipX, _last_needle.tipY,
                              _last_needle.b1X, _last_needle.b1Y,
                              _last_needle.b2X, _last_needle.b2Y,
                              COLOR_BG);
        _canvas->drawLine(_last_needle.b1X, _last_needle.b1Y, _last_needle.tipX, _last_needle.tipY, COLOR_BG);
        _canvas->drawLine(_last_needle.b2X, _last_needle.b2Y, _last_needle.tipX, _last_needle.tipY, COLOR_BG);
    }

    // 2. Draw Needle: Angle +63° (bottom) to -63° (top)
    float frac = currentDuty / 120.0f;
    float needle_angle = 1.10f - (frac * 2.20f);

    drawAnalogNeedle(cx, cy, 145, needle_angle, COLOR_NEEDLE);

    // 3. Digital Duty Cycle Readout Mounted Inside Central Hub Circle
    int duty_int = (int)roundf(currentDuty);
    if (duty_int != _cache.duty_x10) {
        _cache.duty_x10 = duty_int;
        char dutyBuf[16];
        snprintf(dutyBuf, sizeof(dutyBuf), "%d%%", duty_int);
        uint16_t dCol = getDutyColor(currentDuty);
        _canvas->setTextColor(dCol, COLOR_SURFACE);
        _canvas->setTextPadding(58);
        _canvas->drawCenterString(dutyBuf, cx, cy - 2, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 4. Top Ribbon
    if (telemetry.battery_profile_id != _cache.batt_prof) {
        _cache.batt_prof = telemetry.battery_profile_id;
        const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(140);
        _canvas->drawString(pName, 245, 12, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    if (curVoltX10 != _cache.voltage_x10) {
        _cache.voltage_x10 = curVoltX10;
        char vStr[20];
        snprintf(vStr, sizeof(vStr), "%.1f V", telemetry.voltage);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(130);
        _canvas->drawCenterString(vStr, 525, 12, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    if (telemetry.vesc_connected != _cache.vesc_connected) {
        _cache.vesc_connected = telemetry.vesc_connected;
        _canvas->setTextColor(telemetry.vesc_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        _canvas->setTextPadding(110);
        _canvas->drawRightString(telemetry.vesc_connected ? "● VESC" : "○ NO VESC", 805, 12, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 5. Giant Speedometer (Font8, 75px)
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        char spdStr[8];
        snprintf(spdStr, sizeof(spdStr), "%d", curSpeed);
        _canvas->setTextColor(getSpeedColor(telemetry.speed_kmh), COLOR_BG);
        _canvas->setTextPadding(180);
        _canvas->drawCenterString(spdStr, 525, 72, &fonts::Font8);
        _canvas->setTextPadding(0);
    }

    // Range Estimate below KM/H (280px padding prevents "E" from being clipped)
    int curRange = (int)roundf(telemetry.est_range_km);
    if (curRange != _cache.est_range) {
        _cache.est_range = curRange;
        char rStr[32];
        snprintf(rStr, sizeof(rStr), "EST RANGE: %d km", curRange);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(280);
        _canvas->drawCenterString(rStr, 525, 198, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 6. Left Flank: Power & Phase Amps (Zero-flicker sprite with dynamic color)
    int curWatts = (int)roundf(telemetry.power_watts);
    if (curWatts != _cache.watts) {
        _cache.watts = curWatts;
        char numStr[16];
        uint16_t pCol = getPowerColor(telemetry.power_watts);
        if (curWatts >= 1000) {
            snprintf(numStr, sizeof(numStr), "%.2f", curWatts / 1000.0f);
            drawValWithUnit(245, 74, 180, 48, numStr, "kW", pCol, COLOR_LIGHT_GRAY);
        } else {
            snprintf(numStr, sizeof(numStr), "%d", curWatts);
            drawValWithUnit(245, 74, 180, 48, numStr, "W", pCol, COLOR_LIGHT_GRAY);
        }
    }

    int curPhaseX10 = (int)roundf(telemetry.phase_amps * 10.0f);
    if (curPhaseX10 != _cache.phase_x10) {
        _cache.phase_x10 = curPhaseX10;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%.0f", telemetry.phase_amps);
        uint16_t phCol = getPhaseColor(telemetry.phase_amps);
        drawValWithUnit(245, 172, 180, 48, numStr, "A", phCol, COLOR_LIGHT_GRAY);
    }

    // 7. Right Flank: Battery % and Remaining Wh (Zero-flicker sprite with dynamic color)
    int curBat = (int)roundf(telemetry.battery_pct);
    if (curBat != _cache.battery_pct) {
        _cache.battery_pct = curBat;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%d", curBat);
        uint16_t bCol = getBatteryColor(telemetry.battery_pct);
        drawValWithUnit(660, 74, 150, 48, numStr, "%", bCol, bCol);
    }

    int curRemWh = (int)roundf(telemetry.remaining_wh);
    if (curRemWh != _cache.remaining_wh) {
        _cache.remaining_wh = curRemWh;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%d", curRemWh);
        drawValWithUnit(660, 172, 150, 48, numStr, "Wh", COLOR_WHITE, COLOR_LIGHT_GRAY);
    }

    // 8. Bottom Strip: Trip, Clean Thermals, Battery SoH (Positioned at y=282)
    int curTripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (curTripX10 != _cache.trip_x10) {
        _cache.trip_x10 = curTripX10;
        char trStr[24];
        snprintf(trStr, sizeof(trStr), "TRIP  %.1f km", telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(180);
        _canvas->drawString(trStr, 245, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int mTemp = (int)roundf(telemetry.temp_motor);
    int eTemp = (int)roundf(telemetry.temp_esc);
    if (mTemp != _cache.temp_motor || eTemp != _cache.temp_esc) {
        _cache.temp_motor = mTemp;
        _cache.temp_esc = eTemp;
        char tStr[36];
        snprintf(tStr, sizeof(tStr), "MOT %d C   ESC %d C", mTemp, eTemp);
        _canvas->setTextColor(getTempColor(fmaxf(telemetry.temp_motor, telemetry.temp_esc)), COLOR_BG);
        _canvas->setTextPadding(260);
        _canvas->drawCenterString(tStr, 525, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    char sohStr[20];
    snprintf(sohStr, sizeof(sohStr), "SoH  %.0f%%", telemetry.battery_health_soh);
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    _canvas->setTextPadding(140);
    _canvas->drawRightString(sohStr, 805, 282, &fonts::Font4);
    _canvas->setTextPadding(0);
}

// ==============================================================================
// STYLE B: HORIZONTAL DUTY BAR CLUSTER
// ==============================================================================
// Ticks: 0%, 20%, 40%, 60%, 80%, 100% (x=670), and FW (x=800) -> NO OVERLAP!
// Full vertical screen usage: bottom strip moved down to y=282 (no 4-5mm dead space).
// ==============================================================================
void DashboardRenderer::initHorizontalBarStyle(const DashTelemetry &telemetry) {
    // 1. Horizontal Duty Bar Track placed right at the top (x=20, y=8, w=780, h=18)
    int bx = 20, by = 8, bw = 780, bh = 18;
    _canvas->fillRoundRect(bx, by, bw, bh, 4, COLOR_SURFACE);
    _canvas->drawRoundRect(bx, by, bw, bh, 4, COLOR_BORDER);

    // Field Weakening Redline Marker at 100% duty (x=670)
    int fw_mark = bx + 650;
    _canvas->drawFastVLine(fw_mark, by, bh, COLOR_RED);

    // Scale Ticks & Font4 Labels at y=28 (100% at x=670, FW at x=800 -> 130px apart!)
    for (int i = 0; i <= 100; i += 20) {
        int tx = bx + (int)((float)i / 120.0f * (float)bw);
        _canvas->drawFastVLine(tx, by + bh, 3, (i == 100) ? COLOR_RED : COLOR_MUTED_GRAY);
        char sbuf[8];
        snprintf(sbuf, sizeof(sbuf), "%d%%", i);
        _canvas->setTextColor((i == 100) ? COLOR_RED : COLOR_MUTED_GRAY, COLOR_BG);
        _canvas->drawCenterString(sbuf, tx, by + bh + 3, &fonts::Font4);
    }
    // FW Label placed at the far right end (120% duty mark)
    _canvas->drawFastVLine(bx + bw, by + bh, 3, COLOR_RED);
    _canvas->setTextColor(COLOR_RED, COLOR_BG);
    _canvas->drawRightString("FW", bx + bw, by + bh + 3, &fonts::Font4);

    // 2. Mid-Deck Static Labels
    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_BG);
    _canvas->drawString("POWER", 30, 56, &fonts::Font4);
    _canvas->drawString("BATTERY", 660, 56, &fonts::Font4);

    _canvas->drawCenterString("KM/H", 410, 154, &fonts::Font4);

    _canvas->drawString("PHASE", 30, 152, &fonts::Font4);
    _canvas->drawString("ENERGY", 660, 152, &fonts::Font4);

    // 3. Bottom Divider (Moved down to y=270)
    _canvas->drawFastHLine(20, 270, 780, COLOR_BORDER);

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
    // 1. Top Duty Cycle Bar (x=20, y=8, w=780, h=18)
    float currentDuty = _is_sweeping ? _sweep_duty : telemetry.duty_cycle_pct;
    drawPreciseDutyBar(20, 8, 780, 18, currentDuty, 120.0f);

    // 2. Giant Speedometer (Font8, 75px, at y=72)
    int curSpeed = (int)roundf(telemetry.speed_kmh);
    if (curSpeed != _cache.speed) {
        _cache.speed = curSpeed;
        char spdStr[8];
        snprintf(spdStr, sizeof(spdStr), "%d", curSpeed);
        _canvas->setTextColor(getSpeedColor(telemetry.speed_kmh), COLOR_BG);
        _canvas->setTextPadding(180);
        _canvas->drawCenterString(spdStr, 410, 72, &fonts::Font8);
        _canvas->setTextPadding(0);
    }

    int curRange = (int)roundf(telemetry.est_range_km);
    if (curRange != _cache.est_range) {
        _cache.est_range = curRange;
        char rStr[32];
        snprintf(rStr, sizeof(rStr), "EST RANGE: %d km", curRange);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(280);
        _canvas->drawCenterString(rStr, 410, 198, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // 3. Left Flank: Power & Phase Current (Zero-flicker sprite with dynamic color)
    int curWatts = (int)roundf(telemetry.power_watts);
    if (curWatts != _cache.watts) {
        _cache.watts = curWatts;
        char numStr[16];
        uint16_t pCol = getPowerColor(telemetry.power_watts);
        if (curWatts >= 1000) {
            snprintf(numStr, sizeof(numStr), "%.2f", curWatts / 1000.0f);
            drawValWithUnit(30, 80, 180, 48, numStr, "kW", pCol, COLOR_LIGHT_GRAY);
        } else {
            snprintf(numStr, sizeof(numStr), "%d", curWatts);
            drawValWithUnit(30, 80, 180, 48, numStr, "W", pCol, COLOR_LIGHT_GRAY);
        }
    }

    int curPhaseX10 = (int)roundf(telemetry.phase_amps * 10.0f);
    if (curPhaseX10 != _cache.phase_x10) {
        _cache.phase_x10 = curPhaseX10;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%.0f", telemetry.phase_amps);
        uint16_t phCol = getPhaseColor(telemetry.phase_amps);
        drawValWithUnit(30, 176, 180, 48, numStr, "A", phCol, COLOR_LIGHT_GRAY);
    }

    // 4. Right Flank: Battery % & Remaining Wh (Zero-flicker sprite with dynamic color)
    int curBat = (int)roundf(telemetry.battery_pct);
    if (curBat != _cache.battery_pct) {
        _cache.battery_pct = curBat;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%d", curBat);
        uint16_t bCol = getBatteryColor(telemetry.battery_pct);
        drawValWithUnit(660, 80, 150, 48, numStr, "%", bCol, bCol);
    }

    int curRemWh = (int)roundf(telemetry.remaining_wh);
    if (curRemWh != _cache.remaining_wh) {
        _cache.remaining_wh = curRemWh;
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%d", curRemWh);
        drawValWithUnit(660, 176, 150, 48, numStr, "Wh", COLOR_WHITE, COLOR_LIGHT_GRAY);
    }

    // 5. Bottom Status Strip (Positioned at y=282, utilizing full 320px height)
    int curTripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (curTripX10 != _cache.trip_x10) {
        _cache.trip_x10 = curTripX10;
        char trStr[24];
        snprintf(trStr, sizeof(trStr), "TRIP %.1f km", telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(160);
        _canvas->drawString(trStr, 25, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    if (telemetry.battery_profile_id != _cache.batt_prof) {
        _cache.batt_prof = telemetry.battery_profile_id;
        const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(140);
        _canvas->drawString(pName, 215, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    if (curVoltX10 != _cache.voltage_x10) {
        _cache.voltage_x10 = curVoltX10;
        char vStr[20];
        snprintf(vStr, sizeof(vStr), "%.1f V", telemetry.voltage);
        _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas->setTextPadding(110);
        _canvas->drawCenterString(vStr, 410, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int mTemp = (int)roundf(telemetry.temp_motor);
    int eTemp = (int)roundf(telemetry.temp_esc);
    if (mTemp != _cache.temp_motor || eTemp != _cache.temp_esc) {
        _cache.temp_motor = mTemp;
        _cache.temp_esc = eTemp;
        char tStr[36];
        snprintf(tStr, sizeof(tStr), "M %d C  E %d C", mTemp, eTemp);
        _canvas->setTextColor(getTempColor(fmaxf(telemetry.temp_motor, telemetry.temp_esc)), COLOR_BG);
        _canvas->setTextPadding(190);
        _canvas->drawString(tStr, 535, 282, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    char sohStr[20];
    snprintf(sohStr, sizeof(sohStr), "SoH %.0f%%", telemetry.battery_health_soh);
    _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    _canvas->setTextPadding(120);
    _canvas->drawRightString(sohStr, 800, 282, &fonts::Font4);
    _canvas->setTextPadding(0);
}

// ==============================================================================
// SCREEN 1: ENERGY & BATTERY ANALYTICS DASHBOARD
// ==============================================================================
void DashboardRenderer::initEnergyStatsScreen(const DashTelemetry &telemetry) {
    // Header Bar (Short clean title with [1/2], zero text collision)
    _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas->drawString("1. ENERGY & BATTERY", 25, 14, &fonts::Font4);

    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_BG);
    _canvas->drawString("[1/2]", 310, 14, &fonts::Font4);

    _canvas->drawFastHLine(20, 42, 780, COLOR_BORDER);

    // Two Large, Wide Cards (w=375, h=255)
    drawCard(25,  50, 375, 255, COLOR_SURFACE, COLOR_BORDER);
    drawCard(420, 50, 375, 255, COLOR_SURFACE, COLOR_BORDER);

    // Card 1 Static Labels
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawString("CONSUMPTION & EFFICIENCY", 40, 60, &fonts::Font4);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->drawString("AVERAGE EFFICIENCY", 40, 88, &fonts::Font4);
    _canvas->drawFastHLine(40, 160, 345, COLOR_BORDER);

    _canvas->drawString("Trip Energy:", 40, 172, &fonts::Font4);
    _canvas->drawString("Charge Used:", 40, 202, &fonts::Font4);
    _canvas->drawString("Pack Remaining:", 40, 232, &fonts::Font4);
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawString("Projected Range:", 40, 262, &fonts::Font4);

    // Card 2 Static Labels
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawString("BATTERY HEALTH & CAPACITY", 435, 60, &fonts::Font4);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->drawString("BATTERY HEALTH (SoH)", 435, 88, &fonts::Font4);
    _canvas->drawFastHLine(435, 160, 345, COLOR_BORDER);

    _canvas->drawString("Active Profile:", 435, 172, &fonts::Font4);
    _canvas->drawString("Learned Capacity:", 435, 202, &fonts::Font4);
    _canvas->drawString("Nominal Capacity:", 435, 232, &fonts::Font4);
    _canvas->drawString("Pack Voltage:", 435, 262, &fonts::Font4);
}

void DashboardRenderer::renderEnergyStatsScreen(const DashTelemetry &telemetry) {
    // Top Bar: Moving Ride Time (Only update when second changes)
    uint32_t rTime = telemetry.stats.ride_time_sec;
    if (rTime != _cache.ride_sec) {
        _cache.ride_sec = rTime;
        int hrs = rTime / 3600;
        int mins = (rTime % 3600) / 60;
        int secs = rTime % 60;
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "MOVING: %02d:%02d:%02d", hrs, mins, secs);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(220);
        _canvas->drawRightString(timeStr, 800, 14, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 1: Hero Metric (Avg Efficiency Wh/km) - Zero flicker update via sprite
    int curEffX10 = (int)roundf(telemetry.stats.trip_wh_km * 10.0f);
    if (curEffX10 != _cache.eff_x10) {
        _cache.eff_x10 = curEffX10;
        char effStr[16];
        if (telemetry.stats.trip_wh_km > 0.0f) {
            snprintf(effStr, sizeof(effStr), "%.1f", telemetry.stats.trip_wh_km);
        } else {
            snprintf(effStr, sizeof(effStr), "--.-");
        }
        drawValWithUnit(40, 108, 340, 48, effStr, "Wh/km", COLOR_WHITE, COLOR_CYAN, COLOR_SURFACE);
    }

    // Card 1: Secondary Key-Value Rows (Only redraw on change)
    char buf[32];
    int curTripWh = (int)roundf(telemetry.stats.trip_wh);
    if (curTripWh != _cache.watts) { // repurpose or check
        snprintf(buf, sizeof(buf), "%.0f Wh", telemetry.stats.trip_wh);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 172, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curAhX100 = (int)roundf(telemetry.amphours_used * 100.0f);
    if (curAhX100 != _cache.current_x10) {
        _cache.current_x10 = curAhX100;
        snprintf(buf, sizeof(buf), "%.2f Ah", telemetry.amphours_used);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 202, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curRemWh = (int)roundf(telemetry.remaining_wh);
    if (curRemWh != _cache.remaining_wh) {
        _cache.remaining_wh = curRemWh;
        snprintf(buf, sizeof(buf), "%.0f Wh", telemetry.remaining_wh);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 232, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curEstRange = (int)roundf(telemetry.est_range_km);
    if (curEstRange != _cache.est_range) {
        _cache.est_range = curEstRange;
        snprintf(buf, sizeof(buf), "%d km", curEstRange);
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 262, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 2: Hero Metric (Health SoH %) - Zero flicker update via sprite
    int curSohX10 = (int)roundf(telemetry.battery_health_soh * 10.0f);
    if (curSohX10 != _cache.soh_x10) {
        _cache.soh_x10 = curSohX10;
        char sohStr[16];
        snprintf(sohStr, sizeof(sohStr), "%.1f", telemetry.battery_health_soh);
        drawValWithUnit(435, 108, 340, 48, sohStr, "%", COLOR_GREEN, COLOR_GREEN, COLOR_SURFACE);
    }

    // Card 2: Secondary Key-Value Rows
    if (telemetry.battery_profile_id != _cache.batt_prof) {
        _cache.batt_prof = telemetry.battery_profile_id;
        const char *pName = (telemetry.battery_profile_id == 0) ? "[B1 FRESH]" : "[B2 DAILY]";
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(pName, 780, 172, &fonts::Font4);
        _canvas->setTextPadding(0);

        const BatteryProfile &prof = Battery.getProfile(telemetry.battery_profile_id);
        snprintf(buf, sizeof(buf), "%.0f Wh", prof.learned_wh);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 202, &fonts::Font4);

        snprintf(buf, sizeof(buf), "%.0f Wh", prof.nominal_wh);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 232, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curVoltX10 = (int)roundf(telemetry.voltage * 10.0f);
    if (curVoltX10 != _cache.voltage_x10) {
        _cache.voltage_x10 = curVoltX10;
        snprintf(buf, sizeof(buf), "%.1f V", telemetry.voltage);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 262, &fonts::Font4);
        _canvas->setTextPadding(0);
    }
}

// ==============================================================================
// SCREEN 2: POWER & PERFORMANCE ANALYTICS DASHBOARD
// ==============================================================================
void DashboardRenderer::initPerfStatsScreen(const DashTelemetry &telemetry) {
    // Header Bar (Short clean title with [2/2], zero text collision)
    _canvas->setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas->drawString("2. POWER & DYNAMICS", 25, 14, &fonts::Font4);

    _canvas->setTextColor(COLOR_MUTED_GRAY, COLOR_BG);
    _canvas->drawString("[2/2]", 310, 14, &fonts::Font4);

    _canvas->drawFastHLine(20, 42, 780, COLOR_BORDER);

    // Two Large, Wide Cards (w=375, h=255)
    drawCard(25,  50, 375, 255, COLOR_SURFACE, COLOR_BORDER);
    drawCard(420, 50, 375, 255, COLOR_SURFACE, COLOR_BORDER);

    // Card 1 Static Labels
    _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
    _canvas->drawString("ELECTRICAL POWER PEAKS", 40, 60, &fonts::Font4);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->drawString("PEAK ELECTRICAL POWER", 40, 88, &fonts::Font4);
    _canvas->drawFastHLine(40, 160, 345, COLOR_BORDER);

    _canvas->drawString("Peak Battery Current:", 40, 172, &fonts::Font4);
    _canvas->drawString("Peak Phase Current:", 40, 202, &fonts::Font4);
    _canvas->drawString("Peak Motor Temp:", 40, 232, &fonts::Font4);
    _canvas->drawString("Peak ESC Temp:", 40, 262, &fonts::Font4);

    // Card 2 Static Labels
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawString("SPEED & TRIP DYNAMICS", 435, 60, &fonts::Font4);
    _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
    _canvas->drawString("MAX VEHICLE SPEED", 435, 88, &fonts::Font4);
    _canvas->drawFastHLine(435, 160, 345, COLOR_BORDER);

    _canvas->drawString("Average Speed:", 435, 172, &fonts::Font4);
    _canvas->drawString("Trip Distance:", 435, 202, &fonts::Font4);
    _canvas->drawString("Lifetime ODO:", 435, 232, &fonts::Font4);
    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawString("Reset Statistics:", 435, 262, &fonts::Font4);
}

void DashboardRenderer::renderPerfStatsScreen(const DashTelemetry &telemetry) {
    // Top Bar: Moving Ride Time (Only redraw when second changes)
    uint32_t rTime = telemetry.stats.ride_time_sec;
    if (rTime != _cache.ride_sec) {
        _cache.ride_sec = rTime;
        int hrs = rTime / 3600;
        int mins = (rTime % 3600) / 60;
        int secs = rTime % 60;
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "MOVING: %02d:%02d:%02d", hrs, mins, secs);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
        _canvas->setTextPadding(220);
        _canvas->drawRightString(timeStr, 800, 14, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 1: Hero Metric (Peak Power) - Zero flicker update via sprite
    int curPeakWatts = (int)roundf(telemetry.stats.peak_power_watts);
    if (curPeakWatts != _cache.peak_watts) {
        _cache.peak_watts = curPeakWatts;
        char pNumStr[16];
        const char *pUnit = "W";
        if (curPeakWatts >= 1000) {
            snprintf(pNumStr, sizeof(pNumStr), "%.2f", curPeakWatts / 1000.0f);
            pUnit = "kW";
        } else {
            snprintf(pNumStr, sizeof(pNumStr), "%d", curPeakWatts);
            pUnit = "W";
        }
        uint16_t pCol = getPowerColor(telemetry.stats.peak_power_watts);
        drawValWithUnit(40, 108, 340, 48, pNumStr, pUnit, pCol, COLOR_AMBER, COLOR_SURFACE);
    }

    // Card 1: Secondary Key-Value Rows (Only update when value changes)
    char buf[32];
    int curPeakCurrX10 = (int)roundf(telemetry.stats.peak_current_amps * 10.0f);
    if (curPeakCurrX10 != _cache.peak_curr_x10) {
        _cache.peak_curr_x10 = curPeakCurrX10;
        snprintf(buf, sizeof(buf), "%.1f A", telemetry.stats.peak_current_amps);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 172, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curPeakPhaseX10 = (int)roundf(telemetry.stats.peak_phase_amps * 10.0f);
    if (curPeakPhaseX10 != _cache.peak_phase_x10) {
        _cache.peak_phase_x10 = curPeakPhaseX10;
        snprintf(buf, sizeof(buf), "%.1f A", telemetry.stats.peak_phase_amps);
        _canvas->setTextColor(getPhaseColor(telemetry.stats.peak_phase_amps), COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 202, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curMaxMot = (int)roundf(telemetry.stats.max_temp_motor);
    if (curMaxMot != _cache.temp_motor) {
        _cache.temp_motor = curMaxMot;
        snprintf(buf, sizeof(buf), "%.1f C", telemetry.stats.max_temp_motor);
        _canvas->setTextColor(getTempColor(telemetry.stats.max_temp_motor), COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 232, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curMaxEsc = (int)roundf(telemetry.stats.max_temp_esc);
    if (curMaxEsc != _cache.temp_esc) {
        _cache.temp_esc = curMaxEsc;
        snprintf(buf, sizeof(buf), "%.1f C", telemetry.stats.max_temp_esc);
        _canvas->setTextColor(getTempColor(telemetry.stats.max_temp_esc), COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 385, 262, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    // Card 2: Hero Metric (Max Speed) - Zero flicker update via sprite
    int curMaxSpdX10 = (int)roundf(telemetry.stats.max_speed_kmh * 10.0f);
    if (curMaxSpdX10 != _cache.max_spd_x10) {
        _cache.max_spd_x10 = curMaxSpdX10;
        char spdNumStr[16];
        snprintf(spdNumStr, sizeof(spdNumStr), "%.1f", telemetry.stats.max_speed_kmh);
        drawValWithUnit(435, 108, 340, 48, spdNumStr, "km/h", getSpeedColor(telemetry.stats.max_speed_kmh), COLOR_CYAN, COLOR_SURFACE);
    }

    // Card 2: Secondary Key-Value Rows
    int curAvgSpdX10 = (int)roundf(telemetry.stats.avg_speed_kmh * 10.0f);
    if (curAvgSpdX10 != _cache.avg_spd_x10) {
        _cache.avg_spd_x10 = curAvgSpdX10;
        snprintf(buf, sizeof(buf), "%.1f km/h", telemetry.stats.avg_speed_kmh);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 172, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curTripX10 = (int)roundf(telemetry.trip_km * 10.0f);
    if (curTripX10 != _cache.trip_x10) {
        _cache.trip_x10 = curTripX10;
        snprintf(buf, sizeof(buf), "%.1f km", telemetry.trip_km);
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 202, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    int curOdo = (int)roundf(telemetry.odo_km);
    if (curOdo != _cache.odo) {
        _cache.odo = curOdo;
        snprintf(buf, sizeof(buf), "%.1f km", telemetry.odo_km);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->setTextPadding(140);
        _canvas->drawRightString(buf, 780, 232, &fonts::Font4);
        _canvas->setTextPadding(0);
    }

    _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
    _canvas->drawRightString("[Hold BTN2]", 780, 262, &fonts::Font4);
}

// ==============================================================================
// SCREEN 3: INTERACTIVE SETTINGS MENU SYSTEM
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

// All description lines strictly <= 21 characters to guarantee zero spill
static void getSubItemDetails2Lines(uint8_t sub_id, uint8_t item_id,
                                    char *title, size_t title_sz,
                                    char *val, size_t val_sz,
                                    char *d1, size_t d1_sz,
                                    char *d2, size_t d2_sz,
                                    const DashTelemetry &telemetry) {
    DashSettings &s = Settings.get();
    title[0] = 0; val[0] = 0; d1[0] = 0; d2[0] = 0;

    switch (sub_id) {
        case 0: // POWER & DRIVE
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Max Battery Current");
                    snprintf(val, val_sz, "%d A", s.max_battery_amps);
                    snprintf(d1, d1_sz, "Pack current limit");
                    snprintf(d2, d2_sz, "Safe BBS: 5A - 35A");
                    break;
                case 1:
                    snprintf(title, title_sz, "Max Phase Current");
                    snprintf(val, val_sz, "%d A", s.max_phase_amps);
                    snprintf(d1, d1_sz, "Motor torque limit");
                    snprintf(d2, d2_sz, "Safe BBS: 10A - 70A");
                    break;
                case 2:
                    snprintf(title, title_sz, "Field Weakening (FW)");
                    snprintf(val, val_sz, "%d A", s.field_weak_amps);
                    snprintf(d1, d1_sz, "Top speed boost");
                    snprintf(d2, d2_sz, "Redline: 0A - 20A");
                    break;
                case 3:
                    snprintf(title, title_sz, "Speed Limit");
                    if (s.max_speed_kmh == 0) {
                        snprintf(val, val_sz, "OFF (Unlimited)");
                    } else {
                        snprintf(val, val_sz, "%d km/h", s.max_speed_kmh);
                    }
                    snprintf(d1, d1_sz, "Motor cutoff speed");
                    snprintf(d2, d2_sz, "OFF, 25, 32, 45, 60");
                    break;
                case 4:
                    snprintf(title, title_sz, "Throttle Ramp Time");
                    snprintf(val, val_sz, "%.2f s", s.throttle_ramp_sec);
                    snprintf(d1, d1_sz, "Throttle filter");
                    snprintf(d2, d2_sz, "Smooth: 0.10s-1.00s");
                    break;
            }
            break;

        case 1: // DISPLAY & UI
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Dashboard Style");
                    snprintf(val, val_sz, (s.dash_style == 0) ? "Analog Dial" : "Duty Bar");
                    snprintf(d1, d1_sz, "Instrument cluster");
                    snprintf(d2, d2_sz, "Analog Dial / Bar");
                    break;
                case 1:
                    snprintf(title, title_sz, "Screen Brightness");
                    snprintf(val, val_sz, "%d %%", s.brightness_pct);
                    snprintf(d1, d1_sz, "Backlight level");
                    snprintf(d2, d2_sz, "Range: 20%% to 100%%");
                    break;
            }
            break;

        case 2: // BATTERY PROFILES
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Active Profile");
                    snprintf(val, val_sz, (s.active_battery_profile == 0) ? "Profile 1 (Fresh)" : "Profile 2 (Daily)");
                    snprintf(d1, d1_sz, "Switch 52V pack");
                    snprintf(d2, d2_sz, "Profile 1 or 2");
                    break;
                case 1:
                    snprintf(title, title_sz, "Profile 1 Nominal Wh");
                    snprintf(val, val_sz, "%.0f Wh", Battery.getProfile(0).nominal_wh);
                    snprintf(d1, d1_sz, "Pack 1 full Wh");
                    snprintf(d2, d2_sz, "500Wh to 2500Wh");
                    break;
                case 2:
                    snprintf(title, title_sz, "Profile 2 Nominal Wh");
                    snprintf(val, val_sz, "%.0f Wh", Battery.getProfile(1).nominal_wh);
                    snprintf(d1, d1_sz, "Pack 2 full Wh");
                    snprintf(d2, d2_sz, "500Wh to 2500Wh");
                    break;
                case 3:
                    snprintf(title, title_sz, "Reset SoH Learning");
                    snprintf(val, val_sz, "RESET LEARNING");
                    snprintf(d1, d1_sz, "Reset capacity learn");
                    snprintf(d2, d2_sz, "Press [1+2] to reset");
                    break;
            }
            break;

        case 3: // BIKE SETUP
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Wheel Diameter");
                    snprintf(val, val_sz, "%.1f in", s.wheel_diameter_in);
                    snprintf(d1, d1_sz, "Tire outer diameter");
                    snprintf(d2, d2_sz, "20.0 to 29.0 inches");
                    break;
                case 1:
                    snprintf(title, title_sz, "Motor Pole Pairs");
                    snprintf(val, val_sz, "%d", s.motor_pole_pairs);
                    snprintf(d1, d1_sz, "ERPM divisor");
                    snprintf(d2, d2_sz, "Bafang standard: 15");
                    break;
                case 2:
                    snprintf(title, title_sz, "Internal Gear Ratio");
                    snprintf(val, val_sz, "%.2f : 1", s.gear_ratio);
                    snprintf(d1, d1_sz, "Rotor to gear ratio");
                    snprintf(d2, d2_sz, "Ratio: 1.00 to 4.00");
                    break;
            }
            break;

        case 4: // SYSTEM & DIAGNOSTICS
            switch (item_id) {
                case 0:
                    snprintf(title, title_sz, "Reset Trip Statistics");
                    snprintf(val, val_sz, "RESET TRIP STATS");
                    snprintf(d1, d1_sz, "Clear trip distance");
                    snprintf(d2, d2_sz, "Press [1+2] to clear");
                    break;
                case 1:
                    snprintf(title, title_sz, "Factory Reset");
                    snprintf(val, val_sz, "RESTORE DEFAULTS");
                    snprintf(d1, d1_sz, "Restore defaults");
                    snprintf(d2, d2_sz, "Press [1+2] to reset");
                    break;
                case 2:
                    snprintf(title, title_sz, "VESC Hardware Link");
                    snprintf(val, val_sz, telemetry.vesc_connected ? "COMM HEALTHY" : "OFFLINE");
                    snprintf(d1, d1_sz, "Flipsky 75100 V1");
                    snprintf(d2, d2_sz, "TX:43 RX:44 (115k)");
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

        char title[48], val[32], d1[64], d2[64];
        getSubItemDetails2Lines(sub_id, j, title, sizeof(title), val, sizeof(val), d1, sizeof(d1), d2, sizeof(d2), telemetry);

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

    // 2. Clear inner card contents (Clears completely past card border to prevent stuck artifacts!)
    _canvas->fillRect(22, 54, 306, 252, COLOR_SURFACE);
    _canvas->fillRect(346, 54, 452, 252, COLOR_SURFACE);
    _canvas->fillRect(801, 54, 19, 252, COLOR_BG); // Wipe any outside spill on the right edge!

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
        _canvas->drawString(ROOT_CATEGORIES[_menu_root_idx], 365, 68, &fonts::Font4);

        const char *descs1[5] = {
            "Power Limits & Drive",
            "Theme & Brightness",
            "Dual 52V Management",
            "Speed & Gearing",
            "Diagnostics & Reset"
        };
        const char *descs2[5] = {
            "Bat 35A, Phase 70A",
            "Analog / Bar Style",
            "Independent SoH & Wh",
            "Tire, Poles, Ratio",
            "Trip & Defaults"
        };
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->drawString(descs1[_menu_root_idx], 365, 120, &fonts::Font4);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString(descs2[_menu_root_idx], 365, 154, &fonts::Font4);

        // Hints strictly under 21 characters:
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->drawString("[1+2] Enter Submenu", 365, 230, &fonts::Font4);
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString("Hold [2] Exit to Dash", 365, 268, &fonts::Font4);
    } else {
        char val[32], d1[64], d2[64];
        getSubItemDetails2Lines(_menu_root_idx, _menu_sub_idx, curTitle, sizeof(curTitle), val, sizeof(val), d1, sizeof(d1), d2, sizeof(d2), telemetry);

        // Setting Title
        _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
        _canvas->drawString(curTitle, 365, 64, &fonts::Font4);

        // Big Value Box
        int boxY = 96, boxH = 68;
        uint16_t vBg = _menu_edit_mode ? 0x2124 : COLOR_SURFACE;
        uint16_t vBorder = _menu_edit_mode ? COLOR_AMBER : COLOR_BORDER;
        uint16_t vColor = _menu_edit_mode ? COLOR_AMBER : COLOR_WHITE;

        _canvas->fillRoundRect(365, boxY, 415, boxH, 8, vBg);
        _canvas->drawRoundRect(365, boxY, 415, boxH, 8, vBorder);

        // Detect if value string contains letters (use Font4 for words, Font6 for numbers)
        bool hasLetters = false;
        for (int k = 0; val[k] != '\0'; k++) {
            if ((val[k] >= 'a' && val[k] <= 'z') || (val[k] >= 'A' && val[k] <= 'Z')) {
                hasLetters = true;
                break;
            }
        }
        const lgfx::IFont *vFont = hasLetters ? &fonts::Font4 : &fonts::Font6;
        int textY = hasLetters ? (boxY + 20) : (boxY + 10);

        _canvas->setTextColor(vColor, vBg);
        _canvas->drawCenterString(val, 365 + (415 / 2), textY, vFont);

        // 2-Line Font4 Descriptions (Never Overflow!)
        _canvas->setTextColor(COLOR_WHITE, COLOR_SURFACE);
        _canvas->drawString(d1, 365, 178, &fonts::Font4);

        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
        _canvas->drawString(d2, 365, 208, &fonts::Font4);

        // Help Hints (Strictly <= 21 characters, never spill outside card!)
        if (_menu_edit_mode) {
            _canvas->setTextColor(COLOR_AMBER, COLOR_SURFACE);
            _canvas->drawString("[1] +    [2] -", 365, 240, &fonts::Font4);
            _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
            _canvas->drawString("[1+2] Save & Exit", 365, 268, &fonts::Font4);
        } else {
            _canvas->setTextColor(COLOR_CYAN, COLOR_SURFACE);
            _canvas->drawString("[1+2] Edit Setting", 365, 240, &fonts::Font4);
            _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_SURFACE);
            _canvas->drawString("Hold [2] Back to Menu", 365, 268, &fonts::Font4);
        }
    }

    // 5. Header Breadcrumb
    char topBuf[64];
    if (_menu_edit_mode) {
        snprintf(topBuf, sizeof(topBuf), "EDITING VALUE");
        _canvas->setTextColor(COLOR_AMBER, COLOR_BG);
    } else if (_menu_in_sub) {
        snprintf(topBuf, sizeof(topBuf), "%s", curTitle);
        _canvas->setTextColor(COLOR_CYAN, COLOR_BG);
    } else {
        snprintf(topBuf, sizeof(topBuf), "SETTINGS");
        _canvas->setTextColor(COLOR_LIGHT_GRAY, COLOR_BG);
    }
    _canvas->setTextPadding(300);
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
        // BTN1 = UP (+), BTN2 = DOWN (-)
        if (action == NAV_EDIT_INC || action == NAV_BTN1_SHORT) {
            adjustCurrentSetting(+1, telemetry);
        } else if (action == NAV_EDIT_DEC || action == NAV_BTN2_SHORT) {
            adjustCurrentSetting(-1, telemetry);
        } else if (action == NAV_BOTH_PRESSED || action == NAV_BTN2_LONG || action == NAV_BTN1_LONG) {
            _menu_edit_mode = false;
            Buttons.setEditMode(false);
            Settings.save();
            _menu_dirty = true;
        }
        return;
    }

    // Normal menu navigation mode:
    // BTN1 = UP, BTN2 = DOWN, BOTH = SELECT/ENTER, BTN2 LONG = BACK/EXIT
    if (_menu_in_sub) {
        if (action == NAV_BTN1_SHORT) { // Cursor UP
            int total = getSubItemCount(_menu_root_idx);
            if (total > 0) {
                _menu_sub_idx = (_menu_sub_idx - 1 + total) % total;
                _menu_dirty = true;
            }
        } else if (action == NAV_BTN2_SHORT) { // Cursor DOWN
            int total = getSubItemCount(_menu_root_idx);
            if (total > 0) {
                _menu_sub_idx = (_menu_sub_idx + 1) % total;
                _menu_dirty = true;
            }
        } else if (action == NAV_BOTH_PRESSED) { // SELECT / ENTER
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
        } else if (action == NAV_BTN2_LONG || action == NAV_BTN1_LONG) { // BACK
            _menu_in_sub = false;
            _menu_dirty = true;
        }
    } else {
        // In Root Menu:
        if (action == NAV_BTN1_SHORT) { // Cursor UP
            _menu_root_idx = (_menu_root_idx - 1 + 5) % 5;
            _menu_dirty = true;
        } else if (action == NAV_BTN2_SHORT) { // Cursor DOWN
            _menu_root_idx = (_menu_root_idx + 1) % 5;
            _menu_dirty = true;
        } else if (action == NAV_BOTH_PRESSED) { // ENTER SUBMENU
            _menu_in_sub = true;
            _menu_sub_idx = 0;
            _menu_dirty = true;
        } else if (action == NAV_BTN2_LONG || action == NAV_BTN1_LONG) { // EXIT SETTINGS
            telemetry.screen = SCREEN_RIDE_DASH;
            _screen_dirty = true;
            _cache.invalidate();
        }
    }
}

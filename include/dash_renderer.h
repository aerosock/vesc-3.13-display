#pragma once

#include <Arduino.h>
#include "display_driver.h"
#include "dash_types.h"
#include "settings_manager.h"
#include "battery_manager.h"

class DashboardRenderer {
public:
    DashboardRenderer();
    void begin(DisplayDriver *display);
    void render(const DashTelemetry &telemetry);
    void triggerNeedleSweep();
    bool isSweeping() const { return _is_sweeping; }
    void markScreenDirty() { _screen_dirty = true; }

    // Menu Navigation Handler (called by main when in SCREEN_SETTINGS_MENU)
    void handleMenuNav(NavAction action, DashTelemetry &telemetry);
    bool isInEditMode() const { return _menu_edit_mode; }

private:
    DisplayDriver *_display;
    LovyanGFX     *_canvas;

    // Screen 0: Main Ride Dashboard (renders selected style)
    void initRideDashboard(const DashTelemetry &telemetry);
    void renderRideDashboard(const DashTelemetry &telemetry);

    // Style A: Left-Hug Analog Duty Dial Cluster (Motorcycle/Koso inspired)
    void initLeftHugAnalogStyle(const DashTelemetry &telemetry);
    void renderLeftHugAnalogStyle(const DashTelemetry &telemetry);
    void drawAnalogNeedle(int cx, int cy, int length, float angle_rad, uint16_t color);

    // Style B: Horizontal Duty Bar Cluster
    void initHorizontalBarStyle(const DashTelemetry &telemetry);
    void renderHorizontalBarStyle(const DashTelemetry &telemetry);
    void drawPreciseDutyBar(int x, int y, int w, int h, float duty, float max_duty);

    // Screen 1: Energy & Battery Analytics Dashboard
    void initEnergyStatsScreen(const DashTelemetry &telemetry);
    void renderEnergyStatsScreen(const DashTelemetry &telemetry);

    // Screen 2: Power, Speed & Dynamics Analytics Dashboard
    void initPerfStatsScreen(const DashTelemetry &telemetry);
    void renderPerfStatsScreen(const DashTelemetry &telemetry);

    // Screen 3: Interactive Settings Menu System
    void initSettingsScreen(const DashTelemetry &telemetry);
    void renderSettingsScreen(const DashTelemetry &telemetry);
    void renderRootMenu();
    void renderSubmenu(uint8_t sub_id, const DashTelemetry &telemetry);
    void adjustCurrentSetting(int direction, DashTelemetry &telemetry); // +1 or -1

    // Common UI Helpers
    void drawCard(int x, int y, int w, int h, uint16_t bg, uint16_t border);
    void drawValWithUnit(int x, int y, int numPadW, int unitPadW, const char *numStr, const char *unitStr, uint16_t numCol, uint16_t unitCol, uint16_t bgCol = 0x0821);

    // Screen State
    DashboardScreen _active_screen;
    DashboardStyle  _active_style;
    bool            _screen_dirty;

    // Menu Navigation State
    bool     _menu_in_sub;
    uint8_t  _menu_root_idx;
    uint8_t  _menu_sub_idx;
    bool     _menu_edit_mode;
    uint32_t _menu_last_activity_ms;
    bool     _menu_dirty;

    // Differential Render Cache
    struct RenderCache {
        int speed;
        int duty_x10;
        int duty_fill_w;
        int battery_pct;
        int voltage_x10;
        int current_x10;
        int phase_x10;
        int watts;
        int remaining_wh;
        int est_range;
        int temp_motor;
        int temp_esc;
        int trip_x10;
        int odo;
        int uptime;
        bool vesc_connected;
        uint8_t batt_prof;

        // Statistics Screen Caches
        int peak_watts;
        int peak_curr_x10;
        int peak_phase_x10;
        int max_spd_x10;
        int avg_spd_x10;
        int eff_x10;
        int soh_x10;
        uint32_t ride_sec;

        void invalidate() {
            speed = -999;
            duty_x10 = -999;
            duty_fill_w = -1;
            battery_pct = -1;
            voltage_x10 = -1;
            current_x10 = -1;
            phase_x10 = -1;
            watts = -99999;
            remaining_wh = -1;
            est_range = -1;
            temp_motor = -999;
            temp_esc = -999;
            trip_x10 = -1;
            odo = -1;
            uptime = -1;
            vesc_connected = false;
            batt_prof = 255;

            peak_watts = -99999;
            peak_curr_x10 = -1;
            peak_phase_x10 = -1;
            max_spd_x10 = -1;
            avg_spd_x10 = -1;
            eff_x10 = -1;
            soh_x10 = -1;
            ride_sec = 0xFFFFFFFF;
        }
    } _cache;

    // Analog Needle State
    struct NeedleCoords {
        int tipX, tipY, b1X, b1Y, b2X, b2Y;
        bool valid;
    } _last_needle;

    // Needle Sweep Animation State
    bool     _is_sweeping;
    uint8_t  _sweep_phase;
    float    _sweep_progress;
    float    _sweep_duty;
    uint32_t _last_sweep_tick_ms;
};

extern DashboardRenderer Renderer;

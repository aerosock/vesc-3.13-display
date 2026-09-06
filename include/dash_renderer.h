#pragma once

#include <Arduino.h>
#include "display_driver.h"
#include "dash_types.h"

class DashboardRenderer {
public:
    DashboardRenderer();
    void begin(DisplayDriver *display);
    void render(const DashTelemetry &telemetry);
    void triggerR1Sweep();
    bool isSweeping() const { return _is_sweeping; }
    void markScreenDirty() { _screen_dirty = true; }

private:
    DisplayDriver *_display;
    LovyanGFX     *_canvas;

    // Screen 1: Practical Horizontal Tachometer (Large, Clean OEM Design)
    void initHorizontalTachScreen(const DashTelemetry &telemetry);
    void renderHorizontalTachScreen(const DashTelemetry &telemetry);
    void drawPreciseRpmBar(int x, int y, int w, int h, float rpm, float max_rpm);

    // Screen 2: Yamaha R1 Analog Dial + Superbike Cluster
    void initR1DialScreen(const DashTelemetry &telemetry);
    void renderR1DialScreen(const DashTelemetry &telemetry);
    void drawR1AnalogDial(int cx, int cy, int radius, float rpm, float max_rpm);
    void drawR1Needle(int cx, int cy, int length, float angle_rad, uint16_t color);

    // Screen 3: Settings & Diagnostics Menu
    void initSettingsScreen(const DashTelemetry &telemetry);
    void renderSettingsScreen(const DashTelemetry &telemetry);

    // Common UI Helpers with Large High-Contrast Typography
    void drawCard(int x, int y, int w, int h, uint16_t bg, uint16_t border);

    // Screen Dirty & Layout State (Differential Rendering)
    DashboardScreen _active_screen;
    bool            _screen_dirty;
    RidingMode      _last_mode;

    struct RenderCache {
        int speed;
        int rpm_fill_w;
        int rpm_val;
        int battery_pct;
        int voltage_x10;
        int current_x10;
        int watts;
        int temp_motor;
        int temp_esc;
        int trip_x10;
        int odo;
        int uptime;
        bool vesc_connected;
        void invalidate() {
            speed = -999;
            rpm_fill_w = -1;
            rpm_val = -999;
            battery_pct = -1;
            voltage_x10 = -1;
            current_x10 = -1;
            watts = -99999;
            temp_motor = -999;
            temp_esc = -999;
            trip_x10 = -1;
            odo = -1;
            uptime = -1;
            vesc_connected = false;
        }
    } _cache;

    // R1 Analog Needle State
    struct NeedleCoords {
        int tipX, tipY, b1X, b1Y, b2X, b2Y;
        bool valid;
    } _last_needle;

    // R1 Sweep Animation State
    bool     _is_sweeping;
    uint8_t  _sweep_phase;
    float    _sweep_progress;
    float    _sweep_rpm;
    uint32_t _last_sweep_tick_ms;
};

extern DashboardRenderer Renderer;

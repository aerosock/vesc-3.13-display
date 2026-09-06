#pragma once

#include <Arduino.h>

// Dashboard Screen / View IDs
enum DashboardScreen {
    SCREEN_HORIZONTAL_TACH = 0,  // Style 1: Clean OEM horizontal tachometer + big center speed
    SCREEN_R1_ANALOG_TACH  = 1,  // Style 2: Yamaha R1 analog dial + superbike digital cluster
    SCREEN_SETTINGS_MENU   = 2,  // Style 3: Settings / Menu / Diagnostics screen
    SCREEN_COUNT
};

// Generic Navigation Action Events (for menus, settings, and multi-function navigation)
enum NavAction {
    NAV_NONE = 0,
    NAV_PREV,        // Button 1 Short Press (Up / Prev / Switch Page)
    NAV_NEXT,        // Button 2 Short Press (Down / Next / Cycle Mode)
    NAV_MENU_BACK,   // Button 1 Long Press  (Menu / Back / Re-sweep)
    NAV_SELECT_ENTER // Button 2 Long Press  (Select / Enter / Reset)
};

// Riding / Assist Modes
enum RidingMode {
    MODE_ECO   = 0,
    MODE_STD   = 1,
    MODE_SPORT = 2,
    MODE_BOOST = 3,
    MODE_COUNT
};

// Core Telemetry Data Structure
struct DashTelemetry {
    // Speed & Motor
    float speed_kmh;        // Vehicle speed (km/h)
    float rpm;              // Motor RPM
    float throttle_pct;     // Throttle input percentage (0 - 100%)

    // Electrical
    float voltage;          // Battery pack voltage (V)
    float current_amps;     // Motor / Battery current (A)
    float power_watts;      // Power (W)
    float battery_pct;      // Battery State of Charge (0 - 100%)
    float amphours_used;    // Amp-hours consumed

    // Thermal
    float temp_motor;       // Motor temperature (°C)
    float temp_esc;         // Flipsky 75100 Mosfet/ESC temperature (°C)

    // Odometry
    float trip_km;          // Trip distance (km)
    float odo_km;           // Total odometer (km)

    // Status & Navigation State
    RidingMode mode;        // Active power map / riding mode
    DashboardScreen screen; // Currently active screen/view
    bool vesc_connected;    // UART communication healthy
    uint32_t uptime_sec;    // System run time in seconds
};

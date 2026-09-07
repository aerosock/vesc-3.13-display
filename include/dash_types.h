#pragma once

#include <Arduino.h>

// Top-Level Screens (Cycled via Button 1)
enum DashboardScreen {
    SCREEN_RIDE_DASH     = 0,  // Main Ride Dashboard (renders active style: Analog or Horizontal)
    SCREEN_TRIP_STATS    = 1,  // Trip & Energy Statistics Dashboard
    SCREEN_SETTINGS_MENU = 2,  // Interactive Settings & Diagnostics Menu
    SCREEN_COUNT
};

// Main Dashboard Styles (Selected in Settings)
enum DashboardStyle {
    STYLE_ANALOG_DIAL    = 0,  // Left-Hug Analog Duty Dial (0-120% Duty with FW Redline)
    STYLE_HORIZONTAL_BAR = 1,  // Horizontal Duty Bar Cluster
    STYLE_COUNT
};

// Navigation Actions
enum NavAction {
    NAV_NONE = 0,
    NAV_BTN1_SHORT,    // Button 1 Short Press (Next Screen in Dash / Scroll Down in Menu / Value - in Edit)
    NAV_BTN1_LONG,     // Button 1 Long Press  (Menu Back / Cancel)
    NAV_BTN2_SHORT,    // Button 2 Short Press (Quick Action in Dash / Select in Menu / Value + in Edit)
    NAV_BTN2_LONG,     // Button 2 Long Press  (Trip Reset / Secondary Action)
    NAV_EDIT_DEC,      // Accelerated Decrement
    NAV_EDIT_INC,      // Accelerated Increment
    NAV_BOTH_PRESSED   // Both buttons pressed simultaneously
};

// Long-Term & Ride Statistics
struct TripStatistics {
    float trip_wh;           // Energy consumed this trip (Watt-hours)
    float trip_wh_km;        // Energy efficiency (Wh/km)
    float peak_current_amps; // Maximum battery current recorded (A)
    float peak_phase_amps;   // Maximum motor phase current recorded (A)
    float peak_power_watts;  // Maximum power recorded (W)
    float max_speed_kmh;     // Maximum speed reached (km/h)
    float avg_speed_kmh;     // Average speed during trip (km/h)
    uint32_t ride_time_sec;  // Elapsed moving/ride time in seconds
    float max_temp_motor;    // Peak motor temperature recorded (°C)
    float max_temp_esc;      // Peak ESC/Mosfet temperature recorded (°C)

    void reset() {
        trip_wh = 0.0f;
        trip_wh_km = 0.0f;
        peak_current_amps = 0.0f;
        peak_phase_amps = 0.0f;
        peak_power_watts = 0.0f;
        max_speed_kmh = 0.0f;
        avg_speed_kmh = 0.0f;
        ride_time_sec = 0;
        max_temp_motor = 0.0f;
        max_temp_esc = 0.0f;
    }
};

// Core Telemetry Data Structure
struct DashTelemetry {
    // Speed & Inverter State
    float speed_kmh;           // Vehicle speed (km/h)
    float duty_cycle_pct;      // Inverter Duty Cycle (0.0% to 120.0%, >100% = Field Weakening)
    float rpm;                 // Motor mechanical RPM
    float throttle_pct;        // Throttle input percentage (0 - 100%)

    // Electrical (Bafang Mid-Drive Tuned)
    float voltage;             // Battery pack voltage (V)
    float current_amps;        // Battery pack current (A, max 35A)
    float phase_amps;          // Motor phase current (A, max 70A)
    float power_watts;         // Power (W = V * I)
    float amphours_used;       // Amp-hours consumed (Ah)

    // Smart Battery & Health State
    uint8_t battery_profile_id;// 0 = Profile 1 (Fresh 52V), 1 = Profile 2 (Daily 52V)
    float   battery_pct;       // Hybrid Fused State of Charge (0 - 100%)
    float   remaining_wh;      // Remaining energy in pack (Wh)
    float   battery_health_soh;// Learned battery health State of Health (SoH %)
    float   est_range_km;      // Projected remaining range (km)

    // Thermals
    float temp_motor;          // Bafang motor temperature (°C)
    float temp_esc;            // Flipsky 75100 Mosfet temperature (°C)

    // Odometry & Statistics
    float trip_km;             // Trip distance (km)
    float odo_km;              // Total lifetime odometer (km)
    TripStatistics stats;      // Ride & energy statistics

    // System Status
    DashboardScreen screen;    // Currently active screen
    bool vesc_connected;       // UART communication healthy
    uint32_t uptime_sec;       // System run time in seconds
};

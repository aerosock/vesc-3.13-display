#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "dash_types.h"

struct DashSettings {
    uint8_t dash_style;          // 0 = STYLE_ANALOG_DIAL, 1 = STYLE_HORIZONTAL_BAR
    uint8_t brightness_pct;      // 20 to 100% (default 90%)
    uint8_t max_battery_amps;    // 5 to 35 A (step 1A, default 28A)
    uint8_t max_phase_amps;      // 10 to 70 A (step 1A, default 55A)
    uint8_t field_weak_amps;     // 0 to 20 A (step 1A, default 10A)
    uint8_t max_speed_kmh;       // 0 = Unlimited, 25, 32, 45, 60
    float   throttle_ramp_sec;   // 0.1 to 1.0 s (default 0.2s)
    float   wheel_diameter_in;   // 20.0 to 29.0 in (default 26.0)
    uint8_t motor_pole_pairs;    // 8 to 24 (default 15)
    float   gear_ratio;          // 1.0 to 4.0 (default 1.0)
    uint8_t active_battery_profile; // 0 = Profile 1, 1 = Profile 2
};

class SettingsManager {
public:
    SettingsManager();
    void begin();
    void save();
    void resetToDefaults();

    DashSettings& get() { return _settings; }
    const DashSettings& get() const { return _settings; }

private:
    DashSettings _settings;
    Preferences  _prefs;
    void loadDefaults();
};

extern SettingsManager Settings;

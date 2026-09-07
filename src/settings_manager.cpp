#include "settings_manager.h"

SettingsManager Settings;

SettingsManager::SettingsManager() {
    loadDefaults();
}

void SettingsManager::loadDefaults() {
    _settings.dash_style             = STYLE_ANALOG_DIAL;
    _settings.brightness_pct         = 90;
    _settings.max_battery_amps       = 28;
    _settings.max_phase_amps         = 55;
    _settings.field_weak_amps        = 10;
    _settings.max_speed_kmh          = 0; // Unlimited
    _settings.throttle_ramp_sec      = 0.2f;
    _settings.wheel_diameter_in      = 26.0f;
    _settings.motor_pole_pairs       = 15;
    _settings.gear_ratio             = 1.0f;
    _settings.active_battery_profile = 0;
}

void SettingsManager::begin() {
    _prefs.begin("ebike_cfg", false);

    if (!_prefs.isKey("cfg_init")) {
        // First boot: save initial defaults
        save();
        _prefs.putBool("cfg_init", true);
        Serial.println("[SETTINGS] Initialized default NVS configuration.");
    } else {
        _settings.dash_style             = _prefs.getUChar("style", STYLE_ANALOG_DIAL);
        _settings.brightness_pct         = _prefs.getUChar("bright", 90);
        _settings.max_battery_amps       = _prefs.getUChar("bat_a", 28);
        _settings.max_phase_amps         = _prefs.getUChar("pha_a", 55);
        _settings.field_weak_amps        = _prefs.getUChar("fw_a", 10);
        _settings.max_speed_kmh          = _prefs.getUChar("spd_lim", 0);
        _settings.throttle_ramp_sec      = _prefs.getFloat("t_ramp", 0.2f);
        _settings.wheel_diameter_in      = _prefs.getFloat("wheel", 26.0f);
        _settings.motor_pole_pairs       = _prefs.getUChar("poles", 15);
        _settings.gear_ratio             = _prefs.getFloat("gear", 1.0f);
        _settings.active_battery_profile = _prefs.getUChar("bat_prof", 0);
        Serial.println("[SETTINGS] Loaded configuration from NVS flash.");
    }
}

void SettingsManager::save() {
    _prefs.putUChar("style", _settings.dash_style);
    _prefs.putUChar("bright", _settings.brightness_pct);
    _prefs.putUChar("bat_a", _settings.max_battery_amps);
    _prefs.putUChar("pha_a", _settings.max_phase_amps);
    _prefs.putUChar("fw_a", _settings.field_weak_amps);
    _prefs.putUChar("spd_lim", _settings.max_speed_kmh);
    _prefs.putFloat("t_ramp", _settings.throttle_ramp_sec);
    _prefs.putFloat("wheel", _settings.wheel_diameter_in);
    _prefs.putUChar("poles", _settings.motor_pole_pairs);
    _prefs.putFloat("gear", _settings.gear_ratio);
    _prefs.putUChar("bat_prof", _settings.active_battery_profile);
}

void SettingsManager::resetToDefaults() {
    loadDefaults();
    save();
}

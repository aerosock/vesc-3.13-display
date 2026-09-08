#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "board_config.h"
#include "display_driver.h"
#include "settings_manager.h"
#include "battery_manager.h"
#include "dash_renderer.h"

MockSerial Serial;
DisplayDriver Display;
extern DashboardRenderer Renderer;

void setBaseTelemetry(DashTelemetry &t) {
    memset(&t, 0, sizeof(t));
    t.vesc_connected = true;
    t.screen = SCREEN_RIDE_DASH;
    t.voltage = 53.4f;
    t.battery_pct = 88.0f;
    t.current_amps = 0.0f;
    t.phase_amps = 0.0f;
    t.power_watts = 0.0f;
    t.duty_cycle_pct = 0.0f;
    t.speed_kmh = 0.0f;
    t.temp_esc = 34.5f;
    t.temp_motor = 38.2f;
    t.trip_km = 14.2f;
    t.odo_km = 342.8f;
    t.amphours_used = 5.3f;
    t.remaining_wh = 1140.0f;
    t.est_range_km = 48.0f;
    t.battery_health_soh = 98.4f;
    t.battery_profile_id = 0;

    t.stats.trip_wh = 275.4f;
    t.stats.trip_wh_km = 19.4f;
    t.stats.peak_power_watts = 1950.0f;
    t.stats.peak_current_amps = 28.4f;
    t.stats.peak_phase_amps = 68.2f;
    t.stats.max_speed_kmh = 58.7f;
    t.stats.avg_speed_kmh = 28.4f;
    t.stats.ride_time_sec = 2538; // ~42 mins
}

void renderAndSave(const char *name, DashTelemetry &t) {
    Renderer.render(t);
    char path[256];
    snprintf(path, sizeof(path), "tools/simulator/screenshots/%s.bmp", name);
    Display.dumpBMP(path);
    printf("[OK] Saved scenario '%s' -> %s\n", name, path);
}

int main(int argc, char **argv) {
    printf("==================================================\n");
    printf("   VESC 3.16 Display Native Emulator & Renderer   \n");
    printf("==================================================\n");

    Settings.begin();
    Battery.begin();
    Display.init();
    Renderer.begin(&Display);

    DashTelemetry t;
    setBaseTelemetry(t);

    // ----------------------------------------------------
    // Scenario 1: Analog Dial - At Rest / Idle
    // ----------------------------------------------------
    Settings.get().dash_style = STYLE_ANALOG_DIAL;
    t.screen = SCREEN_RIDE_DASH;
    t.speed_kmh = 0.0f;
    t.duty_cycle_pct = 0.0f;
    t.power_watts = 0.0f;
    t.phase_amps = 0.0f;
    Renderer.markScreenDirty();
    renderAndSave("01_analog_idle", t);

    // ----------------------------------------------------
    // Scenario 2: Analog Dial - Moderate Cruise
    // ----------------------------------------------------
    t.speed_kmh = 32.4f;
    t.duty_cycle_pct = 48.0f;
    t.power_watts = 680.0f;
    t.phase_amps = 26.5f;
    t.voltage = 52.1f;
    t.battery_pct = 82.0f;
    renderAndSave("02_analog_cruising", t);

    // ----------------------------------------------------
    // Scenario 3: Analog Dial - High Power & Field Weakening Redline
    // ----------------------------------------------------
    t.speed_kmh = 58.2f;
    t.duty_cycle_pct = 112.5f; // Over 100% (Field Weakening!)
    t.power_watts = 1980.0f;   // Over 1500W (Red)
    t.phase_amps = 64.0f;      // Over 55A (Red)
    t.voltage = 49.4f;
    t.battery_pct = 72.0f;
    t.temp_motor = 82.0f;      // Amber/Red
    t.temp_esc = 76.0f;
    renderAndSave("03_analog_field_weakening", t);

    // ----------------------------------------------------
    // Scenario 4: Horizontal Bar - Moderate Cruise
    // ----------------------------------------------------
    Settings.get().dash_style = STYLE_HORIZONTAL_BAR;
    t.speed_kmh = 34.0f;
    t.duty_cycle_pct = 52.0f;
    t.power_watts = 720.0f;
    t.phase_amps = 28.0f;
    t.temp_motor = 45.0f;
    t.temp_esc = 40.0f;
    Renderer.markScreenDirty();
    renderAndSave("04_horizontal_cruising", t);

    // ----------------------------------------------------
    // Scenario 5: Horizontal Bar - High Power & Field Weakening
    // ----------------------------------------------------
    t.speed_kmh = 59.1f;
    t.duty_cycle_pct = 114.0f;
    t.power_watts = 2150.0f;
    t.phase_amps = 68.0f;
    renderAndSave("05_horizontal_field_weakening", t);

    // ----------------------------------------------------
    // Scenario 6: Screen 1 - Energy & Battery Analytics
    // ----------------------------------------------------
    t.screen = SCREEN_ENERGY_STATS;
    Renderer.markScreenDirty();
    renderAndSave("06_energy_analytics", t);

    // ----------------------------------------------------
    // Scenario 7: Screen 2 - Power & Dynamics Analytics
    // ----------------------------------------------------
    t.screen = SCREEN_PERF_STATS;
    Renderer.markScreenDirty();
    renderAndSave("07_power_dynamics", t);

    // ----------------------------------------------------
    // Scenario 8: Screen 3 - Root Settings Menu
    // ----------------------------------------------------
    t.screen = SCREEN_SETTINGS_MENU;
    Renderer.markScreenDirty();
    renderAndSave("08_settings_root", t);

    // ----------------------------------------------------
    // Scenario 9: Screen 3 - Settings Submenu: Power Limits
    // ----------------------------------------------------
    // Enter first submenu in root (Power Limits)
    Renderer.handleMenuNav(NAV_BOTH_PRESSED, t);
    renderAndSave("09_settings_power_limits", t);

    // ----------------------------------------------------
    // Scenario 10: Screen 3 - Settings Submenu in Edit Mode
    // ----------------------------------------------------
    // Enter Edit Mode on Max Battery Amps
    Renderer.handleMenuNav(NAV_BOTH_PRESSED, t);
    renderAndSave("10_settings_edit_mode", t);

    // ----------------------------------------------------
    // Scenario 11: Screen 3 - Settings Submenu: Display & Styles
    // ----------------------------------------------------
    // Exit edit mode, back to root, move down to Display & UI, enter
    Renderer.handleMenuNav(NAV_BOTH_PRESSED, t); // exit edit mode
    Renderer.handleMenuNav(NAV_BTN1_LONG, t);    // back to root
    Renderer.handleMenuNav(NAV_BTN2_SHORT, t);   // move down to 2. DISPLAY & UI
    Renderer.handleMenuNav(NAV_BOTH_PRESSED, t); // enter Display & UI submenu
    renderAndSave("11_settings_display_style", t);

    // ----------------------------------------------------
    // Scenario 12: Screen 3 - Settings Submenu: Battery Profiles
    // ----------------------------------------------------
    // Back to root, move down to Battery Profiles, enter
    Renderer.handleMenuNav(NAV_BTN1_LONG, t);    // back to root
    Renderer.handleMenuNav(NAV_BTN2_SHORT, t);   // move down to 3. BATTERY PROFILES
    Renderer.handleMenuNav(NAV_BOTH_PRESSED, t); // enter Battery Profiles submenu
    renderAndSave("12_settings_battery_profiles", t);

    printf("\n[SUCCESS] All 12 scenarios rendered and saved to tools/simulator/screenshots/\n");
    return 0;
}

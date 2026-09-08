#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "board_config.h"
#include "display_driver.h"
#include "settings_manager.h"
#include "battery_manager.h"
#include <assert.h>
#include "dash_renderer.h"
#include "buttons.h"

MockSerial Serial;
DisplayDriver Display;
extern DashboardRenderer Renderer;
extern ButtonHandler Buttons;

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

void testButtonDebouncing() {
    printf("\n=== Running Button Debounce & Edit Mode Unit Tests ===\n");
    Buttons.begin();

    auto setPin = [&](int pin, int state) {
        simSetPin(pin, state);
        Buttons.update();
    };

    // 1. Initial State: All released (HIGH)
    setPin(PIN_BTN_1, HIGH);
    setPin(PIN_BTN_2, HIGH);
    for (int i = 0; i < 5; i++) {
        simAdvanceMs(10);
        Buttons.update();
    }
    assert(Buttons.getNavAction() == NAV_NONE);
    assert(!Buttons.isBtn1Down());
    assert(!Buttons.isBtn2Down());
    printf("[PASS] Buttons idle at boot.\n");

    // 2. Simulate Bouncy Click on BTN2 in Edit Mode
    Buttons.setEditMode(true);
    // Note: setEditMode requires full release first (_edit_wait_release)
    simAdvanceMs(10);
    Buttons.update();

    int dec_count = 0;
    int other_count = 0;

    auto stepAndCheck = [&](uint32_t step_ms) {
        simAdvanceMs(step_ms);
        Buttons.update();
        NavAction act = Buttons.getNavAction();
        if (act == NAV_EDIT_DEC) dec_count++;
        else if (act != NAV_NONE) other_count++;
    };

    // Mechanical contact chatter (rapid bouncing between LOW and HIGH):
    setPin(PIN_BTN_2, LOW);
    stepAndCheck(2);
    setPin(PIN_BTN_2, HIGH);
    stepAndCheck(2);
    setPin(PIN_BTN_2, LOW);
    stepAndCheck(3);
    setPin(PIN_BTN_2, HIGH);
    stepAndCheck(3);
    setPin(PIN_BTN_2, LOW); // stable closed from here

    // During chatter (first 10ms), NO actions should fire!
    assert(dec_count == 0);
    assert(other_count == 0);

    // Advance 30ms (total 30ms stable) -> still under 35ms debounce threshold
    stepAndCheck(30);
    assert(dec_count == 0);

    // Advance 10ms (total 40ms stable > 35ms) -> should fire NAV_EDIT_DEC exactly once!
    stepAndCheck(10);
    assert(dec_count == 1);

    // Advance another 20ms (button still held, total hold ~60ms < 500ms auto-repeat)
    stepAndCheck(20);
    assert(dec_count == 1);

    // Now simulate release chatter:
    setPin(PIN_BTN_2, HIGH);
    stepAndCheck(2);
    setPin(PIN_BTN_2, LOW);
    stepAndCheck(2);
    setPin(PIN_BTN_2, HIGH); // stable open
    stepAndCheck(50);

    // Still should be exactly 1 action total! (No extra decrement on release or chatter)
    assert(dec_count == 1);
    assert(other_count == 0);
    assert(!Buttons.isBtn2Down());
    printf("[PASS] Single click on BTN2 with contact bounce fired exactly 1 NAV_EDIT_DEC (no skips)!\n");

    // 3. Test Auto-Repeat on Long Press (> 500ms) in Edit Mode
    dec_count = 0;
    setPin(PIN_BTN_2, LOW); // Press down
    stepAndCheck(40); // Debounce passes
    assert(dec_count == 1); // 1st tap immediately

    // Hold for 400ms (total 440ms < 500ms) -> no repeat yet
    for (int i = 0; i < 8; i++) {
        stepAndCheck(50);
    }
    assert(dec_count == 1);

    // Reach 500ms+ -> repeats should start firing at ~160ms intervals
    stepAndCheck(100); // now at 500ms hold
    assert(dec_count == 2); // First repeat fired!

    stepAndCheck(160);
    assert(dec_count == 3);

    // Release
    setPin(PIN_BTN_2, HIGH);
    stepAndCheck(50);
    assert(!Buttons.isBtn2Down());
    printf("[PASS] Long press auto-repeat initiates strictly after 500ms hold.\n");

    // 4. Test Normal Mode: Click on BTN1 gives NAV_BTN1_SHORT on release
    Buttons.setEditMode(false);
    simAdvanceMs(50);
    Buttons.update();

    int btn1_short = 0, btn1_long = 0;
    NavAction last_act = NAV_NONE;
    auto stepNormal = [&](uint32_t step_ms) {
        simAdvanceMs(step_ms);
        Buttons.update();
        last_act = Buttons.getNavAction();
        if (last_act == NAV_BTN1_SHORT) btn1_short++;
        else if (last_act == NAV_BTN1_LONG) btn1_long++;
    };

    setPin(PIN_BTN_1, LOW);
    stepNormal(40); // Debounced press
    assert(btn1_short == 0); // Not fired on press in normal mode!
    assert(btn1_long == 0);
    stepNormal(100); // 140ms hold (< 500ms)
    setPin(PIN_BTN_1, HIGH); // Release
    stepNormal(40); // Debounced release
    assert(btn1_short == 1); // Fired once on release!
    assert(btn1_long == 0);
    printf("[PASS] Normal mode generates NAV_BTN1_SHORT exactly once on release.\n");

    // 5. Test Dual-Press (Chord: BTN1 + BTN2)
    setPin(PIN_BTN_1, LOW);
    setPin(PIN_BTN_2, LOW);
    stepNormal(40);
    assert(last_act == NAV_BOTH_PRESSED);

    // Release BTN1 first, keep BTN2 down
    setPin(PIN_BTN_1, HIGH);
    stepNormal(40);
    assert(last_act == NAV_NONE); // Chord locked! No accidental single click!

    // Release BTN2
    setPin(PIN_BTN_2, HIGH);
    stepNormal(40);
    assert(last_act == NAV_NONE); // Still no accidental click on chord exit!
    printf("[PASS] Dual-press chord lock prevents spurious clicks upon asymmetric finger release.\n");
    printf("=== All Button Debounce Tests PASSED! ===\n\n");
}

int main(int argc, char **argv) {
    printf("==================================================\n");
    printf("   VESC 3.16 Display Native Emulator & Renderer   \n");
    printf("==================================================\n");

    testButtonDebouncing();

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

#include <Arduino.h>
#include "board_config.h"
#include "dash_types.h"
#include "display_driver.h"
#include "dash_renderer.h"
#include "settings_manager.h"
#include "battery_manager.h"
#include "buttons.h"
#include "vesc_uart.h"

// Active Telemetry State
static DashTelemetry telemetry;

void setup() {
    // 1. Initialize USB CDC Serial Monitor
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=============================================");
    Serial.println(" Waveshare ESP32-S3-LCD-3.16 eBike Dashboard ");
    Serial.println(" Clean OEM Instrument Cluster UI (820x320)");
    Serial.println(" Target: Flipsky 75100 V1 (VESC UART)");
    Serial.println(" Motor: Bafang BBS Mid-Drive (No Regen)");
    Serial.println("=============================================");

    // 2. Initialize Persistent Settings
    Settings.begin();
    Serial.printf("[OK] Settings loaded: MaxBat=%dA, MaxPhase=%dA, FW=%dA, Style=%d\n",
                  Settings.get().max_battery_amps,
                  Settings.get().max_phase_amps,
                  Settings.get().field_weak_amps,
                  Settings.get().dash_style);

    // 3. Initialize Battery Profile & Health Learning System
    Battery.begin();
    Serial.printf("[OK] Battery Manager initialized: Active Profile %d (%s)\n",
                  Battery.getActiveProfileIndex(),
                  Battery.getProfile(Battery.getActiveProfileIndex()).name);

    // 4. Initialize Telemetry State
    telemetry.speed_kmh           = 0.0f;
    telemetry.duty_cycle_pct      = 0.0f;
    telemetry.rpm                 = 0.0f;
    telemetry.throttle_pct        = 0.0f;
    telemetry.voltage             = 53.8f;
    telemetry.current_amps        = 0.0f;
    telemetry.phase_amps          = 0.0f;
    telemetry.power_watts         = 0.0f;
    telemetry.amphours_used       = 0.0f;
    telemetry.battery_profile_id  = Battery.getActiveProfileIndex();
    telemetry.battery_pct         = Battery.getFusedSocPct();
    telemetry.remaining_wh        = Battery.getRemainingWh();
    telemetry.battery_health_soh  = Battery.getProfile(Battery.getActiveProfileIndex()).health_soh_pct;
    telemetry.est_range_km        = Battery.getEstRangeKm();
    telemetry.temp_motor          = 36.0f;
    telemetry.temp_esc            = 31.0f;
    telemetry.trip_km             = 0.0f;
    telemetry.odo_km              = 142.0f;
    telemetry.stats.reset();
    telemetry.screen              = SCREEN_RIDE_DASH;
    telemetry.vesc_connected      = false;
    telemetry.uptime_sec          = 0;

    // 5. Initialize Physical Navigation Buttons (SCL=GPIO 7, SDA=GPIO 15)
    Buttons.begin();
    Serial.println("[OK] Navigation Buttons initialized on SCL (GPIO 7) and SDA (GPIO 15)");

    // 6. Initialize ST7701 Display and Backlight
    Serial.println("[INFO] Initializing ST7701 Display Driver...");
    if (!Display.init()) {
        Serial.println("[ERROR] Display initialization failed!");
    } else {
        Serial.println("[OK] Display initialized with Octal PSRAM frame buffer (820x320 landscape).");
    }
    uint8_t br = (uint8_t)map(Settings.get().brightness_pct, 0, 100, 0, 255);
    Display.setBrightness(br);

    // 7. Initialize Renderer
    Renderer.begin(&Display);
    if (Settings.get().dash_style == STYLE_ANALOG_DIAL) {
        Renderer.triggerNeedleSweep();
    }

    // 8. Initialize VESC UART or Simulation Engine
    Vesc.begin();
#if SIMULATION_MODE
    Serial.println("[MODE] Self-Test Simulation Mode Active.");
    Serial.println("       Set SIMULATION_MODE in include/board_config.h to 0 for live Flipsky 75100 UART.");
#else
    Serial.println("[MODE] Live Flipsky 75100 VESC UART Mode Active (TX: GPIO 43, RX: GPIO 44).");
#endif

    Serial.println("[OK] Setup complete. Entering main render loop.");
}

void loop() {
    static uint32_t last_render_ms = 0;
    static uint32_t last_log_ms = 0;
    static uint32_t boot_ms = millis();
    uint32_t now = millis();

    // 1. Process Physical Button Inputs
    Buttons.update();
    NavAction nav = Buttons.getNavAction();

    if (nav != NAV_NONE) {
        if (telemetry.screen == SCREEN_SETTINGS_MENU) {
            Renderer.handleMenuNav(nav, telemetry);
        } else {
            switch (nav) {
                case NAV_BTN1_SHORT: // Cycle Screens: Ride Dash -> Trip Stats -> Settings Menu -> Ride Dash
                    telemetry.screen = (DashboardScreen)(((int)telemetry.screen + 1) % SCREEN_COUNT);
                    if (telemetry.screen == SCREEN_RIDE_DASH && Settings.get().dash_style == STYLE_ANALOG_DIAL) {
                        Renderer.triggerNeedleSweep();
                    }
                    Serial.printf("[NAV] Switched to Screen ID: %d\n", (int)telemetry.screen);
                    break;

                case NAV_BTN2_SHORT: // Quick Toggle: Battery Profile (Profile 1 <-> Profile 2)
                    {
                        uint8_t next_p = (Settings.get().active_battery_profile == 0) ? 1 : 0;
                        Settings.get().active_battery_profile = next_p;
                        Battery.setActiveProfile(next_p);
                        Settings.save();
                        Serial.printf("[NAV] Fast Toggled Battery Profile: %d (%s)\n", next_p, Battery.getProfile(next_p).name);
                    }
                    break;

                case NAV_BTN1_LONG: // Button 1 Long Press: Needle Sweep on Dash or Jump to Settings
                    if (telemetry.screen == SCREEN_RIDE_DASH && Settings.get().dash_style == STYLE_ANALOG_DIAL) {
                        Renderer.triggerNeedleSweep();
                        Serial.println("[NAV] Button 1 Long: Needle Sweep Triggered");
                    } else {
                        telemetry.screen = SCREEN_SETTINGS_MENU;
                        Serial.println("[NAV] Button 1 Long: Switched to Settings Menu");
                    }
                    break;

                case NAV_BTN2_LONG: // Button 2 Long Press: Reset Trip & Statistics
                    telemetry.trip_km = 0.0f;
                    telemetry.stats.reset();
                    Serial.println("[NAV] Button 2 Long: Trip Odometer and Statistics Reset to 0.0");
                    break;

                default:
                    break;
            }
        }
    }

    // 2. Update Telemetry (Simulation Physics or Live VESC UART packets)
    Vesc.update(telemetry);
    telemetry.uptime_sec = (now - boot_ms) / 1000;

    // 3. Render at smooth ~30 FPS rate
    if (now - last_render_ms >= 33) {
        last_render_ms = now;
        Renderer.render(telemetry);
    }

    // 4. Debug print over USB Serial every 2 seconds
    if (now - last_log_ms >= 2000) {
        last_log_ms = now;
        Serial.printf("[DASH] Screen: %d | Spd: %.1f km/h | Duty: %.1f%% | Bat: %.1fV (%.0f%%) | Cur: %.1fA | Pha: %.1fA | Est: %.1fkm\n",
                      (int)telemetry.screen, telemetry.speed_kmh, telemetry.duty_cycle_pct, telemetry.voltage,
                      telemetry.battery_pct, telemetry.current_amps, telemetry.phase_amps, telemetry.est_range_km);
    }
}


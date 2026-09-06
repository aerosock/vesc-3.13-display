#include <Arduino.h>
#include "board_config.h"
#include "dash_types.h"
#include "display_driver.h"
#include "dash_renderer.h"
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
    Serial.println("=============================================");

    // 2. Initialize Telemetry State
    telemetry.speed_kmh      = 0.0f;
    telemetry.rpm            = 0.0f;
    telemetry.throttle_pct   = 0.0f;
    telemetry.voltage        = 54.2f;
    telemetry.current_amps   = 0.0f;
    telemetry.power_watts    = 0.0f;
    telemetry.battery_pct    = 88.0f;
    telemetry.amphours_used  = 0.0f;
    telemetry.temp_motor     = 42.0f;
    telemetry.temp_esc       = 36.0f;
    telemetry.trip_km        = 14.2f;
    telemetry.odo_km         = 328.0f;
    telemetry.mode           = MODE_SPORT;
    telemetry.screen         = SCREEN_HORIZONTAL_TACH; // Default Screen 1
    telemetry.vesc_connected = false;
    telemetry.uptime_sec     = 0;

    // 3. Initialize Physical Navigation Buttons (SCL=GPIO 7, SDA=GPIO 15)
    Buttons.begin();
    Serial.println("[OK] Navigation Buttons initialized on SCL (GPIO 7) and SDA (GPIO 15)");

    // 4. Initialize ST7701 Display and Backlight
    Serial.println("[INFO] Initializing ST7701 Display Driver...");
    if (!Display.init()) {
        Serial.println("[ERROR] Display initialization failed!");
    } else {
        Serial.println("[OK] Display initialized with Octal PSRAM frame buffer (820x320 landscape).");
    }
    Display.setBrightness(225); // 0 - 255

    // 5. Initialize Renderer
    Renderer.begin(&Display);

    // 6. Initialize VESC UART or Simulation Engine
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

    // 1. Process Physical Button Inputs with Modular Navigation Actions
    Buttons.update();
    NavAction nav = Buttons.getNavAction();

    if (nav != NAV_NONE) {
        switch (nav) {
            case NAV_PREV: // Button 1 Short Press: Cycle Screens / Views
                telemetry.screen = (DashboardScreen)(((int)telemetry.screen + 1) % SCREEN_COUNT);
                if (telemetry.screen == SCREEN_R1_ANALOG_TACH) {
                    Renderer.triggerR1Sweep(); // Trigger R1 needle sweep on entering analog screen
                }
                Serial.printf("[NAV] Switched to Screen ID: %d\n", (int)telemetry.screen);
                break;

            case NAV_NEXT: // Button 2 Short Press: Cycle Ride Modes
                telemetry.mode = (RidingMode)(((int)telemetry.mode + 1) % MODE_COUNT);
                {
                    const char *mNames[] = { "ECO", "STANDARD", "SPORT", "BOOST" };
                    Serial.printf("[NAV] Riding Mode: %s\n", mNames[(int)telemetry.mode]);
                }
                break;

            case NAV_MENU_BACK: // Button 1 Long Press: Re-sweep or Back
                Serial.println("[NAV] Button 1 Long Press: Triggering Needle Sweep / Menu Back");
                Renderer.triggerR1Sweep();
                break;

            case NAV_SELECT_ENTER: // Button 2 Long Press: Reset Trip / Enter
                telemetry.trip_km = 0.0f;
                Serial.println("[NAV] Button 2 Long Press: Trip Odometer Reset to 0.0 km");
                break;

            default:
                break;
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
        Serial.printf("[DASH] Screen: %d | Speed: %.1f km/h | RPM: %.0f | Batt: %.1fV (%.0f%%) | Amps: %.1fA | MotTemp: %.1fC\n",
                      (int)telemetry.screen, telemetry.speed_kmh, telemetry.rpm, telemetry.voltage, telemetry.battery_pct, telemetry.current_amps, telemetry.temp_motor);
    }
}

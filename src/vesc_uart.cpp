#include "vesc_uart.h"
#include "settings_manager.h"
#include "battery_manager.h"
#include <math.h>

VescHandler Vesc;

// Standard CRC-CCITT for VESC packets
static uint16_t crc16(const uint8_t *buf, uint32_t len) {
    uint16_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc = (uint8_t)(crc >> 8) | (crc << 8);
        crc ^= buf[i];
        crc ^= (uint8_t)(crc & 0xff) >> 4;
        crc ^= (crc << 8) << 4;
        crc ^= ((crc & 0xff) << 4) << 1;
    }
    return crc;
}

VescHandler::VescHandler()
    : _pole_pairs(15),
      _wheel_diameter_mm(660.0f),
      _gear_ratio(1.0f),
      _sim_throttle(0.0f),
      _target_speed(0.0f),
      _target_rpm(0.0f),
      _last_poll_ms(0),
      _sim_last_update_ms(0) {}

void VescHandler::begin() {
    VESC_UART_PORT.begin(VESC_UART_BAUDRATE, SERIAL_8N1, VESC_UART_RX_PIN, VESC_UART_TX_PIN);
    _sim_last_update_ms = millis();
    _last_poll_ms = millis();
}

void VescHandler::setSimThrottle(float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    _sim_throttle = pct;
}

void VescHandler::update(DashTelemetry &telemetry) {
#if SIMULATION_MODE
    runSimulation(telemetry);
#else
    pollRealVesc(telemetry);
#endif
}

// ==============================================================================
// Realistic eBike Telemetry Simulation Engine (Bafang Mid-Drive Profile)
// ==============================================================================
void VescHandler::runSimulation(DashTelemetry &telemetry) {
    uint32_t now = millis();
    float dt = (now - _sim_last_update_ms) / 1000.0f;
    if (dt <= 0.001f) return;
    _sim_last_update_ms = now;

    // Automated 45-second dynamic riding cycle
    uint32_t cycle_ms = now % 45000;
    float sim_t = 0.0f;

    if (cycle_ms < 4000) {
        // Stopped at intersection
        sim_t = 0.0f;
    } else if (cycle_ms < 14000) {
        // Acceleration phase (heavy torque demand)
        float p = (cycle_ms - 4000) / 10000.0f;
        sim_t = p * 75.0f;
    } else if (cycle_ms < 22000) {
        // Steady cruise at ~35 km/h
        sim_t = 40.0f;
    } else if (cycle_ms < 34000) {
        // Hard sprint into Field Weakening territory (>100% duty)
        float p = (cycle_ms - 22000) / 12000.0f;
        sim_t = 40.0f + p * 58.0f; // up to 98% throttle
    } else {
        // Coasting down to stop (BBS mid-drive freewheeling, no regen)
        sim_t = 0.0f;
    }

    _sim_throttle = sim_t;
    telemetry.throttle_pct = _sim_throttle;

    // 1. Target Speed and Duty Cycle calculation
    float max_possible_speed = 62.0f;
    uint8_t speed_limit = Settings.get().max_speed_kmh;
    if (speed_limit > 0 && max_possible_speed > (float)speed_limit) {
        max_possible_speed = (float)speed_limit;
    }

    _target_speed = (_sim_throttle / 100.0f) * max_possible_speed;
    telemetry.speed_kmh += (_target_speed - telemetry.speed_kmh) * (dt * 1.6f);
    if (telemetry.speed_kmh < 0.2f) telemetry.speed_kmh = 0.0f;

    // 2. Realistic Duty Cycle calculation (0.0% to 120.0%)
    // Base duty cycle tracks vehicle speed / back-EMF headroom
    float base_duty = (telemetry.speed_kmh / 48.0f) * 92.0f;
    if (_sim_throttle > 85.0f && telemetry.speed_kmh > 46.0f) {
        // Field Weakening engaged: duty cycle pushes beyond 100% (up to 114%)
        float fw_extra = ((_sim_throttle - 85.0f) / 15.0f) * 18.0f;
        telemetry.duty_cycle_pct = 96.0f + fw_extra;
    } else {
        telemetry.duty_cycle_pct = base_duty;
    }
    if (telemetry.duty_cycle_pct > 118.0f) telemetry.duty_cycle_pct = 118.0f;
    if (telemetry.duty_cycle_pct < 0.0f) telemetry.duty_cycle_pct = 0.0f;

    // RPM equivalent for BBS mid-drive (crank/motor cadence)
    telemetry.rpm = (telemetry.speed_kmh / 45.0f) * 130.0f * 35.0f; // ~4500 motor RPM

    // 3. Current & Phase Amps (Bafang Mid-Drive tuning)
    uint8_t max_bat_a = Settings.get().max_battery_amps; // e.g. 28A
    uint8_t max_pha_a = Settings.get().max_phase_amps;   // e.g. 55A

    float target_bat_amps = (_sim_throttle / 100.0f) * (float)max_bat_a;
    telemetry.current_amps += (target_bat_amps - telemetry.current_amps) * (dt * 3.5f);
    if (telemetry.current_amps < 0.0f) telemetry.current_amps = 0.0f;

    // Phase current is higher during low-speed high-torque launches
    float duty_fraction = telemetry.duty_cycle_pct / 100.0f;
    if (duty_fraction < 0.35f) duty_fraction = 0.35f;
    float calculated_phase = telemetry.current_amps / duty_fraction;
    if (calculated_phase > (float)max_pha_a) calculated_phase = (float)max_pha_a;
    telemetry.phase_amps = calculated_phase;

    // 4. Battery Voltage & Power
    // Nominal 53.8V for 14S pack, minor IR sag under load (~0.05 ohm internal resistance)
    telemetry.voltage = 53.8f - (telemetry.current_amps * 0.048f);
    telemetry.power_watts = telemetry.voltage * telemetry.current_amps;

    // 5. Update Smart Battery SoC, Health & Range Learning
    Battery.update(telemetry, dt);

    // 6. Thermals: gradual warming under high phase amps
    if (telemetry.phase_amps > 30.0f) {
        telemetry.temp_motor += 0.35f * dt;
        telemetry.temp_esc   += 0.18f * dt;
    } else {
        telemetry.temp_motor -= 0.08f * dt;
        telemetry.temp_esc   -= 0.08f * dt;
    }
    if (telemetry.temp_motor < 36.0f) telemetry.temp_motor = 36.0f;
    if (telemetry.temp_esc < 31.0f)   telemetry.temp_esc = 31.0f;

    // 7. Distance & Odometry
    float distance_delta = (telemetry.speed_kmh / 3600.0f) * dt;
    telemetry.trip_km += distance_delta;
    telemetry.odo_km  += distance_delta;

    // 8. Statistics Tracking
    if (telemetry.current_amps > telemetry.stats.peak_current_amps) {
        telemetry.stats.peak_current_amps = telemetry.current_amps;
    }
    if (telemetry.phase_amps > telemetry.stats.peak_phase_amps) {
        telemetry.stats.peak_phase_amps = telemetry.phase_amps;
    }
    if (telemetry.power_watts > telemetry.stats.peak_power_watts) {
        telemetry.stats.peak_power_watts = telemetry.power_watts;
    }
    if (telemetry.speed_kmh > telemetry.stats.max_speed_kmh) {
        telemetry.stats.max_speed_kmh = telemetry.speed_kmh;
    }
    if (telemetry.temp_motor > telemetry.stats.max_temp_motor) {
        telemetry.stats.max_temp_motor = telemetry.temp_motor;
    }
    if (telemetry.temp_esc > telemetry.stats.max_temp_esc) {
        telemetry.stats.max_temp_esc = telemetry.temp_esc;
    }
    if (telemetry.speed_kmh > 1.2f) {
        telemetry.stats.ride_time_sec += (uint32_t)(dt + 0.5f);
    }
    if (telemetry.stats.ride_time_sec > 4 && telemetry.trip_km > 0.05f) {
        telemetry.stats.avg_speed_kmh = telemetry.trip_km / (telemetry.stats.ride_time_sec / 3600.0f);
    }

    telemetry.vesc_connected = true;
}

// ==============================================================================
// Live Flipsky 75100 VESC UART Communication
// ==============================================================================
void VescHandler::pollRealVesc(DashTelemetry &telemetry) {
    uint32_t now = millis();
    static uint32_t last_update_time_ms = millis();
    float dt = (now - last_update_time_ms) / 1000.0f;
    last_update_time_ms = now;

    // Poll VESC at 20 Hz (every 50 ms)
    if (now - _last_poll_ms >= 50) {
        _last_poll_ms = now;
        sendVescGetValues();
    }

    // Process incoming packet from UART
    static uint8_t rx_buffer[256];
    static size_t rx_index = 0;
    static uint32_t last_rx_byte_ms = 0;

    while (VESC_UART_PORT.available()) {
        uint8_t b = VESC_UART_PORT.read();
        last_rx_byte_ms = now;

        if (rx_index == 0) {
            if (b == 0x02 || b == 0x03) { // Start byte
                rx_buffer[rx_index++] = b;
            }
        } else {
            if (rx_index < sizeof(rx_buffer)) {
                rx_buffer[rx_index++] = b;
                if (rx_buffer[0] == 0x02 && rx_index >= 4) {
                    uint8_t payload_len = rx_buffer[1];
                    if (rx_index == (size_t)(payload_len + 5)) {
                        if (rx_buffer[rx_index - 1] == 0x03) {
                            parseVescPacket(rx_buffer, rx_index, telemetry);
                        }
                        rx_index = 0;
                    }
                }
            } else {
                rx_index = 0;
            }
        }
    }

    // Timeout reset
    if (rx_index > 0 && (now - last_rx_byte_ms > 120)) {
        rx_index = 0;
        telemetry.vesc_connected = false;
    }

    // Update Battery State
    if (dt > 0.001f && dt < 1.0f) {
        Battery.update(telemetry, dt);
    }
}

void VescHandler::sendVescGetValues() {
    static const uint8_t req[] = { 0x02, 0x01, 0x04, 0x40, 0x84, 0x03 };
    VESC_UART_PORT.write(req, sizeof(req));
}

bool VescHandler::parseVescPacket(uint8_t *buffer, size_t len, DashTelemetry &telemetry) {
    if (len < 60) return false;
    uint8_t *payload = &buffer[2];

    if (payload[0] != 0x04) return false; // COMM_GET_VALUES

    uint16_t expected_crc = (buffer[len - 3] << 8) | buffer[len - 2];
    uint16_t calc_crc = crc16(payload, buffer[1]);
    if (expected_crc != calc_crc) return false;

    telemetry.vesc_connected = true;

    // Unpack big-endian payload
    int idx = 1;
    int16_t temp_mos = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int16_t temp_mot = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t current_mot = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    int32_t current_in  = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    idx += 8; // skip id, iq
    int16_t duty_now = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t erpm = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    int16_t v_in = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t ah_used = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    idx += 4; // skip ah_charged
    int32_t wh_used = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    idx += 4; // skip wh_charged
    int32_t tachometer = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;

    telemetry.temp_esc       = temp_mos / 10.0f;
    telemetry.temp_motor     = temp_mot / 10.0f;
    telemetry.phase_amps     = fabsf(current_mot / 100.0f);
    telemetry.current_amps   = fabsf(current_in / 100.0f);
    telemetry.duty_cycle_pct = fabsf((float)duty_now / 10.0f);
    telemetry.voltage        = v_in / 10.0f;
    telemetry.power_watts    = telemetry.voltage * telemetry.current_amps;
    telemetry.amphours_used  = ah_used / 10000.0f;

    // Convert ERPM to Vehicle Speed (km/h)
    int pole_pairs = Settings.get().motor_pole_pairs;
    if (pole_pairs < 2) pole_pairs = 15;
    float wheel_circ_m = (Settings.get().wheel_diameter_in * 0.0254f) * M_PI;
    float gear_ratio = Settings.get().gear_ratio;
    if (gear_ratio < 0.1f) gear_ratio = 1.0f;

    float mech_rpm = (float)erpm / (float)pole_pairs;
    telemetry.rpm = fabsf(mech_rpm);
    float wheel_rpm = mech_rpm / gear_ratio;
    telemetry.speed_kmh = fabsf((wheel_rpm * wheel_circ_m * 60.0f) / 1000.0f);

    // Odometer & Trip from tachometer step counts
    float revs = (float)tachometer / ((float)pole_pairs * 6.0f);
    telemetry.trip_km = fabsf((revs * wheel_circ_m) / (gear_ratio * 1000.0f));

    return true;
}

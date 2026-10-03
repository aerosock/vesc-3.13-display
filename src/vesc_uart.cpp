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

// Deserializes VESC IEEE-754 auto-float
static float buffer_get_float32_auto(const uint8_t *buffer, int32_t *index) {
    uint32_t res = ((uint32_t)buffer[*index] << 24) |
                   ((uint32_t)buffer[*index + 1] << 16) |
                   ((uint32_t)buffer[*index + 2] << 8) |
                   ((uint32_t)buffer[*index + 3]);
    *index += 4;

    int e = (res >> 23) & 0xFF;
    uint32_t sig_i = res & 0x7FFFFF;
    bool neg = (res & (1U << 31)) != 0;

    float sig = 0.0f;
    if (e != 0 || sig_i != 0) {
        sig = (float)sig_i / (8388608.0f * 2.0f) + 0.5f;
        e -= 126;
    }

    if (neg) {
        sig = -sig;
    }

    return ldexpf(sig, e);
}

static float _last_adc1_v = 0.0f;
static float _last_adc1_pct = 0.0f;
static float _last_adc2_v = 0.0f;
static float _last_adc2_pct = 0.0f;

VescHandler::VescHandler()
    : _pole_pairs(15),
      _wheel_diameter_mm(660.0f),
      _gear_ratio(1.0f),
      _sim_throttle(0.0f),
      _target_speed(0.0f),
      _target_rpm(0.0f),
      _last_poll_ms(0),
      _sim_last_update_ms(0),
      _bridge_active(false),
      _bridge_pc_to_vesc(0),
      _bridge_vesc_to_pc(0),
      _wifi_bridge_active(false),
      _wifi_client_connected(false),
      _bridge_wifi_to_vesc(0),
      _bridge_vesc_to_wifi(0),
      _sync_status(SYNC_IDLE),
      _mcconf_req_ms(0),
      _sync_ms(0) {
    _wifi_client_ip[0] = '\0';
}

void VescHandler::begin() {
    VESC_UART_PORT.setRxBufferSize(4096);
    VESC_UART_PORT.setTxBufferSize(4096);
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

    // Interleave polling at 25ms interval:
    // Phase 0: COMM_GET_VALUES (20Hz)
    // Phase 1: COMM_GET_DECODED_ADC (20Hz)
    static uint8_t poll_phase = 0;
    if (now - _last_poll_ms >= 25) {
        _last_poll_ms = now;
        if (poll_phase == 0) {
            sendVescGetValues();
            poll_phase = 1;
        } else {
            sendVescGetDecodedAdc();
            poll_phase = 0;
        }
    }

    // Process incoming packet from UART
    static uint8_t rx_buffer[1024];
    static size_t rx_index = 0;
    static uint32_t last_rx_byte_ms = 0;

    static uint32_t total_rx_bytes = 0;
    static uint32_t last_diag_ms = 0;

    while (VESC_UART_PORT.available()) {
        uint8_t b = VESC_UART_PORT.read();
        total_rx_bytes++;
        last_rx_byte_ms = now;

        if (rx_index == 0) {
            if (b == 0x02 || b == 0x03) { // Start byte
                rx_buffer[rx_index++] = b;
            }
        } else {
            if (rx_index < sizeof(rx_buffer)) {
                rx_buffer[rx_index++] = b;

                // Short packet (header 0x02, payload <= 256)
                if (rx_buffer[0] == 0x02 && rx_index >= 4) {
                    uint8_t payload_len = rx_buffer[1];
                    if (rx_index == (size_t)(payload_len + 5)) {
                        if (rx_buffer[rx_index - 1] == 0x03) {
                            parseVescPacket(rx_buffer, rx_index, telemetry);
                        }
                        rx_index = 0;
                    }
                }
                // Long packet (header 0x03, payload > 256, e.g. COMM_GET_MCCONF)
                else if (rx_buffer[0] == 0x03 && rx_index >= 5) {
                    uint16_t payload_len = ((uint16_t)rx_buffer[1] << 8) | rx_buffer[2];
                    if (rx_index == (size_t)(payload_len + 6)) {
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
    if (rx_index > 0 && (now - last_rx_byte_ms > 150)) {
        rx_index = 0;
        telemetry.vesc_connected = false;
    }

    // Check on-demand MCCONF pull timeout
    if (_sync_status == SYNC_REQUESTED && (now - _mcconf_req_ms > 2500)) {
        _sync_status = SYNC_FAILED;
        _sync_ms = now;
        Serial.println("[VESC_UART] COMM_GET_MCCONF request timed out (VESC off or disconnected).");
    }

    // Diagnostics every 2 seconds
    if (now - last_diag_ms >= 2000) {
        last_diag_ms = now;
        Serial.printf("[VESC_UART] Link: %s | V_in: %.1fV | Thr(ADC%d): %.1f%% [ADC1: %.2fV (%.1f%%) | ADC2: %.2fV (%.1f%%)]\n",
                      telemetry.vesc_connected ? "CONNECTED" : "NO_DATA",
                      telemetry.voltage,
                      Settings.get().throttle_adc_channel + 1,
                      telemetry.throttle_pct,
                      _last_adc1_v, _last_adc1_pct,
                      _last_adc2_v, _last_adc2_pct);
    }

    // Update Battery State
    if (dt > 0.001f && dt < 1.0f) {
        Battery.update(telemetry, dt);
    }
}

void VescHandler::sendVescGetValues() {
    // Request COMM_GET_VALUES_SETUP (0x2F / 47) which contains mc_interface_get_speed()
    static const uint8_t req[] = { 0x02, 0x01, 0x2F, 0xD5, 0x8D, 0x03 };
    VESC_UART_PORT.write(req, sizeof(req));
}

void VescHandler::sendVescGetDecodedAdc() {
    static const uint8_t req[] = { 0x02, 0x01, 0x20, 0x24, 0x62, 0x03 };
    VESC_UART_PORT.write(req, sizeof(req));
}

bool VescHandler::parseVescPacket(uint8_t *buffer, size_t len, DashTelemetry &telemetry) {
    if (len < 6) return false;

    uint8_t *payload;
    size_t payload_len;

    if (buffer[0] == 0x02) {
        payload = &buffer[2];
        payload_len = buffer[1];
    } else if (buffer[0] == 0x03) {
        payload = &buffer[3];
        payload_len = ((size_t)buffer[1] << 8) | buffer[2];
    } else {
        return false;
    }

    uint16_t expected_crc = ((uint16_t)buffer[len - 3] << 8) | buffer[len - 2];
    uint16_t calc_crc = crc16(payload, payload_len);
    if (expected_crc != calc_crc) {
        return false;
    }

    telemetry.vesc_connected = true;
    uint8_t cmd_id = payload[0];

    // 1. Decoded ADC Packet (COMM_GET_DECODED_ADC = 0x20 / 32)
    if (cmd_id == 0x20) {
        if (payload_len >= 5) {
            int32_t raw_level1 = (int32_t)(((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) | (uint32_t)payload[4]);
            int32_t raw_volt1  = (payload_len >= 9)  ? (int32_t)(((uint32_t)payload[5] << 24) | ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 8) | (uint32_t)payload[8]) : 0;
            int32_t raw_level2 = (payload_len >= 13) ? (int32_t)(((uint32_t)payload[9] << 24) | ((uint32_t)payload[10] << 16) | ((uint32_t)payload[11] << 8) | (uint32_t)payload[12]) : 0;
            int32_t raw_volt2  = (payload_len >= 17) ? (int32_t)(((uint32_t)payload[13] << 24) | ((uint32_t)payload[14] << 16) | ((uint32_t)payload[15] << 8) | (uint32_t)payload[16]) : 0;

            _last_adc1_v = raw_volt1 / 1000000.0f;
            _last_adc1_pct = (raw_level1 / 1000000.0f) * 100.0f;
            _last_adc2_v = raw_volt2 / 1000000.0f;
            _last_adc2_pct = (raw_level2 / 1000000.0f) * 100.0f;

            // Choose ADC1 (default 0) or ADC2 (1) based on settings
            int32_t chosen_level = (Settings.get().throttle_adc_channel == 1 && payload_len >= 13) ? raw_level2 : raw_level1;
            float pct = (chosen_level / 1000000.0f) * 100.0f;
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 100.0f) pct = 100.0f;
            telemetry.throttle_pct = pct;
        }
        return true;
    }

    // 2. Setup Telemetry Packet with True Ground Speed (COMM_GET_VALUES_SETUP = 0x2F / 47)
    if (cmd_id == 0x2F) {
        if (len < 50) return false;
        int idx = 1;
        int16_t temp_mos = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
        int16_t temp_mot = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
        int32_t current_mot = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        int32_t current_in  = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        int16_t duty_now = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
        int32_t erpm = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        int32_t speed_m_s = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        int16_t v_in = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
        idx += 2; // skip batt_level
        int32_t ah_used = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        idx += 4; // skip ah_charge_tot
        int32_t wh_used = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
        idx += 4; // skip wh_charge_tot
        int32_t dist_m = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;

        telemetry.temp_esc       = temp_mos / 10.0f;
        telemetry.temp_motor     = temp_mot / 10.0f;
        telemetry.phase_amps     = fabsf(current_mot / 100.0f);
        telemetry.current_amps   = fabsf(current_in / 100.0f);
        telemetry.duty_cycle_pct = fabsf((float)duty_now / 10.0f);
        int pole_pairs = Settings.get().motor_pole_pairs;
        if (pole_pairs < 2) pole_pairs = 15;
        telemetry.rpm            = fabsf((float)erpm / (float)pole_pairs);
        telemetry.voltage        = (v_in / 10.0f) + Settings.get().voltage_trim_v;
        if (telemetry.voltage < 0.0f) telemetry.voltage = 0.0f;
        telemetry.power_watts    = telemetry.voltage * telemetry.current_amps;
        telemetry.amphours_used  = ah_used / 10000.0f;

        // Ground speed from VESC (injected by Port 4 LispBM wheel sensor override)
        telemetry.speed_kmh      = fabsf((speed_m_s / 1000.0f) * 3.6f);
        telemetry.trip_km        = fabsf((dist_m / 1000.0f) / 1000.0f);
        return true;
    }

    // 3. Fallback: Standard Telemetry Packet (COMM_GET_VALUES = 0x04)
    if (cmd_id != 0x04) return false;
    if (len < 60) return false;

    // Unpack big-endian payload
    int idx = 1;
    int16_t temp_mos = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int16_t temp_mot = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t current_mot = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
    int32_t current_in  = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
    idx += 8; // skip id, iq
    int16_t duty_now = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t erpm = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
    int16_t v_in = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t ah_used = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
    idx += 4; // skip ah_charged
    int32_t wh_used = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;
    idx += 4; // skip wh_charged
    int32_t tachometer = (int32_t)(((uint32_t)payload[idx] << 24) | ((uint32_t)payload[idx + 1] << 16) | ((uint32_t)payload[idx + 2] << 8) | (uint32_t)payload[idx + 3]); idx += 4;

    telemetry.temp_esc       = temp_mos / 10.0f;
    telemetry.temp_motor     = temp_mot / 10.0f;
    telemetry.phase_amps     = fabsf(current_mot / 100.0f);
    telemetry.current_amps   = fabsf(current_in / 100.0f);
    telemetry.duty_cycle_pct = fabsf((float)duty_now / 10.0f);
    telemetry.voltage        = (v_in / 10.0f) + Settings.get().voltage_trim_v;
    if (telemetry.voltage < 0.0f) telemetry.voltage = 0.0f;
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

    // 4. Motor Configuration Packet (COMM_GET_MCCONF = 0x0E / 14)
    if (cmd_id == 0x0E) {
        if (payload_len >= 25) {
            int32_t ind = 5; // skip cmd_id (1) and MCCONF_SIGNATURE (4)
            ind += 4;        // skip pwm_mode, comm_mode, motor_type, sensor_mode
            float l_current_max    = buffer_get_float32_auto(payload, &ind);
            float l_current_min    = buffer_get_float32_auto(payload, &ind);
            float l_in_current_max = buffer_get_float32_auto(payload, &ind);
            float l_in_current_min = buffer_get_float32_auto(payload, &ind);

            if (l_current_max > 5.0f && l_current_max < 200.0f &&
                l_in_current_max > 5.0f && l_in_current_max < 150.0f) {
                
                DashSettings &s = Settings.get();
                s.max_phase_amps   = (uint8_t)roundf(fabsf(l_current_max));
                s.max_battery_amps = (uint8_t)roundf(fabsf(l_in_current_max));
                Settings.save();

                _sync_status = SYNC_SUCCESS;
                _sync_ms = millis();
                Serial.printf("[VESC_UART] Pulled MCCONF successfully: Phase=%dA, Bat=%dA (saved to flash)\n",
                              s.max_phase_amps, s.max_battery_amps);
            } else {
                _sync_status = SYNC_FAILED;
                _sync_ms = millis();
            }
        } else {
            _sync_status = SYNC_FAILED;
            _sync_ms = millis();
        }
        return true;
    }

    return true;
}

void VescHandler::requestMcconf() {
    static const uint8_t req[] = { 0x02, 0x01, 0x0E, 0xE1, 0xCE, 0x03 };
    VESC_UART_PORT.write(req, sizeof(req));
    _sync_status = SYNC_REQUESTED;
    _mcconf_req_ms = millis();
    Serial.println("[VESC_UART] Sent COMM_GET_MCCONF request to VESC.");
}

#if !defined(LGFX_LINUX_FB) && !defined(SIMULATOR)
#include <WiFi.h>
static WiFiServer _wifi_server(6510);
static WiFiClient _wifi_client;
#endif

// ==============================================================================
// Transparent USB CDC <-> UART Passthrough Bridge for VESC Tool
// ==============================================================================
void VescHandler::enterBridgeMode() {
    _bridge_active = true;
    _bridge_pc_to_vesc = 0;
    _bridge_vesc_to_pc = 0;

    // Ensure 4KB queues for high-volume configuration bursts (e.g. COMM_SET_MCCONF)
    Serial.setRxBufferSize(4096);
    Serial.setTxBufferSize(4096);
    VESC_UART_PORT.setRxBufferSize(4096);
    VESC_UART_PORT.setTxBufferSize(4096);

    // Allow in-flight telemetry frames to conclude, then flush both pipelines
    delay(30);
    while (Serial.available()) Serial.read();
    while (VESC_UART_PORT.available()) VESC_UART_PORT.read();
}

void VescHandler::exitBridgeMode() {
    _bridge_active = false;
    _last_poll_ms = millis();
}

void VescHandler::updateUsbBridge() {
    if (!_bridge_active) return;

    // 1. Forward USB CDC (PC / VESC Tool) -> Flipsky 75100 VESC UART
    while (Serial.available() > 0) {
        uint8_t buf[512];
        size_t n = Serial.read(buf, sizeof(buf));
        if (n > 0) {
            VESC_UART_PORT.write(buf, n);
            _bridge_pc_to_vesc += n;
        } else {
            break;
        }
    }

    // 2. Forward Flipsky 75100 VESC UART -> USB CDC (PC / VESC Tool)
    while (VESC_UART_PORT.available() > 0) {
        uint8_t buf[512];
        size_t n = VESC_UART_PORT.read(buf, sizeof(buf));
        if (n > 0) {
            Serial.write(buf, n);
            _bridge_vesc_to_pc += n;
        } else {
            break;
        }
    }
}

// ==============================================================================
// Transparent Wireless Wi-Fi TCP <-> UART Passthrough Bridge for VESC Tool
// ==============================================================================
void VescHandler::enterWifiBridgeMode() {
    _wifi_bridge_active = true;
    _wifi_client_connected = false;
    _wifi_client_ip[0] = '\0';
    _bridge_wifi_to_vesc = 0;
    _bridge_vesc_to_wifi = 0;

    // Ensure 4KB UART queues for high-volume configuration bursts
    VESC_UART_PORT.setRxBufferSize(4096);
    VESC_UART_PORT.setTxBufferSize(4096);

    // Allow in-flight telemetry to conclude, then flush buffers
    delay(30);
    while (Serial.available()) Serial.read();
    while (VESC_UART_PORT.available()) VESC_UART_PORT.read();

#if !defined(LGFX_LINUX_FB) && !defined(SIMULATOR)
    WiFi.mode(WIFI_AP);
    WiFi.softAP("VESC-DASH-AP");
    _wifi_server.begin(6510);
    _wifi_server.setNoDelay(true);
    Serial.println("[WIFI_BRIDGE] SoftAP started: SSID='VESC-DASH-AP', Port=6510");
#endif
}

void VescHandler::exitWifiBridgeMode() {
    _wifi_bridge_active = false;
    _wifi_client_connected = false;
    _wifi_client_ip[0] = '\0';
    _last_poll_ms = millis();

#if !defined(LGFX_LINUX_FB) && !defined(SIMULATOR)
    if (_wifi_client) {
        _wifi_client.stop();
    }
    _wifi_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI_BRIDGE] Wi-Fi powered down completely.");
#endif
}

void VescHandler::updateWifiBridge() {
#if !defined(LGFX_LINUX_FB) && !defined(SIMULATOR)
    if (!_wifi_bridge_active) return;

    if (_wifi_server.hasClient()) {
        if (_wifi_client && _wifi_client.connected()) {
            WiFiClient rej = _wifi_server.available();
            rej.stop();
        } else {
            _wifi_client = _wifi_server.available();
            _wifi_client.setNoDelay(true);
            _wifi_client_connected = true;
            IPAddress ip = _wifi_client.remoteIP();
            snprintf(_wifi_client_ip, sizeof(_wifi_client_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            Serial.printf("[WIFI_BRIDGE] Client connected: %s\n", _wifi_client_ip);
        }
    }

    if (_wifi_client && _wifi_client.connected()) {
        _wifi_client_connected = true;

        // 1. Forward TCP -> VESC UART (bulk read/write loop)
        while (_wifi_client.available() > 0) {
            uint8_t buf[512];
            int bytes_read = _wifi_client.read(buf, sizeof(buf));
            if (bytes_read > 0) {
                VESC_UART_PORT.write(buf, bytes_read);
                _bridge_wifi_to_vesc += bytes_read;
            } else {
                break;
            }
        }

        // 2. Forward VESC UART -> TCP (bulk read/write loop)
        while (VESC_UART_PORT.available() > 0) {
            uint8_t buf[512];
            size_t n = VESC_UART_PORT.read(buf, sizeof(buf));
            if (n > 0) {
                _wifi_client.write(buf, n);
                _bridge_vesc_to_wifi += n;
            } else {
                break;
            }
        }
    } else {
        if (_wifi_client_connected) {
            _wifi_client_connected = false;
            _wifi_client_ip[0] = '\0';
            Serial.println("[WIFI_BRIDGE] Client disconnected.");
        }
    }
#endif
}

void VescHandler::updateBridge() {
    if (_bridge_active) {
        updateUsbBridge();
    } else if (_wifi_bridge_active) {
        updateWifiBridge();
    }
}


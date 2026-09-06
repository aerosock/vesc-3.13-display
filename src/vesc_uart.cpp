#include "vesc_uart.h"

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
      _wheel_diameter_mm(660.0f), // 26" ebike wheel
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
// Realistic eBike Telemetry Simulation Engine
// ==============================================================================
void VescHandler::runSimulation(DashTelemetry &telemetry) {
    uint32_t now = millis();
    float dt = (now - _sim_last_update_ms) / 1000.0f;
    if (dt <= 0.001f) return;
    _sim_last_update_ms = now;

    // Automated ride cycle simulation (cycles every 40 seconds)
    uint32_t cycle_ms = now % 40000;
    float sim_t = 0.0f;

    if (cycle_ms < 5000) {
        // Idle at stop light
        sim_t = 0.0f;
    } else if (cycle_ms < 14000) {
        // Acceleration phase 1
        float p = (cycle_ms - 5000) / 9000.0f;
        sim_t = p * 60.0f;
    } else if (cycle_ms < 22000) {
        // Cruising at ~38 km/h
        sim_t = 45.0f;
    } else if (cycle_ms < 32000) {
        // Fast sprint up to ~65 km/h
        float p = (cycle_ms - 22000) / 10000.0f;
        sim_t = 45.0f + p * 50.0f; // up to 95% throttle
    } else {
        // Deceleration / Braking back to 0
        float p = (cycle_ms - 32000) / 8000.0f;
        sim_t = (1.0f - p) * 50.0f;
        if (sim_t < 0.0f) sim_t = 0.0f;
    }

    _sim_throttle = sim_t;
    telemetry.throttle_pct = _sim_throttle;

    // Target Speed and RPM based on throttle and riding mode
    float modeMultiplier = 1.0f;
    if (telemetry.mode == MODE_ECO) modeMultiplier = 0.55f;
    else if (telemetry.mode == MODE_STD) modeMultiplier = 0.80f;
    else if (telemetry.mode == MODE_SPORT) modeMultiplier = 1.0f;
    else if (telemetry.mode == MODE_BOOST) modeMultiplier = 1.15f;

    _target_speed = (_sim_throttle / 100.0f) * 68.0f * modeMultiplier;
    _target_rpm   = (_sim_throttle / 100.0f) * 9800.0f * modeMultiplier;

    // Smooth physics response
    telemetry.speed_kmh += (_target_speed - telemetry.speed_kmh) * (dt * 1.8f);
    telemetry.rpm       += (_target_rpm - telemetry.rpm) * (dt * 3.0f);

    if (telemetry.speed_kmh < 0.2f) telemetry.speed_kmh = 0.0f;
    if (telemetry.rpm < 20.0f) telemetry.rpm = 0.0f;

    // Current & Power (P = V * I)
    float baseAmps = (_sim_throttle / 100.0f) * 32.0f;
    telemetry.current_amps += (baseAmps - telemetry.current_amps) * (dt * 4.0f);
    if (telemetry.current_amps < 0.0f) telemetry.current_amps = 0.0f;

    // Battery voltage sag model (Nominal 54.2V for 13S/14S Li-ion, sags with current)
    telemetry.voltage = 54.2f - (telemetry.current_amps * 0.065f);
    telemetry.power_watts = telemetry.voltage * telemetry.current_amps;

    // State of charge percentage (realistic ~85% in simulation)
    telemetry.battery_pct = 88.0f - (telemetry.trip_km * 0.35f);
    if (telemetry.battery_pct < 5.0f) telemetry.battery_pct = 5.0f;
    if (telemetry.battery_pct > 100.0f) telemetry.battery_pct = 100.0f;

    // Thermals: warm up gradually under high power
    if (telemetry.current_amps > 15.0f) {
        telemetry.temp_motor += 0.4f * dt;
        telemetry.temp_esc   += 0.2f * dt;
    } else {
        telemetry.temp_motor -= 0.1f * dt;
        telemetry.temp_esc   -= 0.1f * dt;
    }
    if (telemetry.temp_motor < 38.0f) telemetry.temp_motor = 38.0f;
    if (telemetry.temp_esc < 32.0f) telemetry.temp_esc = 32.0f;

    // Odometer & Trip
    float distance_delta = (telemetry.speed_kmh / 3600.0f) * dt;
    telemetry.trip_km += distance_delta;
    telemetry.odo_km  += distance_delta;

    telemetry.vesc_connected = true;
}

// ==============================================================================
// Live Flipsky 75100 VESC UART Communication
// ==============================================================================
void VescHandler::pollRealVesc(DashTelemetry &telemetry) {
    uint32_t now = millis();
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
                // Check if short packet end byte received
                if (rx_buffer[0] == 0x02 && rx_index >= 4) {
                    uint8_t payload_len = rx_buffer[1];
                    if (rx_index == (size_t)(payload_len + 5)) { // 0x02 + len + payload + crc(2) + 0x03
                        if (rx_buffer[rx_index - 1] == 0x03) {
                            parseVescPacket(rx_buffer, rx_index, telemetry);
                        }
                        rx_index = 0;
                    }
                }
            } else {
                rx_index = 0; // Buffer overflow reset
            }
        }
    }

    // Timeout reset
    if (rx_index > 0 && (now - last_rx_byte_ms > 100)) {
        rx_index = 0;
        telemetry.vesc_connected = false;
    }
}

void VescHandler::sendVescGetValues() {
    // VESC COMM_GET_VALUES request packet:
    // 0x02 (start), 0x01 (len), 0x04 (COMM_GET_VALUES), 0x40, 0x84 (CRC16), 0x03 (stop)
    static const uint8_t req[] = { 0x02, 0x01, 0x04, 0x40, 0x84, 0x03 };
    VESC_UART_PORT.write(req, sizeof(req));
}

bool VescHandler::parseVescPacket(uint8_t *buffer, size_t len, DashTelemetry &telemetry) {
    if (len < 60) return false;
    uint8_t *payload = &buffer[2]; // Start after [0x02, len]

    if (payload[0] != 0x04) return false; // Must be COMM_GET_VALUES response

    // Verify CRC
    uint16_t expected_crc = (buffer[len - 3] << 8) | buffer[len - 2];
    uint16_t calc_crc = crc16(payload, buffer[1]);
    if (expected_crc != calc_crc) {
        return false;
    }

    telemetry.vesc_connected = true;

    // Unpack big-endian payload
    int idx = 1;
    int16_t temp_mos = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int16_t temp_mot = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t current_mot = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    int32_t current_in  = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    idx += 8; // skip id, iq
    idx += 2; // skip duty
    int32_t erpm = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    int16_t v_in = (int16_t)((payload[idx] << 8) | payload[idx + 1]); idx += 2;
    int32_t ah_used = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;
    idx += 4; // skip ah_charged
    idx += 8; // skip wh_used, wh_charged
    int32_t tachometer = (int32_t)((payload[idx] << 24) | (payload[idx + 1] << 16) | (payload[idx + 2] << 8) | payload[idx + 3]); idx += 4;

    telemetry.temp_esc     = temp_mos / 10.0f;
    telemetry.temp_motor   = temp_mot / 10.0f;
    telemetry.current_amps = current_in / 100.0f;
    telemetry.voltage      = v_in / 10.0f;
    telemetry.power_watts  = telemetry.voltage * telemetry.current_amps;
    telemetry.amphours_used = ah_used / 10000.0f;

    // Convert Electrical RPM to Mechanical RPM
    int pole_pairs = (_pole_pairs > 0) ? _pole_pairs : 15;
    telemetry.rpm = (float)abs(erpm) / pole_pairs;

    // Wheel speed calculation:
    // Speed (km/h) = (Mechanical RPM / Gear Ratio) * (pi * Diameter_m) * (60 / 1000)
    float wheel_rpm = telemetry.rpm / _gear_ratio;
    float circumference_m = (_wheel_diameter_mm * 3.14159f) / 1000.0f;
    telemetry.speed_kmh = (wheel_rpm * circumference_m * 60.0f) / 1000.0f;

    // Distance calculation from tachometer pulses
    // Each mechanical revolution = pole_pairs * 6 hall steps
    float revs = (float)abs(tachometer) / (pole_pairs * 6.0f);
    telemetry.trip_km = (revs * circumference_m) / 1000.0f;

    // Battery percentage estimate for 13S (48V nominal) or 14S (52V nominal)
    // Range ~ 42.0V (0%) to 58.8V (100%)
    float pct = ((telemetry.voltage - 42.0f) / (58.8f - 42.0f)) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    telemetry.battery_pct = pct;

    return true;
}

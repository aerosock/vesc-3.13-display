#include "battery_manager.h"
#include <string.h>

BatteryManager Battery;

BatteryManager::BatteryManager()
    : _active_profile_id(0),
      _fused_soc_pct(88.0f),
      _remaining_wh(1144.0f),
      _est_range_km(45.0f),
      _rest_start_ms(0),
      _is_resting(false),
      _cycle_start_v(54.2f),
      _cycle_wh_accum(0.0f),
      _tracking_full_cycle(false) {
    loadDefaults();
}

void BatteryManager::loadDefaults() {
    // Profile 1: Fresh 52V 25Ah Pack (~1300 Wh nominal)
    strncpy(_profiles[0].name, "B1 Fresh", sizeof(_profiles[0].name));
    _profiles[0].nominal_wh          = 1300.0f;
    _profiles[0].learned_wh          = 1280.0f; // ~98% Health
    _profiles[0].total_wh_discharged = 1450.0f;
    _profiles[0].charge_cycles       = 3;
    _profiles[0].health_soh_pct      = (_profiles[0].learned_wh / _profiles[0].nominal_wh) * 100.0f;

    // Profile 2: Workhorse / Daily 52V 25Ah Pack (higher wear)
    strncpy(_profiles[1].name, "B2 Daily", sizeof(_profiles[1].name));
    _profiles[1].nominal_wh          = 1300.0f;
    _profiles[1].learned_wh          = 1060.0f; // ~81% Health
    _profiles[1].total_wh_discharged = 18600.0f;
    _profiles[1].charge_cycles       = 145;
    _profiles[1].health_soh_pct      = (_profiles[1].learned_wh / _profiles[1].nominal_wh) * 100.0f;
}

void BatteryManager::begin() {
    _prefs.begin("ebike_bat", false);

    if (!_prefs.isKey("bat_init")) {
        saveProfiles();
        _prefs.putBool("bat_init", true);
        Serial.println("[BATTERY] Initialized default battery profiles.");
    } else {
        _active_profile_id = _prefs.getUChar("active_p", 0);
        if (_active_profile_id > 1) _active_profile_id = 0;

        for (int i = 0; i < 2; i++) {
            char k[16];
            snprintf(k, sizeof(k), "p%d_nom", i);
            _profiles[i].nominal_wh = _prefs.getFloat(k, 1300.0f);
            snprintf(k, sizeof(k), "p%d_lrn", i);
            _profiles[i].learned_wh = _prefs.getFloat(k, (i == 0) ? 1280.0f : 1060.0f);
            snprintf(k, sizeof(k), "p%d_tot", i);
            _profiles[i].total_wh_discharged = _prefs.getFloat(k, 0.0f);
            snprintf(k, sizeof(k), "p%d_cyc", i);
            _profiles[i].charge_cycles = _prefs.getUShort(k, 0);
            _profiles[i].health_soh_pct = (_profiles[i].learned_wh / _profiles[i].nominal_wh) * 100.0f;
        }
        Serial.printf("[BATTERY] Loaded profile %d (%s) - Health: %.1f%%\n",
                      _active_profile_id, _profiles[_active_profile_id].name,
                      _profiles[_active_profile_id].health_soh_pct);
    }
}

void BatteryManager::saveProfiles() {
    _prefs.putUChar("active_p", _active_profile_id);
    for (int i = 0; i < 2; i++) {
        char k[16];
        snprintf(k, sizeof(k), "p%d_nom", i);
        _prefs.putFloat(k, _profiles[i].nominal_wh);
        snprintf(k, sizeof(k), "p%d_lrn", i);
        _prefs.putFloat(k, _profiles[i].learned_wh);
        snprintf(k, sizeof(k), "p%d_tot", i);
        _prefs.putFloat(k, _profiles[i].total_wh_discharged);
        snprintf(k, sizeof(k), "p%d_cyc", i);
        _prefs.putUShort(k, _profiles[i].charge_cycles);
    }
}

void BatteryManager::setActiveProfile(uint8_t id) {
    if (id > 1) id = 0;
    _active_profile_id = id;
    saveProfiles();
    // Re-initialize state of charge from current OCV
    _fused_soc_pct = 85.0f;
    _remaining_wh = (_fused_soc_pct / 100.0f) * _profiles[_active_profile_id].learned_wh;
    Serial.printf("[BATTERY] Switched to %s (Learned Wh: %.0f, Health: %.1f%%)\n",
                  _profiles[_active_profile_id].name,
                  _profiles[_active_profile_id].learned_wh,
                  _profiles[_active_profile_id].health_soh_pct);
}

void BatteryManager::resetProfileLearning(uint8_t id) {
    if (id > 1) id = 0;
    _profiles[id].learned_wh = _profiles[id].nominal_wh;
    _profiles[id].health_soh_pct = 100.0f;
    saveProfiles();
}

// 14S Li-Ion (52V nominal) Open-Circuit Voltage table (58.8V max to 43.4V min)
float BatteryManager::getOcvSoc(float v_pack) {
    if (v_pack >= 58.0f) return 100.0f;
    if (v_pack >= 56.5f) return 90.0f + (v_pack - 56.5f) * (10.0f / 1.5f);
    if (v_pack >= 55.0f) return 80.0f + (v_pack - 55.0f) * (10.0f / 1.5f);
    if (v_pack >= 53.6f) return 70.0f + (v_pack - 53.6f) * (10.0f / 1.4f);
    if (v_pack >= 52.4f) return 60.0f + (v_pack - 52.4f) * (10.0f / 1.2f);
    if (v_pack >= 51.2f) return 50.0f + (v_pack - 51.2f) * (10.0f / 1.2f);
    if (v_pack >= 49.8f) return 40.0f + (v_pack - 49.8f) * (10.0f / 1.4f);
    if (v_pack >= 48.2f) return 30.0f + (v_pack - 48.2f) * (10.0f / 1.6f);
    if (v_pack >= 46.8f) return 20.0f + (v_pack - 46.8f) * (10.0f / 1.4f);
    if (v_pack >= 45.0f) return 10.0f + (v_pack - 45.0f) * (10.0f / 1.8f);
    if (v_pack >= 43.4f) return 0.0f  + (v_pack - 43.4f) * (10.0f / 1.6f);
    return 0.0f;
}

void BatteryManager::update(DashTelemetry &telemetry, float dt) {
    uint32_t now = millis();
    BatteryProfile &prof = _profiles[_active_profile_id];

    // 1. Compute instant power and energy delta
    float power_w = telemetry.voltage * telemetry.current_amps;
    float delta_wh = (power_w * (dt / 3600.0f));

    // 2. Track energy throughput
    if (delta_wh > 0.0f) {
        telemetry.stats.trip_wh += delta_wh;
        prof.total_wh_discharged += delta_wh;

        // Subtract from remaining Wh in current pack
        _remaining_wh -= delta_wh;
        if (_remaining_wh < 0.0f) _remaining_wh = 0.0f;
    }

    // 3. Resting OCV Detection: resting if current is near zero (|I| < 0.8A)
    if (fabsf(telemetry.current_amps) < 0.8f) {
        if (!_is_resting) {
            _is_resting = true;
            _rest_start_ms = now;
        }
    } else {
        _is_resting = false;
    }

    // 4. Hybrid SoC Filter
    // Coulomb-counting SoC estimate:
    float coulomb_soc = (_remaining_wh / prof.learned_wh) * 100.0f;

    if (_is_resting && (now - _rest_start_ms > 3000)) {
        // Resting for >3s: voltage has recovered, so OCV is reliable
        float ocv_soc = getOcvSoc(telemetry.voltage);
        // Gentle Kalman/complementary convergence (tau ~ 30s)
        _fused_soc_pct += (ocv_soc - _fused_soc_pct) * (dt * 0.05f);
        // Synchronize remaining Wh with converged SoC
        _remaining_wh = (_fused_soc_pct / 100.0f) * prof.learned_wh;
    } else {
        // Under throttle/load: follow Coulomb counter directly (immune to voltage sag)
        _fused_soc_pct = coulomb_soc;
    }

    if (_fused_soc_pct > 100.0f) _fused_soc_pct = 100.0f;
    if (_fused_soc_pct < 0.0f)   _fused_soc_pct = 0.0f;

    // 5. Projected Remaining Range (km)
    // Energy efficiency Wh/km:
    if (telemetry.trip_km > 0.4f && telemetry.stats.trip_wh > 1.0f) {
        telemetry.stats.trip_wh_km = telemetry.stats.trip_wh / telemetry.trip_km;
    } else {
        telemetry.stats.trip_wh_km = 22.0f; // Typical BBS mid-drive consumption
    }

    float eff = telemetry.stats.trip_wh_km;
    if (eff < 14.0f) eff = 14.0f; // Minimum realistic floor
    _est_range_km = _remaining_wh / eff;
    if (_est_range_km > 150.0f) _est_range_km = 150.0f;

    // 6. Self-Learning Battery Degradation / Full-Cycle Detection
    if (telemetry.voltage > 57.5f) {
        _tracking_full_cycle = true;
        _cycle_start_v = telemetry.voltage;
        _cycle_wh_accum = 0.0f;
    }

    if (_tracking_full_cycle) {
        if (delta_wh > 0.0f) _cycle_wh_accum += delta_wh;
        // When discharged down past 45.0V (~10% SoC), evaluate learned capacity
        if (telemetry.voltage < 45.0f && _cycle_wh_accum > (prof.learned_wh * 0.65f)) {
            _tracking_full_cycle = false;
            // Extrapolate full 100% capacity from the 100%->10% test
            float measured_full_wh = _cycle_wh_accum / 0.90f;
            // Exponential moving average: 85% previous, 15% new measurement
            prof.learned_wh = (prof.learned_wh * 0.85f) + (measured_full_wh * 0.15f);
            prof.charge_cycles++;
            prof.health_soh_pct = (prof.learned_wh / prof.nominal_wh) * 100.0f;
            saveProfiles();
            Serial.printf("[BATTERY] Updated health! Learned Wh: %.0f, Health: %.1f%%\n",
                          prof.learned_wh, prof.health_soh_pct);
        }
    }

    // 7. Push states into Telemetry struct
    telemetry.battery_profile_id = _active_profile_id;
    telemetry.battery_pct        = _fused_soc_pct;
    telemetry.remaining_wh       = _remaining_wh;
    telemetry.battery_health_soh = prof.health_soh_pct;
    telemetry.est_range_km       = _est_range_km;
}

#pragma once

#include <Arduino.h>
#include "board_config.h"
#include "dash_types.h"

enum VescSyncStatus {
    SYNC_IDLE = 0,
    SYNC_REQUESTED,
    SYNC_SUCCESS,
    SYNC_FAILED
};

class VescHandler {
public:
    VescHandler();
    void begin();
    void update(DashTelemetry &telemetry);

    // Manual simulation throttle override
    void setSimThrottle(float pct);
    float getSimThrottle() const { return _sim_throttle; }

    // Configuration
    void setMotorPolePairs(int poles) { _pole_pairs = poles; }
    void setWheelDiameterMm(float mm) { _wheel_diameter_mm = mm; }
    void setGearRatio(float ratio) { _gear_ratio = ratio; }

    // Transparent USB CDC <-> UART Bridge for VESC Tool
    void enterBridgeMode();
    void exitBridgeMode();
    bool isBridgeActive() const { return _bridge_active; }
    void updateBridge();
    uint32_t getBridgePcToVescBytes() const { return _bridge_pc_to_vesc; }
    uint32_t getBridgeVescToPcBytes() const { return _bridge_vesc_to_pc; }

    // On-Demand Settings Pull from VESC Controller
    void requestMcconf();
    VescSyncStatus getSyncStatus() const { return _sync_status; }
    uint32_t getSyncTimeMs() const { return _sync_ms; }
    void resetSyncStatus() { _sync_status = SYNC_IDLE; }

private:
    void runSimulation(DashTelemetry &telemetry);
    void pollRealVesc(DashTelemetry &telemetry);
    void sendVescGetValues();
    void sendVescGetDecodedAdc();
    bool parseVescPacket(uint8_t *buffer, size_t len, DashTelemetry &telemetry);

    // Telemetry & Ebike Physics Parameters
    int _pole_pairs;            // Standard ebike hub motor: usually 15 or 23 pole pairs
    float _wheel_diameter_mm;   // e.g., 26" wheel ~ 660mm, 20" ~ 500mm
    float _gear_ratio;          // 1.0 for direct-drive hub, or gear reduction ratio

    // Simulation Engine State
    float _sim_throttle;
    float _target_speed;
    float _target_rpm;
    uint32_t _last_poll_ms;
    uint32_t _sim_last_update_ms;

    // USB Bridge State
    bool     _bridge_active;
    uint32_t _bridge_pc_to_vesc;
    uint32_t _bridge_vesc_to_pc;

    // Configuration Pull State
    VescSyncStatus _sync_status;
    uint32_t       _mcconf_req_ms;
    uint32_t       _sync_ms;
};

extern VescHandler Vesc;

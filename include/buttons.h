#pragma once

#include <Arduino.h>
#include "board_config.h"
#include "dash_types.h"

class ButtonHandler {
public:
    ButtonHandler();
    void begin();
    void update();

    // Mode switch: changes button behavior between normal menu/dash navigation and value editing
    void setEditMode(bool in_edit);
    bool isInEditMode() const { return _in_edit_mode; }

    NavAction getNavAction();

    bool isBtn1Down() const { return _btn1.is_down; }
    bool isBtn2Down() const { return _btn2.is_down; }

private:
    struct ButtonState {
        uint8_t  pin;
        bool     raw_state;
        bool     is_down;
        uint32_t last_change_ms;
        uint32_t press_start_ms;
        uint32_t last_repeat_ms;
        bool     long_press_fired;
        bool     single_pending;   // Debounced press waiting in chord window
        uint32_t pending_since_ms; // When single press was confirmed debounced
    };

    ButtonState _btn1;
    ButtonState _btn2;
    NavAction   _pending_action;
    bool        _in_edit_mode;

    bool        _chord_active;     // Active from chord detection through release cooldown
    uint32_t    _all_up_since_ms;  // Timestamp since both buttons are continuously unpressed
    bool        _require_all_up;   // Lockout gate: must see both buttons released before accepting taps

    void processDebounce(ButtonState &btn, bool raw, uint32_t now);
};

extern ButtonHandler Buttons;

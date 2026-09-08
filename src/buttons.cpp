#include "buttons.h"

ButtonHandler Buttons;

static const uint32_t DEBOUNCE_TIME_MS = 35;

ButtonHandler::ButtonHandler()
    : _pending_action(NAV_NONE),
      _in_edit_mode(false),
      _chord_locked(false),
      _edit_wait_release(false) {
    _btn1 = { PIN_BTN_1, false, false, 0, 0, 0, false };
    _btn2 = { PIN_BTN_2, false, false, 0, 0, 0, false };
}

void ButtonHandler::begin() {
    pinMode(_btn1.pin, INPUT_PULLUP);
    pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonHandler::setEditMode(bool in_edit) {
    _in_edit_mode = in_edit;
    _edit_wait_release = true; // Must release buttons before accepting edit taps!
    _chord_locked = true;      // Must release buttons before accepting normal taps!
    _pending_action = NAV_NONE;
}

void ButtonHandler::update() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // 1. Debounce BTN1 (SCL / GPIO 7)
    if (raw1 != _btn1.raw_state) {
        _btn1.raw_state = raw1;
        _btn1.last_change_ms = now;
    }
    bool btn1_just_pressed = false;
    bool btn1_just_released = false;
    if ((now - _btn1.last_change_ms) >= DEBOUNCE_TIME_MS) {
        if (_btn1.is_down != _btn1.raw_state) {
            _btn1.is_down = _btn1.raw_state;
            if (_btn1.is_down) {
                btn1_just_pressed = true;
                _btn1.press_start_ms = now;
                _btn1.last_repeat_ms = now;
                _btn1.long_press_fired = false;
            } else {
                btn1_just_released = true;
            }
        }
    }

    // 2. Debounce BTN2 (SDA / GPIO 15)
    if (raw2 != _btn2.raw_state) {
        _btn2.raw_state = raw2;
        _btn2.last_change_ms = now;
    }
    bool btn2_just_pressed = false;
    bool btn2_just_released = false;
    if ((now - _btn2.last_change_ms) >= DEBOUNCE_TIME_MS) {
        if (_btn2.is_down != _btn2.raw_state) {
            _btn2.is_down = _btn2.raw_state;
            if (_btn2.is_down) {
                btn2_just_pressed = true;
                _btn2.press_start_ms = now;
                _btn2.last_repeat_ms = now;
                _btn2.long_press_fired = false;
            } else {
                btn2_just_released = true;
            }
        }
    }

    // 3. Clear Chord Locks & Release Gates when both buttons are fully released
    if (!_btn1.is_down && !_btn2.is_down) {
        _chord_locked = false;
        _edit_wait_release = false;
    }

    // If waiting for full release after entering edit mode or after a chord, ignore single presses
    if (_chord_locked || _edit_wait_release) {
        return;
    }

    // 4. Simultaneous Dual Press (Chord: BTN1 + BTN2)
    if (_btn1.is_down && _btn2.is_down) {
        _chord_locked = true;
        _btn1.long_press_fired = true;
        _btn2.long_press_fired = true;
        _pending_action = NAV_BOTH_PRESSED;
        return;
    }

    // 5. Mode-Specific Dispatch
    if (_in_edit_mode) {
        // --- EDIT MODE ---
        // BTN1 = UP (+), BTN2 = DOWN (-)
        // Single click: exactly 1 step on debounced press edge!
        if (btn1_just_pressed) {
            _pending_action = NAV_EDIT_INC;
        } else if (_btn1.is_down) {
            uint32_t hold_time = now - _btn1.press_start_ms;
            if (hold_time >= 500) {
                uint32_t interval = (hold_time > 2000) ? 60 : 160;
                if (now - _btn1.last_repeat_ms >= interval) {
                    _btn1.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_INC;
                }
            }
        }

        if (btn2_just_pressed) {
            _pending_action = NAV_EDIT_DEC;
        } else if (_btn2.is_down) {
            uint32_t hold_time = now - _btn2.press_start_ms;
            if (hold_time >= 500) {
                uint32_t interval = (hold_time > 2000) ? 60 : 160;
                if (now - _btn2.last_repeat_ms >= interval) {
                    _btn2.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_DEC;
                }
            }
        }
    } else {
        // --- NORMAL MODE ---
        // Single click: on release (if held < 500ms and not chorded)
        // Long click: when held >= 500ms

        // BTN1
        if (_btn1.is_down) {
            if (!_btn1.long_press_fired && (now - _btn1.press_start_ms >= 500)) {
                _btn1.long_press_fired = true;
                _pending_action = NAV_BTN1_LONG;
            }
        } else if (btn1_just_released) {
            if (!_btn1.long_press_fired) {
                _pending_action = NAV_BTN1_SHORT;
            }
        }

        // BTN2
        if (_btn2.is_down) {
            if (!_btn2.long_press_fired && (now - _btn2.press_start_ms >= 500)) {
                _btn2.long_press_fired = true;
                _pending_action = NAV_BTN2_LONG;
            }
        } else if (btn2_just_released) {
            if (!_btn2.long_press_fired) {
                _pending_action = NAV_BTN2_SHORT;
            }
        }
    }
}

NavAction ButtonHandler::getNavAction() {
    NavAction act = _pending_action;
    _pending_action = NAV_NONE;
    return act;
}

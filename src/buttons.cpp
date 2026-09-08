#include "buttons.h"

ButtonHandler Buttons;

ButtonHandler::ButtonHandler()
    : _pending_action(NAV_NONE),
      _in_edit_mode(false),
      _chord_locked(false),
      _edit_wait_release(false) {
    _btn1 = { PIN_BTN_1, false, 0, 0, false };
    _btn2 = { PIN_BTN_2, false, 0, 0, false };
}

void ButtonHandler::begin() {
    pinMode(_btn1.pin, INPUT_PULLUP);
    pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonHandler::setEditMode(bool in_edit) {
    _in_edit_mode = in_edit;
    if (in_edit) {
        _edit_wait_release = true; // Must release buttons before accepting edit taps!
    } else {
        _chord_locked = true;      // Must release buttons before accepting normal taps!
    }
    _btn1.is_down = false;
    _btn2.is_down = false;
}

void ButtonHandler::update() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // If both buttons are released, clear chord locks and release gates
    if (!raw1 && !raw2) {
        _chord_locked = false;
        _edit_wait_release = false;
    }

    // Both buttons pressed simultaneously -> Chord Action (SELECT / ENTER)
    if (raw1 && raw2) {
        if (!_chord_locked) {
            _chord_locked = true;
            _btn1.is_down = true;
            _btn2.is_down = true;
            _btn1.long_press_fired = true; // Suppress trailing single clicks
            _btn2.long_press_fired = true;
            _pending_action = NAV_BOTH_PRESSED;
        }
        return;
    }

    // If waiting for full release after a chord, ignore single button states
    if (_chord_locked) {
        return;
    }

    if (_in_edit_mode) {
        updateEditMode(raw1, raw2, now);
    } else {
        updateNormalMode(raw1, raw2, now);
    }
}

void ButtonHandler::updateNormalMode(bool raw1, bool raw2, uint32_t now) {
    // Button 1 (SCL / GPIO 7) - UP / PREV / (Hold: Open Settings)
    if (raw1) {
        if (!_btn1.is_down) {
            _btn1.is_down = true;
            _btn1.press_start_ms = now;
            _btn1.long_press_fired = false;
        } else {
            if (!_btn1.long_press_fired && (now - _btn1.press_start_ms >= 500)) {
                _btn1.long_press_fired = true;
                _pending_action = NAV_BTN1_LONG;
            }
        }
    } else {
        if (_btn1.is_down) {
            _btn1.is_down = false;
            uint32_t dur = now - _btn1.press_start_ms;
            if (dur >= 30 && !_btn1.long_press_fired && !_chord_locked) {
                _pending_action = NAV_BTN1_SHORT;
            }
        }
    }

    // Button 2 (SDA / GPIO 15) - DOWN / NEXT / (Hold: Exit / Trip Reset)
    if (raw2) {
        if (!_btn2.is_down) {
            _btn2.is_down = true;
            _btn2.press_start_ms = now;
            _btn2.long_press_fired = false;
        } else {
            if (!_btn2.long_press_fired && (now - _btn2.press_start_ms >= 500)) {
                _btn2.long_press_fired = true;
                _pending_action = NAV_BTN2_LONG;
            }
        }
    } else {
        if (_btn2.is_down) {
            _btn2.is_down = false;
            uint32_t dur = now - _btn2.press_start_ms;
            if (dur >= 30 && !_btn2.long_press_fired && !_chord_locked) {
                _pending_action = NAV_BTN2_SHORT;
            }
        }
    }
}

// In Edit Mode:
// BTN1 = UP / Increment (+), BTN2 = DOWN / Decrement (-)
// Single click = 1 step. Holding >= 500ms initiates repeat.
void ButtonHandler::updateEditMode(bool raw1, bool raw2, uint32_t now) {
    if (_edit_wait_release) {
        return; // Ignore inputs until user lifts fingers from previous enter press
    }

    // BTN1 (UP / Increment +)
    if (raw1) {
        if (!_btn1.is_down) {
            _btn1.is_down = true;
            _btn1.press_start_ms = now;
            _btn1.last_repeat_ms = now;
            _pending_action = NAV_EDIT_INC; // Step once immediately!
        } else {
            uint32_t hold_time = now - _btn1.press_start_ms;
            // 500ms initial hold delay before repeating starts
            if (hold_time >= 500) {
                uint32_t interval = (hold_time > 1800) ? 60 : 160;
                if (now - _btn1.last_repeat_ms >= interval) {
                    _btn1.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_INC;
                }
            }
        }
    } else {
        _btn1.is_down = false;
    }

    // BTN2 (DOWN / Decrement -)
    if (raw2) {
        if (!_btn2.is_down) {
            _btn2.is_down = true;
            _btn2.press_start_ms = now;
            _btn2.last_repeat_ms = now;
            _pending_action = NAV_EDIT_DEC; // Step once immediately!
        } else {
            uint32_t hold_time = now - _btn2.press_start_ms;
            // 500ms initial hold delay before repeating starts
            if (hold_time >= 500) {
                uint32_t interval = (hold_time > 1800) ? 60 : 160;
                if (now - _btn2.last_repeat_ms >= interval) {
                    _btn2.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_DEC;
                }
            }
        }
    } else {
        _btn2.is_down = false;
    }
}

NavAction ButtonHandler::getNavAction() {
    NavAction act = _pending_action;
    _pending_action = NAV_NONE;
    return act;
}

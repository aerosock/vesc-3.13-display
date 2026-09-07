#include "buttons.h"

ButtonHandler Buttons;

ButtonHandler::ButtonHandler()
    : _pending_action(NAV_NONE),
      _in_edit_mode(false) {
    _btn1 = { PIN_BTN_1, false, 0, 0, false };
    _btn2 = { PIN_BTN_2, false, 0, 0, false };
}

void ButtonHandler::begin() {
    pinMode(_btn1.pin, INPUT_PULLUP);
    pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonHandler::update() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // Both buttons pressed simultaneously
    if (raw1 && raw2) {
        if (!_btn1.is_down || !_btn2.is_down) {
            _btn1.is_down = true;
            _btn2.is_down = true;
            _btn1.press_start_ms = now;
            _btn2.press_start_ms = now;
            _pending_action = NAV_BOTH_PRESSED;
            return;
        }
        return;
    }

    if (_in_edit_mode) {
        updateEditMode();
    } else {
        updateNormalMode();
    }
}

void ButtonHandler::updateNormalMode() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // Button 1 (SCL / GPIO 7)
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
            if (dur >= 30 && !_btn1.long_press_fired) {
                _pending_action = NAV_BTN1_SHORT;
            }
        }
    }

    // Button 2 (SDA / GPIO 15)
    if (raw2) {
        if (!_btn2.is_down) {
            _btn2.is_down = true;
            _btn2.press_start_ms = now;
            _btn2.long_press_fired = false;
        } else {
            if (!_btn2.long_press_fired && (now - _btn2.press_start_ms >= 600)) {
                _btn2.long_press_fired = true;
                _pending_action = NAV_BTN2_LONG;
            }
        }
    } else {
        if (_btn2.is_down) {
            _btn2.is_down = false;
            uint32_t dur = now - _btn2.press_start_ms;
            if (dur >= 30 && !_btn2.long_press_fired) {
                _pending_action = NAV_BTN2_SHORT;
            }
        }
    }
}

// In Edit Mode:
// BTN1 = Decrement (-), BTN2 = Increment (+)
// Holding down accelerates the repeating rate!
void ButtonHandler::updateEditMode() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // BTN1 (Decrement)
    if (raw1) {
        if (!_btn1.is_down) {
            _btn1.is_down = true;
            _btn1.press_start_ms = now;
            _btn1.last_repeat_ms = now;
            _pending_action = NAV_EDIT_DEC;
        } else {
            uint32_t hold_time = now - _btn1.press_start_ms;
            uint32_t repeat_interval = 160; // Base speed

            if (hold_time > 2200) {
                repeat_interval = 40;  // High speed
            } else if (hold_time > 900) {
                repeat_interval = 80;  // Medium speed
            }

            if (hold_time >= 350 && (now - _btn1.last_repeat_ms >= repeat_interval)) {
                _btn1.last_repeat_ms = now;
                _pending_action = NAV_EDIT_DEC;
            }
        }
    } else {
        _btn1.is_down = false;
    }

    // BTN2 (Increment)
    if (raw2) {
        if (!_btn2.is_down) {
            _btn2.is_down = true;
            _btn2.press_start_ms = now;
            _btn2.last_repeat_ms = now;
            _pending_action = NAV_EDIT_INC;
        } else {
            uint32_t hold_time = now - _btn2.press_start_ms;
            uint32_t repeat_interval = 160; // Base speed

            if (hold_time > 2200) {
                repeat_interval = 40;  // High speed
            } else if (hold_time > 900) {
                repeat_interval = 80;  // Medium speed
            }

            if (hold_time >= 350 && (now - _btn2.last_repeat_ms >= repeat_interval)) {
                _btn2.last_repeat_ms = now;
                _pending_action = NAV_EDIT_INC;
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

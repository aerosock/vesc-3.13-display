#include "buttons.h"

ButtonHandler Buttons;

static const uint32_t DEBOUNCE_TIME_MS   = 28;  // Physical contact debounce stability threshold
static const uint32_t CHORD_WINDOW_MS    = 85;  // Grace period to detect second button of a dual-press
static const uint32_t CHORD_COOLDOWN_MS  = 120; // Required release silence after chord before unlocking
static const uint32_t REPEAT_DELAY_MS    = 450; // Hold delay in edit mode before auto-repeat begins
static const uint32_t LONG_PRESS_TIME_MS = 500; // Hold delay in normal mode for long press

ButtonHandler::ButtonHandler()
    : _pending_action(NAV_NONE),
      _in_edit_mode(false),
      _chord_active(false),
      _all_up_since_ms(0),
      _require_all_up(false) {
    _btn1 = { PIN_BTN_1, false, false, 0, 0, 0, false, false, 0 };
    _btn2 = { PIN_BTN_2, false, false, 0, 0, 0, false, false, 0 };
}

void ButtonHandler::begin() {
    pinMode(_btn1.pin, INPUT_PULLUP);
    pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonHandler::setEditMode(bool in_edit) {
    _in_edit_mode = in_edit;
    _require_all_up = true;
    _chord_active = true; // Lock until both buttons are released and quiet for cooldown period
    _all_up_since_ms = 0;
    _btn1.single_pending = false;
    _btn2.single_pending = false;
    _pending_action = NAV_NONE;
}

void ButtonHandler::processDebounce(ButtonState &btn, bool raw, uint32_t now) {
    if (raw != btn.raw_state) {
        btn.raw_state = raw;
        btn.last_change_ms = now;
    }

    if ((now - btn.last_change_ms) >= DEBOUNCE_TIME_MS) {
        if (btn.is_down != btn.raw_state) {
            btn.is_down = btn.raw_state;
            if (btn.is_down) {
                // Button transitioned to pressed
                btn.press_start_ms = now;
                btn.last_repeat_ms = now;
                btn.long_press_fired = false;
                if (!_chord_active && !_require_all_up) {
                    btn.single_pending = true;
                    btn.pending_since_ms = now;
                }
            } else {
                // Button transitioned to released
                // If single_pending was true, keep it true so chord window or short click can resolve
            }
        }
    }
}

void ButtonHandler::update() {
    uint32_t now = millis();
    bool raw1 = (digitalRead(_btn1.pin) == LOW);
    bool raw2 = (digitalRead(_btn2.pin) == LOW);

    // 1. Debounce both physical buttons
    processDebounce(_btn1, raw1, now);
    processDebounce(_btn2, raw2, now);

    bool both_down = (_btn1.is_down && _btn2.is_down);
    bool both_up   = (!_btn1.is_down && !_btn2.is_down);

    // 2. Track continuous release silence
    if (both_up) {
        if (_all_up_since_ms == 0) {
            _all_up_since_ms = now;
        }
    } else {
        _all_up_since_ms = 0;
    }

    // 3. Post-Chord Lockout & Release Cooldown
    // Prevents asymmetric finger lift, chatter, or microvibrations from leaking single clicks
    if (_chord_active) {
        if (both_up && (now - _all_up_since_ms >= CHORD_COOLDOWN_MS)) {
            _chord_active = false;
            _require_all_up = false;
            _btn1.single_pending = false;
            _btn2.single_pending = false;
        }
        return; // Suppress all actions while chord is active or cooling down
    }

    // 4. Release Gate (after entering edit mode or screen transition)
    if (_require_all_up) {
        if (both_up && (now - _all_up_since_ms >= 50)) {
            _require_all_up = false;
            _btn1.single_pending = false;
            _btn2.single_pending = false;
        }
        return;
    }

    // 5. Dual-Press (Chord) Detection with Coincidence Window
    // Triggers if both buttons are down, or if one is down/pending and second joins within window
    if (both_down || (_btn1.single_pending && _btn2.is_down) || (_btn2.single_pending && _btn1.is_down)) {
        _chord_active = true;
        _all_up_since_ms = 0;
        _btn1.single_pending = false;
        _btn2.single_pending = false;
        _btn1.long_press_fired = true;
        _btn2.long_press_fired = true;
        _pending_action = NAV_BOTH_PRESSED;
        return;
    }

    // 6. Mode-Specific Dispatch
    if (_in_edit_mode) {
        // --- EDIT MODE ---
        // BTN1 = UP (+), BTN2 = DOWN (-)

        // Process BTN1 pending single press (chord window expired)
        if (_btn1.single_pending) {
            if (now - _btn1.pending_since_ms >= CHORD_WINDOW_MS) {
                _btn1.single_pending = false;
                _pending_action = NAV_EDIT_INC;
                _btn1.last_repeat_ms = now;
            }
        } else if (_btn1.is_down) {
            uint32_t hold_time = now - _btn1.press_start_ms;
            if (hold_time >= REPEAT_DELAY_MS) {
                uint32_t interval = (hold_time > 1800) ? 60 : 150;
                if (now - _btn1.last_repeat_ms >= interval) {
                    _btn1.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_INC;
                }
            }
        }

        // Process BTN2 pending single press (chord window expired)
        if (_btn2.single_pending) {
            if (now - _btn2.pending_since_ms >= CHORD_WINDOW_MS) {
                _btn2.single_pending = false;
                _pending_action = NAV_EDIT_DEC;
                _btn2.last_repeat_ms = now;
            }
        } else if (_btn2.is_down) {
            uint32_t hold_time = now - _btn2.press_start_ms;
            if (hold_time >= REPEAT_DELAY_MS) {
                uint32_t interval = (hold_time > 1800) ? 60 : 150;
                if (now - _btn2.last_repeat_ms >= interval) {
                    _btn2.last_repeat_ms = now;
                    _pending_action = NAV_EDIT_DEC;
                }
            }
        }
    } else {
        // --- NORMAL MODE ---
        // BTN1
        if (_btn1.single_pending) {
            if (!_btn1.is_down) {
                // Released before long press: wait for chord window to expire, then fire short
                if (now - _btn1.pending_since_ms >= CHORD_WINDOW_MS) {
                    _btn1.single_pending = false;
                    _pending_action = NAV_BTN1_SHORT;
                }
            } else {
                // Still down: check if long press threshold reached
                if (now - _btn1.press_start_ms >= LONG_PRESS_TIME_MS) {
                    _btn1.single_pending = false;
                    _btn1.long_press_fired = true;
                    _pending_action = NAV_BTN1_LONG;
                }
            }
        }

        // BTN2
        if (_btn2.single_pending) {
            if (!_btn2.is_down) {
                // Released before long press: wait for chord window to expire, then fire short
                if (now - _btn2.pending_since_ms >= CHORD_WINDOW_MS) {
                    _btn2.single_pending = false;
                    _pending_action = NAV_BTN2_SHORT;
                }
            } else {
                // Still down: check if long press threshold reached
                if (now - _btn2.press_start_ms >= LONG_PRESS_TIME_MS) {
                    _btn2.single_pending = false;
                    _btn2.long_press_fired = true;
                    _pending_action = NAV_BTN2_LONG;
                }
            }
        }
    }
}

NavAction ButtonHandler::getNavAction() {
    NavAction act = _pending_action;
    _pending_action = NAV_NONE;
    return act;
}


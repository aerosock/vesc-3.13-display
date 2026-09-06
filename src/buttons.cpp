#include "buttons.h"

ButtonHandler Buttons;

ButtonHandler::ButtonHandler()
    : _pending_action(NAV_NONE) {
    _btn1 = { PIN_BTN_1, false, 0, false };
    _btn2 = { PIN_BTN_2, false, 0, false };
}

void ButtonHandler::begin() {
    pinMode(_btn1.pin, INPUT_PULLUP);
    pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonHandler::checkButton(ButtonState &btn, NavAction short_act, NavAction long_act) {
    bool raw = digitalRead(btn.pin); // Active LOW
    uint32_t now = millis();

    if (raw == LOW) {
        if (!btn.is_pressed) {
            btn.is_pressed = true;
            btn.press_start_time = now;
            btn.long_press_fired = false;
        } else {
            // Held down for more than 500ms
            if (!btn.long_press_fired && (now - btn.press_start_time >= 500)) {
                btn.long_press_fired = true;
                _pending_action = long_act;
            }
        }
    } else {
        if (btn.is_pressed) {
            btn.is_pressed = false;
            uint32_t duration = now - btn.press_start_time;
            if (duration >= 35 && !btn.long_press_fired) {
                _pending_action = short_act;
            }
        }
    }
}

void ButtonHandler::update() {
    // Button 1 (SCL / GPIO 7): Short -> NAV_PREV, Long -> NAV_MENU_BACK
    checkButton(_btn1, NAV_PREV, NAV_MENU_BACK);

    // Button 2 (SDA / GPIO 15): Short -> NAV_NEXT, Long -> NAV_SELECT_ENTER
    checkButton(_btn2, NAV_NEXT, NAV_SELECT_ENTER);
}

NavAction ButtonHandler::getNavAction() {
    NavAction act = _pending_action;
    _pending_action = NAV_NONE;
    return act;
}

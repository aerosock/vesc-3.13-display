#pragma once

#include <Arduino.h>
#include "board_config.h"
#include "dash_types.h"

class ButtonHandler {
public:
    ButtonHandler();
    void begin();
    void update();

    // Returns generic navigation actions for menus, screens, and setting adjusters
    NavAction getNavAction();

    // Raw button queries if needed
    bool isBtn1Pressed() const { return _btn1.is_pressed; }
    bool isBtn2Pressed() const { return _btn2.is_pressed; }

private:
    struct ButtonState {
        uint8_t pin;
        bool is_pressed;
        uint32_t press_start_time;
        bool long_press_fired;
    };

    ButtonState _btn1;
    ButtonState _btn2;
    NavAction   _pending_action;

    void checkButton(ButtonState &btn, NavAction short_act, NavAction long_act);
};

extern ButtonHandler Buttons;

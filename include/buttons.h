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
    void setEditMode(bool in_edit) { _in_edit_mode = in_edit; }
    bool isInEditMode() const { return _in_edit_mode; }

    NavAction getNavAction();

    bool isBtn1Down() const { return _btn1.is_down; }
    bool isBtn2Down() const { return _btn2.is_down; }

private:
    struct ButtonState {
        uint8_t pin;
        bool is_down;
        uint32_t press_start_ms;
        uint32_t last_repeat_ms;
        bool long_press_fired;
    };

    ButtonState _btn1;
    ButtonState _btn2;
    NavAction   _pending_action;
    bool        _in_edit_mode;

    void updateNormalMode();
    void updateEditMode();
};

extern ButtonHandler Buttons;

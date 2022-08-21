#pragma once

#include <padscore/kpad.h>
#include <vpad/input.h>
#include <cstring>

extern VPADStatus vpad_status;
extern VPADReadError vpad_error;
extern KPADStatus kpad[4], kpad_status;

typedef enum Button {
    PAD_BUTTON_A = VPAD_BUTTON_A | WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A | WPAD_PRO_BUTTON_A,
    PAD_BUTTON_B = VPAD_BUTTON_B | WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_PRO_BUTTON_B,
    PAD_BUTTON_UP = VPAD_BUTTON_UP | VPAD_STICK_L_EMULATION_UP | WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP | WPAD_CLASSIC_STICK_L_EMULATION_UP | WPAD_PRO_BUTTON_UP | WPAD_PRO_STICK_L_EMULATION_UP,
    PAD_BUTTON_DOWN = VPAD_BUTTON_DOWN | VPAD_STICK_L_EMULATION_DOWN | WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_STICK_L_EMULATION_DOWN | WPAD_PRO_BUTTON_DOWN | WPAD_PRO_STICK_L_EMULATION_DOWN,
    PAD_BUTTON_LEFT = VPAD_BUTTON_LEFT | VPAD_STICK_L_EMULATION_LEFT | WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT | WPAD_CLASSIC_STICK_L_EMULATION_LEFT | WPAD_PRO_BUTTON_LEFT | WPAD_PRO_STICK_L_EMULATION_LEFT,
    PAD_BUTTON_RIGHT = VPAD_BUTTON_RIGHT | VPAD_STICK_L_EMULATION_RIGHT | WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT | WPAD_CLASSIC_STICK_L_EMULATION_RIGHT | WPAD_PRO_BUTTON_RIGHT | WPAD_PRO_STICK_L_EMULATION_RIGHT,
    PAD_BUTTON_L = VPAD_BUTTON_L | WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_L | WPAD_PRO_TRIGGER_L,
    PAD_BUTTON_R = VPAD_BUTTON_R | WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_R | WPAD_PRO_TRIGGER_R,
    PAD_BUTTON_ANY
} Button;

typedef enum ButtonState {
    TRIGGER,
    HOLD,
    RELEASE
} ButtonState;

void readInput();
bool getInput(ButtonState state, Button button);
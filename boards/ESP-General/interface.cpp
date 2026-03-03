#include "core/powerSave.h"
#include <interface.h>

#ifndef GAME_JOY_UP_PIN
#define GAME_JOY_UP_PIN -1
#endif
#ifndef GAME_JOY_DOWN_PIN
#define GAME_JOY_DOWN_PIN -1
#endif
#ifndef GAME_JOY_LEFT_PIN
#define GAME_JOY_LEFT_PIN -1
#endif
#ifndef GAME_JOY_RIGHT_PIN
#define GAME_JOY_RIGHT_PIN -1
#endif
#ifndef GAME_JOY_MID_PIN
#define GAME_JOY_MID_PIN -1
#endif
#ifndef GAME_JOY_SET_PIN
#define GAME_JOY_SET_PIN -1
#endif
#ifndef GAME_JOY_RESET_PIN
#define GAME_JOY_RESET_PIN -1
#endif
#ifndef GAME_BTN_1_PIN
#define GAME_BTN_1_PIN -1
#endif
#ifndef GAME_BTN_2_PIN
#define GAME_BTN_2_PIN -1
#endif
#ifndef GAME_BTN_BACK_PIN
#define GAME_BTN_BACK_PIN -1
#endif
#ifndef GAME_BTN_EXIT_PIN
#define GAME_BTN_EXIT_PIN -1
#endif


/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    bruceConfig.startupApp = "WebUI";

#if GAME_JOY_UP_PIN >= 0
    pinMode(GAME_JOY_UP_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_DOWN_PIN >= 0
    pinMode(GAME_JOY_DOWN_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_LEFT_PIN >= 0
    pinMode(GAME_JOY_LEFT_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_RIGHT_PIN >= 0
    pinMode(GAME_JOY_RIGHT_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_MID_PIN >= 0
    pinMode(GAME_JOY_MID_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_SET_PIN >= 0
    pinMode(GAME_JOY_SET_PIN, INPUT_PULLUP);
#endif
#if GAME_JOY_RESET_PIN >= 0
    pinMode(GAME_JOY_RESET_PIN, INPUT_PULLUP);
#endif
#if GAME_BTN_1_PIN >= 0
    pinMode(GAME_BTN_1_PIN, INPUT_PULLUP);
#endif
#if GAME_BTN_2_PIN >= 0
    pinMode(GAME_BTN_2_PIN, INPUT_PULLUP);
#endif
#if GAME_BTN_BACK_PIN >= 0
    pinMode(GAME_BTN_BACK_PIN, INPUT_PULLUP);
#endif
#if GAME_BTN_EXIT_PIN >= 0
    pinMode(GAME_BTN_EXIT_PIN, INPUT_PULLUP);
#endif
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() { return 0; }

/***************************************************************************************
** Function name: isCharging()
** Description:   Default implementation that returns false
***************************************************************************************/
bool isCharging() { return false; }

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (millis() - tm < 80 && !LongPress) return;

    bool upPressed = (GAME_JOY_UP_PIN >= 0) && (digitalRead(GAME_JOY_UP_PIN) == LOW);
    bool dwPressed = (GAME_JOY_DOWN_PIN >= 0) && (digitalRead(GAME_JOY_DOWN_PIN) == LOW);
    bool leftPressed = (GAME_JOY_LEFT_PIN >= 0) && (digitalRead(GAME_JOY_LEFT_PIN) == LOW);
    bool rightPressed = (GAME_JOY_RIGHT_PIN >= 0) && (digitalRead(GAME_JOY_RIGHT_PIN) == LOW);
    bool midPressed = (GAME_JOY_MID_PIN >= 0) && (digitalRead(GAME_JOY_MID_PIN) == LOW);
    bool backPressed = (GAME_BTN_BACK_PIN >= 0) && (digitalRead(GAME_BTN_BACK_PIN) == LOW);

    bool anyPressed = upPressed || dwPressed || leftPressed || rightPressed || midPressed || backPressed;
    if (anyPressed) tm = millis();
    if (anyPressed && wakeUpScreen()) return;

    AnyKeyPress = anyPressed;
    PrevPress = leftPressed;
    NextPress = rightPressed;
    UpPress = upPressed;
    DownPress = dwPressed;
    SelPress = midPressed;
    EscPress = backPressed;
}


/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turnoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}

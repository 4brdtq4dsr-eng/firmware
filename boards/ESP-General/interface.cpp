#include "core/powerSave.h"
#include <interface.h>

#ifndef BTN_ACT
#define BTN_ACT LOW
#endif
#ifndef BTN_A
#define BTN_A -1
#endif
#ifndef BTN_B
#define BTN_B -1
#endif
#ifndef BTN_MENU
#define BTN_MENU -1
#endif
#ifndef BTN_SELECT
#define BTN_SELECT -1
#endif
#ifndef BTN_START
#define BTN_START -1
#endif
#ifndef JOY_Z_BTN
#define JOY_Z_BTN -1
#endif
#ifndef JOY_X_PIN
#define JOY_X_PIN -1
#endif
#ifndef JOY_Y_PIN
#define JOY_Y_PIN -1
#endif

#ifndef JOY_CENTER
#define JOY_CENTER 2048
#endif
#ifndef JOY_DEADZONE
#define JOY_DEADZONE 800
#endif

static bool buttonPressed(int pin) {
    if (pin < 0) return false;
    return digitalRead(pin) == BTN_ACT;
}

static int axisRead(int pin) {
    if (pin < 0) return JOY_CENTER;
    return analogRead(pin);
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    bruceConfig.startupApp = "WebUI";

    if (TFT_BL >= 0) pinMode(TFT_BL, OUTPUT);

    if (BTN_A >= 0) pinMode(BTN_A, INPUT_PULLUP);
    if (BTN_B >= 0) pinMode(BTN_B, INPUT_PULLUP);
    if (BTN_MENU >= 0) pinMode(BTN_MENU, INPUT_PULLUP);
    if (BTN_SELECT >= 0) pinMode(BTN_SELECT, INPUT_PULLUP);
    if (BTN_START >= 0) pinMode(BTN_START, INPUT_PULLUP);
    if (JOY_Z_BTN >= 0) pinMode(JOY_Z_BTN, INPUT_PULLUP);

    if (JOY_X_PIN >= 0) pinMode(JOY_X_PIN, INPUT);
    if (JOY_Y_PIN >= 0) pinMode(JOY_Y_PIN, INPUT);
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
void _setBrightness(uint8_t brightval) {
    if (TFT_BL < 0) return;
    digitalWrite(TFT_BL, brightval > 5 ? HIGH : LOW);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = millis();
    if (!(millis() - tm > 120 || LongPress)) return;

    bool a = buttonPressed(BTN_A);
    bool b = buttonPressed(BTN_B);
    bool menu = buttonPressed(BTN_MENU);
    bool select = buttonPressed(BTN_SELECT) || buttonPressed(JOY_Z_BTN);
    bool start = buttonPressed(BTN_START);

    int joyX = axisRead(JOY_X_PIN);
    int joyY = axisRead(JOY_Y_PIN);

    bool joyLeft = (JOY_X_PIN >= 0) && (joyX < (JOY_CENTER - JOY_DEADZONE));
    bool joyRight = (JOY_X_PIN >= 0) && (joyX > (JOY_CENTER + JOY_DEADZONE));
    bool joyUp = (JOY_Y_PIN >= 0) && (joyY < (JOY_CENTER - JOY_DEADZONE));
    bool joyDown = (JOY_Y_PIN >= 0) && (joyY > (JOY_CENTER + JOY_DEADZONE));

    bool any = a || b || menu || select || start || joyLeft || joyRight || joyUp || joyDown;
    if (!any) return;

    tm = millis();
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    PrevPress = joyLeft;
    NextPress = joyRight;
    UpPress = joyUp;
    DownPress = joyDown;
    SelPress = select || a;
    EscPress = menu || start;
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

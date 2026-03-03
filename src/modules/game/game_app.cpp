#include "game_app.h"

#include "core/display.h"
#include "core/sd_functions.h"
#include <ArduinoJson.h>
#include <globals.h>

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

namespace {
constexpr int kMaxLevel = 20;
constexpr int kTopBarH = 20;
constexpr int kGroundYPad = 18;
constexpr char kSavePath[] = "/game/save.json";

bool pinPressed(int pin) { return pin >= 0 && digitalRead(pin) == LOW; }

struct Player {
    float x = 20;
    float y = 0;
    float vx = 0;
    float vy = 0;
    bool onGround = false;
};

struct Door {
    float x = 200;
    float y = 0;
    int w = 12;
    int h = 18;
};

class Game {
public:
    void run() {
        mountedSd_ = setupSdCard();
        loadSave();
        if (!mountedSd_) sdWarnOnce_ = true;

        while (!exitApp_) {
            level_ = constrain(level_, 1, kMaxLevel);
            showIntro();
            if (exitApp_) break;
            restartLevel();
            playLevel();
            if (exitApp_) break;
            if (completed_) {
                if (level_ < kMaxLevel) {
                    level_++;
                    if (maxUnlocked_ < level_) {
                        maxUnlocked_ = level_;
                        saveProgress();
                    }
                }
            }
        }

        returnToMenu = true;
        resetTftDisplay();
    }

private:
    int level_ = 1;
    int maxUnlocked_ = 1;
    bool mountedSd_ = false;
    bool sdWarnOnce_ = false;
    bool exitApp_ = false;
    bool completed_ = false;
    bool paused_ = false;

    Player p_;
    Door door_;
    bool cursorVisible_ = false;
    int cursorX_ = 40;
    int cursorY_ = 60;
    int codeA_ = 0;
    int codeB_ = 0;
    int codeC_ = 0;
    bool gateOpen_ = false;
    bool leverOn_ = false;
    bool floorBtnOn_ = false;
    int level14Step_ = 0;
    String typed_;
    uint32_t levelStartMs_ = 0;

    bool joyUp() { return pinPressed(GAME_JOY_UP_PIN); }
    bool joyDown() { return pinPressed(GAME_JOY_DOWN_PIN); }
    bool joyLeft() { return pinPressed(GAME_JOY_LEFT_PIN); }
    bool joyRight() { return pinPressed(GAME_JOY_RIGHT_PIN); }
    bool joyMid() { return pinPressed(GAME_JOY_MID_PIN); }
    bool joySet() { return pinPressed(GAME_JOY_SET_PIN); }
    bool joyReset() { return pinPressed(GAME_JOY_RESET_PIN); }
    bool btn1() { return pinPressed(GAME_BTN_1_PIN); }
    bool btn2() { return pinPressed(GAME_BTN_2_PIN); }
    bool btnBack() { return pinPressed(GAME_BTN_BACK_PIN) || check(EscPress); }
    bool btnExit() { return pinPressed(GAME_BTN_EXIT_PIN); }

    void showIntro() {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(1);
        tft.drawCentreString(("Уровень " + String(level_) + ": " + levelTitle(level_)).c_str(), tftWidth / 2, 24, 1);
        int y = 56;
        tft.drawString("Джойстик ←/→: ходьба", 10, y, 1);
        y += 14;
        tft.drawString("Кнопка 1: курсор (удерж.)", 10, y, 1);
        y += 14;
        tft.drawString("Кнопка 2: действие", 10, y, 1);
        y += 14;
        tft.drawString("SET: пауза, RESET: рестарт", 10, y, 1);
        y += 14;
        tft.drawString("BACK: назад, EXIT: выход", 10, y, 1);
        y += 20;
        tft.drawString(levelHint(level_).c_str(), 10, y, 1);
        y += 24;
        tft.drawString("Нажмите MID для старта", 10, y, 1);

        while (!exitApp_) {
            InputHandler();
            if (btnExit()) {
                exitApp_ = true;
                return;
            }
            if (btnBack()) {
                exitApp_ = true;
                return;
            }
            if (joyMid()) {
                while (joyMid()) {
                    InputHandler();
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    void restartLevel() {
        completed_ = false;
        paused_ = false;
        gateOpen_ = level_ <= 3;
        leverOn_ = false;
        floorBtnOn_ = false;
        typed_ = "";
        levelStartMs_ = millis();
        p_ = Player{};
        p_.y = tftHeight - kGroundYPad - 10;
        p_.x = 18;
        door_.x = tftWidth - 24;
        door_.y = tftHeight - kGroundYPad - 18;
        codeA_ = random(1, 10);
        codeB_ = random(1, 10);
        codeC_ = random(1, 10);
        level14Step_ = random(0, 2);
        if (level_ == 18) {
            p_.y = kTopBarH + 20;
            p_.onGround = true;
        }
    }

    void playLevel() {
        while (!exitApp_ && !completed_) {
            InputHandler();
            if (btnExit()) {
                exitApp_ = true;
                break;
            }
            if (joySet()) paused_ = true;
            if (joyReset()) restartLevel();
            if (paused_) {
                drawPause();
                handlePause();
                continue;
            }

            handleGameplayInput();
            updatePhysics();
            updateLevelLogic();
            drawLevel();
            if (hitDoor()) completed_ = true;
            vTaskDelay(pdMS_TO_TICKS(16));
        }
    }

    void handleGameplayInput() {
        cursorVisible_ = btn1();
        if (cursorVisible_) {
            if (joyLeft()) cursorX_ -= 2;
            if (joyRight()) cursorX_ += 2;
            if (joyUp()) cursorY_ -= 2;
            if (joyDown()) cursorY_ += 2;
            cursorX_ = constrain(cursorX_, 2, tftWidth - 3);
            cursorY_ = constrain(cursorY_, kTopBarH + 2, tftHeight - 3);
            if (btn2()) cursorAction();
            return;
        }

        p_.vx = 0;
        if (joyLeft()) p_.vx = -1.8f;
        if (joyRight()) p_.vx = 1.8f;
        if ((joyUp() || joyMid()) && p_.onGround) {
            p_.vy = (level_ == 15 ? -5.8f : -4.8f);
            p_.onGround = false;
        }
    }

    void updatePhysics() {
        if (level_ == 18) {
            p_.x += p_.vx;
            p_.y += (joyUp() ? -1.6f : 0) + (joyDown() ? 1.6f : 0);
            p_.x = constrain(p_.x, 4.f, (float)tftWidth - 10);
            p_.y = constrain(p_.y, (float)kTopBarH + 4, (float)tftHeight - 10);
            return;
        }

        p_.vy += 0.26f;
        p_.x += p_.vx;
        p_.y += p_.vy;

        float floorY = tftHeight - kGroundYPad - 10;
        if (level_ == 1 && p_.x > 96 && p_.x < 132) floorY = tftHeight + 50;
        if (level_ == 2 && p_.x > 90 && p_.x < 130) floorY = tftHeight - 60;

        if (p_.y >= floorY) {
            p_.y = floorY;
            p_.vy = 0;
            p_.onGround = true;
        }

        if (p_.x < 2) p_.x = 2;
        if (p_.x > tftWidth - 10) p_.x = tftWidth - 10;

        if (p_.y > tftHeight + 30) restartLevel();
    }

    void cursorAction() {
        if (level_ == 3) {
            door_.x = cursorX_ - 6;
            door_.y = cursorY_ - 9;
        } else if (level_ == 5) {
            if (abs(cursorX_ - 150) < 10 && abs(cursorY_ - (kTopBarH + 40)) < 10 && floorBtnOn_) gateOpen_ = true;
            if (abs(cursorX_ - 150) < 10 && abs(cursorY_ - (kTopBarH + 40)) < 10) leverOn_ = !leverOn_;
        } else if (level_ == 6) {
            tft.drawLine(cursorX_ - 10, cursorY_, cursorX_ + 10, cursorY_, TFT_WHITE);
            gateOpen_ = true;
        } else if (level_ == 10) {
            if (cursorX_ > 20 && cursorX_ < 110 && cursorY_ > 100 && cursorY_ < 130) gateOpen_ = true;
        } else if (level_ == 19) {
            gateOpen_ = true;
        } else if (level_ == 20) {
            gateOpen_ = true;
            tft.fillRect(70, 140, 90, 6, TFT_WHITE);
        }
    }

    void updateLevelLogic() {
        if (level_ == 4) {
            gateOpen_ = keypadThreeDigits(codeA_, codeB_, codeC_);
        } else if (level_ == 5) {
            if (p_.x > 56 && p_.x < 72) floorBtnOn_ = true;
            gateOpen_ = floorBtnOn_ && leverOn_;
        } else if (level_ == 7) {
            gateOpen_ = p_.x > 150;
        } else if (level_ == 8) {
            if (millis() - levelStartMs_ > 12000) gateOpen_ = true;
        } else if (level_ == 11) {
            gateOpen_ = true;
        } else if (level_ == 12) {
            gateOpen_ = true;
        } else if (level_ == 14) {
            gateOpen_ = quickLeftRightPattern();
        } else if (level_ == 16) {
            gateOpen_ = keypadNumberEquals(6);
        } else if (level_ == 17) {
            gateOpen_ = keypadNumberEquals(7);
        } else if (level_ >= 18) {
            gateOpen_ = true;
        }

        if (!gateOpen_) {
            door_.x = tftWidth - 24;
            door_.y = tftHeight - kGroundYPad - 18;
        }
    }

    bool quickLeftRightPattern() {
        static int phase = 0;
        if (level14Step_ == 0) {
            if (joyLeft()) phase = 1;
            if (phase == 1 && joyRight()) return true;
        } else {
            if (joyRight()) phase = 1;
            if (phase == 1 && joyLeft()) return true;
        }
        return false;
    }

    bool keypadNumberEquals(int expected) {
        String val = keypadInput();
        return val.length() > 0 && val.toInt() == expected;
    }

    bool keypadThreeDigits(int a, int b, int c) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Код:", 10, 34, 1);
        if (millis() - levelStartMs_ < 1000) {
            tft.drawString(String(a) + String(b) + String(c), 54, 34, 1);
            return false;
        }
        String val = keypadInput();
        if (val.length() == 3) {
            if (val == String(a) + String(b) + String(c)) return true;
            restartLevel();
        }
        return false;
    }

    String keypadInput() {
        const char *keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "OK"};
        static int idx = 0;
        if (joyLeft()) idx = (idx + 11) % 12;
        if (joyRight()) idx = (idx + 1) % 12;
        if (joyUp()) idx = (idx + 9) % 12;
        if (joyDown()) idx = (idx + 3) % 12;
        if (btn2() || joyMid()) {
            String k = keys[idx];
            if (k == "⌫") {
                if (!typed_.isEmpty()) typed_.remove(typed_.length() - 1);
            } else if (k == "OK") {
                String out = typed_;
                typed_ = "";
                return out;
            } else if (typed_.length() < 4) {
                typed_ += k;
            }
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        int x0 = 10;
        int y0 = tftHeight - 92;
        tft.drawRect(x0, y0 - 18, 86, 14, TFT_WHITE);
        tft.drawString(typed_, x0 + 4, y0 - 16, 1);
        for (int i = 0; i < 12; ++i) {
            int cx = x0 + (i % 3) * 28;
            int cy = y0 + (i / 3) * 18;
            tft.drawRect(cx, cy, 24, 14, i == idx ? TFT_WHITE : TFT_WHITE);
            tft.drawString(keys[i], cx + 6, cy + 3, 1);
        }
        return "";
    }

    bool hitDoor() {
        if (!gateOpen_) return false;
        return p_.x + 8 >= door_.x && p_.x <= door_.x + door_.w && p_.y + 10 >= door_.y;
    }

    void drawBrickFrame() {
        for (int x = 0; x < tftWidth; x += 10) {
            tft.drawRect(x, kTopBarH, 10, 6, TFT_WHITE);
            tft.drawRect(x, tftHeight - 6, 10, 6, TFT_WHITE);
        }
        for (int y = kTopBarH; y < tftHeight; y += 8) {
            tft.drawRect(0, y, 6, 8, TFT_WHITE);
            tft.drawRect(tftWidth - 6, y, 6, 8, TFT_WHITE);
        }
    }

    void drawTopBar() {
        tft.fillRect(0, 0, tftWidth, kTopBarH, TFT_BLACK);
        tft.drawRect(2, 2, 12, 12, TFT_WHITE);
        tft.fillRect(5, 5, 2, 6, TFT_WHITE);
        tft.fillRect(9, 5, 2, 6, TFT_WHITE);
        tft.drawString(("Уровень " + String(level_)).c_str(), 22, 6, 1);
        tft.drawRect(tftWidth - 16, 2, 12, 12, TFT_WHITE);
        tft.drawCircle(tftWidth - 10, 8, 3, TFT_WHITE);
    }

    void drawLevel() {
        tft.fillScreen(TFT_BLACK);
        drawTopBar();
        drawBrickFrame();

        // Ground
        if (level_ != 18) tft.drawLine(8, tftHeight - kGroundYPad, tftWidth - 8, tftHeight - kGroundYPad, TFT_WHITE);

        // Level-specific hints/props
        if (level_ == 5) {
            tft.drawRect(58, tftHeight - kGroundYPad - 3, 12, 3, floorBtnOn_ ? TFT_WHITE : TFT_WHITE);
            tft.drawLine(145, kTopBarH + 40, 155, kTopBarH + 40, TFT_WHITE);
        }
        if (level_ == 8 && !gateOpen_) {
            int rx = random(20, tftWidth - 20);
            tft.drawLine(rx, kTopBarH + 2, rx, kTopBarH + 18, TFT_WHITE);
            if (abs((int)p_.x - rx) < 4) restartLevel();
        }
        if (level_ == 10) {
            tft.drawRect(20, 100, 90, 24, TFT_WHITE);
            tft.drawString("Рестарт уровня", 24, 108, 1);
        }
        if (level_ == 14) {
            tft.drawString(level14Step_ == 0 ? "Низкий-Высокий" : "Высокий-Низкий", 30, 48, 1);
        }
        if (level_ == 19) {
            tft.drawString("Блоки: 3", 10, 40, 1);
        }

        // Player and door
        tft.drawRect((int)p_.x, (int)p_.y, 8, 10, TFT_WHITE);
        tft.drawRect((int)door_.x, (int)door_.y, door_.w, door_.h, gateOpen_ ? TFT_WHITE : TFT_WHITE);
        if (!gateOpen_) tft.drawLine(door_.x, door_.y, door_.x + door_.w, door_.y + door_.h, TFT_WHITE);

        if (cursorVisible_) {
            tft.drawLine(cursorX_ - 3, cursorY_, cursorX_ + 3, cursorY_, TFT_WHITE);
            tft.drawLine(cursorX_, cursorY_ - 3, cursorX_, cursorY_ + 3, TFT_WHITE);
        }

        if (sdWarnOnce_) {
            tft.drawString("SD не найдена: прогресс не сохраняется", 8, tftHeight - 16, 1);
            sdWarnOnce_ = false;
        }
    }

    void drawPause() {
        tft.fillRect(20, 60, tftWidth - 40, 90, TFT_BLACK);
        tft.drawRect(20, 60, tftWidth - 40, 90, TFT_WHITE);
        tft.drawCentreString("Пауза", tftWidth / 2, 74, 1);
        tft.drawString("MID/Кнопка2: продолжить", 30, 98, 1);
        tft.drawString("BACK: назад", 30, 112, 1);
        tft.drawString("EXIT: выход", 30, 126, 1);
    }

    void handlePause() {
        while (paused_ && !exitApp_) {
            InputHandler();
            if (btnExit()) {
                exitApp_ = true;
                return;
            }
            if (btnBack()) {
                paused_ = false;
                return;
            }
            if (btn2() || joyMid()) {
                paused_ = false;
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void loadSave() {
        if (!mountedSd_) return;
        if (!SD.exists(kSavePath)) return;
        File f = SD.open(kSavePath, FILE_READ);
        if (!f) return;
        JsonDocument doc;
        if (deserializeJson(doc, f) == DeserializationError::Ok) {
            maxUnlocked_ = constrain((int)(doc["maxUnlockedLevel"] | 1), 1, kMaxLevel);
            level_ = maxUnlocked_;
        }
        f.close();
    }

    void saveProgress() {
        if (!mountedSd_) return;
        if (!SD.exists("/game")) SD.mkdir("/game");

        JsonDocument doc;
        doc["version"] = 1;
        doc["maxUnlockedLevel"] = maxUnlocked_;

        File tmp = SD.open("/game/save.tmp", FILE_WRITE);
        if (!tmp) return;
        serializeJson(doc, tmp);
        tmp.close();
        SD.remove(kSavePath);
        SD.rename("/game/save.tmp", kSavePath);
    }

    String levelTitle(int l) {
        static const char *kTitles[kMaxLevel] = {
            "Прыжок", "Стены хранят секреты", "Подвинь что-нибудь", "Память", "Операторы", "Рисуй", "Только ТЫ",
            "Ракеты", "Ползунки", "Попробуй заново", "Палец вверх/вниз", "R.I.P.", "2.5D", "Частота", "Удвоенный",
            "Грани", "Стрелки", "Лабиринт", "Копатель", "Выделение"
        };
        return kTitles[l - 1];
    }

    String levelHint(int l) {
        static const char *kHints[kMaxLevel] = {
            "Подсказка: тайминг прыжка.", "Подсказка: ищи невидимую опору.", "Подсказка: дверь можно перетащить.",
            "Подсказка: запомни 3 цифры.", "Подсказка: кнопка пола, потом рычаг.", "Подсказка: нарисуй мост.",
            "Подсказка: клон ломает камень.", "Подсказка: выживи 12 секунд.", "Подсказка: двигай платформы.",
            "Подсказка: нажми ложный рестарт.", "Подсказка: совмещай иконки.", "Подсказка: иногда смерть полезна.",
            "Подсказка: это только видимость.", "Подсказка: повтори последовательность.", "Подсказка: двойной прыжок.",
            "Подсказка: посчитай стороны.", "Подсказка: посчитай сумму.", "Подсказка: не касайся стен.",
            "Подсказка: копай и строй.", "Подсказка: создай платформу выделением."
        };
        return kHints[l - 1];
    }
};
} // namespace

void gameApp() {
    Game game;
    game.run();
}

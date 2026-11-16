#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Настроения и позиции из оригинальной FluxGarage RoboEyes
#define DEFAULT 0
#define TIRED   1
#define ANGRY   2
#define HAPPY   3

#define ON  1
#define OFF 0

// Направления взгляда
#define N  1
#define NE 2
#define E  3
#define SE 4
#define S  5
#define SW 6
#define W  7
#define NW 8

class RoboEyesST7735 {
private:
    Adafruit_ST7735* tft = nullptr;
    GFXcanvas16* canvas = nullptr;

    int screenWidth = 160;
    int screenHeight = 128;
    int frameInterval = 25; // ~40 FPS
    uint32_t fpsTimer = 0;

    uint16_t bgColor = ST77XX_BLACK;
    uint16_t mainColor = ST77XX_CYAN;

    // Геометрия глаз
    int eyeLwidthDefault = 36, eyeLheightDefault = 44;
    int eyeRwidthDefault = 36, eyeRheightDefault = 44;
    int eyeLwidthCurrent = 36, eyeLheightCurrent = 44;
    int eyeRwidthCurrent = 36, eyeRheightCurrent = 44;
    int eyeLwidthNext = 36, eyeLheightNext = 44;
    int eyeRwidthNext = 36, eyeRheightNext = 44;

    int eyeLborderRadiusDefault = 10, eyeRborderRadiusDefault = 10;
    int eyeLborderRadiusCurrent = 10, eyeRborderRadiusCurrent = 10;
    int eyeLborderRadiusNext = 10, eyeRborderRadiusNext = 10;

    int spaceBetweenDefault = 20;
    int spaceBetweenCurrent = 20;
    int spaceBetweenNext = 20;

    // Координаты
    int eyeLxDefault, eyeLyDefault;
    int eyeRxDefault, eyeRyDefault;
    int eyeLx, eyeLy, eyeRx, eyeRy;
    int eyeLxNext, eyeLyNext, eyeRxNext, eyeRyNext;

    // Веки и эмоции
    bool tired = false, angry = false, happy = false, curious = false, cyclops = false;
    bool eyeL_open = false, eyeR_open = false;
    bool isBlinkingL = false, isBlinkingR = false;

    byte eyelidsTiredHeight = 0, eyelidsTiredHeightNext = 0;
    byte eyelidsAngryHeight = 0, eyelidsAngryHeightNext = 0;
    byte eyelidsHappyBottomOffset = 0, eyelidsHappyBottomOffsetNext = 0;

    // Авто-анимации
    bool autoblinker = false;
    int blinkInterval = 3, blinkIntervalVariation = 2;
    uint32_t blinktimer = 0;

    bool idle = false;
    int idleInterval = 2, idleIntervalVariation = 2;
    uint32_t idleAnimationTimer = 0;

    bool confused = false, laugh = false;
    uint32_t confusedAnimationTimer = 0, laughAnimationTimer = 0;
    int confusedAnimationDuration = 500, laughAnimationDuration = 500;
    bool confusedToggle = true, laughToggle = true;

    bool hFlicker = false, vFlicker = false;
    byte hFlickerAmplitude = 2, vFlickerAmplitude = 2;
    bool hFlickerAlternate = false, vFlickerAlternate = false;

public:
    void begin(Adafruit_ST7735* displayPtr, int width, int height, byte maxFrameRate) {
        tft = displayPtr;
        screenWidth = width;
        screenHeight = height;
        frameInterval = 1000 / maxFrameRate;

        // Выделяем буфер под зону глаз (160x96 пикселей = ~30 КБ ОЗУ, без мерцания!)
        if (canvas == nullptr) {
            canvas = new GFXcanvas16(screenWidth, 96);
        }

        eyeLxDefault = ((screenWidth) - (eyeLwidthDefault + spaceBetweenDefault + eyeRwidthDefault)) / 2;
        eyeLyDefault = (96 - eyeLheightDefault) / 2;
        eyeRxDefault = eyeLxDefault + eyeLwidthDefault + spaceBetweenDefault;
        eyeRyDefault = eyeLyDefault;

        eyeLx = eyeLxDefault; eyeLy = eyeLyDefault;
        eyeRx = eyeRxDefault; eyeRy = eyeRyDefault;
        eyeLxNext = eyeLx; eyeLyNext = eyeLy;
        eyeRxNext = eyeRx; eyeRyNext = eyeRy;

        eyeLheightCurrent = 1; eyeRheightCurrent = 1;
        open();
    }

    void setDisplayColors(uint16_t background, uint16_t main) {
        bgColor = background;
        mainColor = main;
    }

    void setWidth(byte leftEye, byte rightEye) { eyeLwidthNext = leftEye; eyeRwidthNext = rightEye; eyeLwidthDefault = leftEye; eyeRwidthDefault = rightEye; }
    void setHeight(byte leftEye, byte rightEye) { eyeLheightNext = leftEye; eyeRheightNext = rightEye; eyeLheightDefault = leftEye; eyeRheightDefault = rightEye; }
    void setBorderradius(byte leftEye, byte rightEye) { eyeLborderRadiusNext = leftEye; eyeRborderRadiusNext = rightEye; eyeLborderRadiusDefault = leftEye; eyeRborderRadiusDefault = rightEye; }
    void setSpacebetween(int space) { spaceBetweenNext = space; spaceBetweenDefault = space; }
    void setCyclops(bool active) { cyclops = active; }
    void setCuriosity(bool active) { curious = active; }

    void setMood(unsigned char mood) {
        tired = (mood == TIRED);
        angry = (mood == ANGRY);
        happy = (mood == HAPPY);
    }

    void setPosition(unsigned char pos) {
        int maxX = screenWidth - eyeLwidthCurrent - spaceBetweenCurrent - eyeRwidthCurrent;
        int maxY = 96 - eyeLheightDefault;
        switch (pos) {
            case N:  eyeLxNext = maxX / 2; eyeLyNext = 0; break;
            case NE: eyeLxNext = maxX;     eyeLyNext = 0; break;
            case E:  eyeLxNext = maxX;     eyeLyNext = maxY / 2; break;
            case SE: eyeLxNext = maxX;     eyeLyNext = maxY; break;
            case S:  eyeLxNext = maxX / 2; eyeLyNext = maxY; break;
            case SW: eyeLxNext = 0;        eyeLyNext = maxY; break;
            case W:  eyeLxNext = 0;        eyeLyNext = maxY / 2; break;
            case NW: eyeLxNext = 0;        eyeLyNext = 0; break;
            default: eyeLxNext = eyeLxDefault; eyeLyNext = eyeLyDefault; break;
        }
    }

    void setAutoblinker(bool active, int interval = 3, int variation = 2) {
        autoblinker = active; blinkInterval = interval; blinkIntervalVariation = variation;
    }

    void setIdleMode(bool active, int interval = 2, int variation = 2) {
        idle = active; idleInterval = interval; idleIntervalVariation = variation;
    }

    void open(bool left = true, bool right = true) {
        if (left)  { eyeL_open = true;  eyeLheightNext = eyeLheightDefault; }
        if (right) { eyeR_open = true;  eyeRheightNext = eyeRheightDefault; }
    }

    void close(bool left = true, bool right = true) {
        if (left)  { eyeL_open = false; eyeLheightNext = 1; }
        if (right) { eyeR_open = false; eyeRheightNext = 1; }
    }

    void blink(bool left = true, bool right = true) {
        if (left)  { isBlinkingL = true; eyeLheightNext = 1; }
        if (right) { isBlinkingR = true; eyeRheightNext = 1; }
    }



    void anim_confused() { confused = true; confusedAnimationTimer = millis(); }
    void anim_laugh() { laugh = true; laughAnimationTimer = millis(); }

    void update() {
        if (!tft || !canvas) return;
        if (millis() - fpsTimer < frameInterval) return;
        fpsTimer = millis();
        drawEyes();
    }

    void drawEyes() {
        // 1. Эффект любопытства (меняем высоту ТОЛЬКО если глаз открыт и не моргает прямо сейчас)
        if (curious) {
            if (!isBlinkingL && eyeL_open) {
                if (eyeLxNext <= 10) eyeLheightNext = eyeLheightDefault + 8;
                else if (eyeLxNext >= (screenWidth - eyeLwidthCurrent - spaceBetweenCurrent - eyeRwidthCurrent - 10) && cyclops) eyeLheightNext = eyeLheightDefault + 8;
                else eyeLheightNext = eyeLheightDefault;
            }

            if (!isBlinkingR && eyeR_open) {
                if (eyeRxNext >= screenWidth - eyeRwidthCurrent - 10) eyeRheightNext = eyeRheightDefault + 8;
                else eyeRheightNext = eyeRheightDefault;
            }
        }

        // 2. Быстрая и плавная интерполяция ширины и высоты глаз
        eyeLwidthCurrent = (eyeLwidthCurrent + eyeLwidthNext) / 2;
        int prevLHeight = eyeLheightCurrent;
        if (isBlinkingL) {
            // При моргании схлопываем глаз быстро (шаг 12 пикселей за кадр)
            eyeLheightCurrent = max(1, eyeLheightCurrent - 12);
        } else {
            // При открытии возвращаем плавно, но без зависания в конце
            eyeLheightCurrent = (eyeLheightCurrent + eyeLheightNext * 2) / 3;
        }
        eyeLy += (prevLHeight - eyeLheightCurrent) / 2;

        eyeRwidthCurrent = (eyeRwidthCurrent + eyeRwidthNext) / 2;
        int prevRHeight = eyeRheightCurrent;
        if (isBlinkingR) {
            eyeRheightCurrent = max(1, eyeRheightCurrent - 12);
        } else {
            eyeRheightCurrent = (eyeRheightCurrent + eyeRheightNext * 2) / 3;
        }
        eyeRy += (prevRHeight - eyeRheightCurrent) / 2;

        // Автоматическое открытие после полного закрытия при моргании
        if (isBlinkingL && eyeLheightCurrent <= 2) {
            isBlinkingL = false;
            eyeLheightNext = eyeLheightDefault;
        }
        if (isBlinkingR && eyeRheightCurrent <= 2) {
            isBlinkingR = false;
            eyeRheightNext = eyeRheightDefault;
        }

        if (eyeL_open && eyeLheightCurrent <= 2) {
            eyeLheightNext = eyeLheightDefault;
        }
        if (eyeR_open && eyeRheightCurrent <= 2) {
            eyeRheightNext = eyeRheightDefault;
        }

        eyeLborderRadiusCurrent = (eyeLborderRadiusCurrent + eyeLborderRadiusNext) / 2;
        eyeRborderRadiusCurrent = (eyeRborderRadiusCurrent + eyeRborderRadiusNext) / 2;
        spaceBetweenCurrent = (spaceBetweenCurrent + spaceBetweenNext) / 2;

        eyeLx = (eyeLx + eyeLxNext) / 2;
        eyeLy = (eyeLy + eyeLyNext) / 2;
        eyeRxNext = eyeLxNext + eyeLwidthCurrent + spaceBetweenCurrent;
        eyeRyNext = eyeLyNext;
        eyeRx = (eyeRx + eyeRxNext) / 2;
        eyeRy = (eyeRy + eyeRyNext) / 2;

        // 3. Таймеры авто-моргания и свободного взгляда
        if (autoblinker && millis() >= blinktimer) {
            blink();
            blinktimer = millis() + (blinkInterval * 1000) + (random(blinkIntervalVariation) * 1000);
        }

        if (idle && millis() >= idleAnimationTimer) {
            int maxX = screenWidth - eyeLwidthCurrent - spaceBetweenCurrent - eyeRwidthCurrent;
            int maxY = 96 - eyeLheightDefault;
            eyeLxNext = random(0, max(1, maxX));
            eyeLyNext = random(0, max(1, maxY));
            idleAnimationTimer = millis() + (idleInterval * 1000) + (random(idleIntervalVariation) * 1000);
        }

        // 4. Анимации смеха и замешательства
        if (confused) {
            if (millis() - confusedAnimationTimer <= confusedAnimationDuration) {
                int offset = confusedToggle ? 12 : -12;
                eyeLx += offset; eyeRx += offset;
                confusedToggle = !confusedToggle;
            } else confused = false;
        }
        if (laugh) {
            if (millis() - laughAnimationTimer <= laughAnimationDuration) {
                int offset = laughToggle ? -5 : 5;
                eyeLy += offset; eyeRy += offset;
                laughToggle = !laughToggle;
            } else laugh = false;
        }

        // 5. Отрисовка в буфер GFXcanvas16
        canvas->fillScreen(bgColor);

        canvas->fillRoundRect(eyeLx, eyeLy, eyeLwidthCurrent, eyeLheightCurrent, eyeLborderRadiusCurrent, mainColor);
        if (!cyclops) {
            canvas->fillRoundRect(eyeRx, eyeRy, eyeRwidthCurrent, eyeRheightCurrent, eyeRborderRadiusCurrent, mainColor);
        }

        // Расчет век для эмоций
        eyelidsTiredHeightNext = tired ? (eyeLheightCurrent / 2) : 0;
        eyelidsAngryHeightNext = angry ? (eyeLheightCurrent / 2) : 0;
        eyelidsHappyBottomOffsetNext = happy ? (eyeLheightCurrent / 2) : 0;

        eyelidsTiredHeight = (eyelidsTiredHeight + eyelidsTiredHeightNext) / 2;
        eyelidsAngryHeight = (eyelidsAngryHeight + eyelidsAngryHeightNext) / 2;
        eyelidsHappyBottomOffset = (eyelidsHappyBottomOffset + eyelidsHappyBottomOffsetNext) / 2;

        // Сонные веки (TIRED)
        if (eyelidsTiredHeight > 0) {
            canvas->fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx, eyeLy + eyelidsTiredHeight - 1, bgColor);
            if (!cyclops) canvas->fillTriangle(eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy + eyelidsTiredHeight - 1, bgColor);
        }
        // Злые веки (ANGRY)
        if (eyelidsAngryHeight > 0) {
            canvas->fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy + eyelidsAngryHeight - 1, bgColor);
            if (!cyclops) canvas->fillTriangle(eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx, eyeRy + eyelidsAngryHeight - 1, bgColor);
        }
        // Счастливые веки (HAPPY)
        if (eyelidsHappyBottomOffset > 0) {
            canvas->fillRoundRect(eyeLx - 1, (eyeLy + eyeLheightCurrent) - eyelidsHappyBottomOffset + 1, eyeLwidthCurrent + 2, eyeLheightDefault, eyeLborderRadiusCurrent, bgColor);
            if (!cyclops) canvas->fillRoundRect(eyeRx - 1, (eyeRy + eyeRheightCurrent) - eyelidsHappyBottomOffset + 1, eyeRwidthCurrent + 2, eyeRheightDefault, eyeRborderRadiusCurrent, bgColor);
        }

        // Выводим готовый кадр по центру экрана (отступ сверху 16px, чтобы не задевать BLE ON и часы)
        tft->drawRGBBitmap(0, 16, canvas->getBuffer(), screenWidth, 96);
    }
};
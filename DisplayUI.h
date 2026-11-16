#pragma once
#include "RoboEyes_ST7735.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <utils.h>
#include <fonts/segoeuiUkr6.h>
#include "config.h"
#include "NotificationQueue.h"

class DisplayUI {
private:
    Adafruit_ST7735 tft;
    GFXcanvas16 eyeCanvas;

    bool roboEyesActive = false;
    bool showingNotification = false;

    // Состояние печатной машинки
    String animLines[8];
    int animTotalLines = 0;
    int animCurrentLine = 0;
    int animCharInLine = 0;
    uint32_t lastTypeMs = 0;

    // Состояние глаз
    RoboEyesST7735 roboEyes;

    // Таймер часов
    uint32_t lastClockDrawMs = 0;

    int splitTextToLines(const char* utf8Text, int x, int maxLines, String outLines[]) {
        tft.setFont(&segoeuiUkr6);
        String winText = Utf8win1251(utf8Text);
        int len = winText.length();
        int maxWidth = 158 - x;
        int line = 0, pos = 0;

        while (pos < len && line < maxLines) {
            while (pos < len && winText[pos] == ' ') pos++;
            if (pos >= len) break;

            int endPos = pos;
            int lastSpace = -1;

            while (endPos < len) {
                char c = winText[endPos];
                if (c == '\n') break;
                if (c == ' ') lastSpace = endPos;

                String testStr = winText.substring(pos, endPos + 1);
                int16_t x1, y1;
                uint16_t w, h;
                tft.getTextBounds(testStr, 0, 0, &x1, &y1, &w, &h);
                if (w > maxWidth) break;
                endPos++;
            }

            if (endPos < len && winText[endPos] != '\n' && lastSpace > pos) {
                endPos = lastSpace;
            }
            if (endPos == pos) endPos++;

            outLines[line] = winText.substring(pos, endPos);
            pos = endPos;
            if (pos < len && winText[pos] == '\n') pos++;
            line++;
        }
        return line;
    }

    void drawWrappedText(const char* utf8Text, int x, int y, uint16_t color, int maxLines) {
        String tempLines[4];
        int count = splitTextToLines(utf8Text, x, min(maxLines, 4), tempLines);
        tft.setFont(&segoeuiUkr6);
        tft.setTextColor(color);
        tft.setTextWrap(false);
        for (int i = 0; i < count; i++) {
            tft.setCursor(x, y + (i * 12));
            tft.print(tempLines[i]);
        }
    }

    void clearEyeTrail(int oldX, int oldY, int newX, int newY) {
        if (oldX < 0) return;
        if (newX > oldX) tft.fillRect(oldX, oldY, newX - oldX, EYE_H, ST77XX_BLACK);
        else if (newX < oldX) tft.fillRect(newX + EYE_W, oldY, oldX - newX, EYE_H, ST77XX_BLACK);
        if (newY > oldY) tft.fillRect(oldX, oldY, EYE_W, newY - oldY, ST77XX_BLACK);
        else if (newY < oldY) tft.fillRect(oldX, newY + EYE_H, EYE_W, oldY - newY, ST77XX_BLACK);
    }

public:
    DisplayUI() : tft(TFT_CS, TFT_DC, TFT_RST), eyeCanvas(EYE_W, EYE_H) {}

    bool isShowingNotification() const { return showingNotification; }
    void setShowingNotification(bool state) { showingNotification = state; }

    void begin() {
        SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
        SPI.setFrequency(SPI_FREQ);

        pinMode(TFT_RST, OUTPUT);
        digitalWrite(TFT_RST, LOW);
        delay(50);
        digitalWrite(TFT_RST, HIGH);
        delay(120);

        tft.initR(INITR_BLACKTAB);
        tft.setRotation(1);
        tft.fillScreen(ST77XX_BLACK);
        // Инициализируем глаза под разрешение 160x128 (горизонтальная ориентация) с частотой 40 FPS
        roboEyes.begin(&tft, 160, 128, 40);
        // Настраиваем цвета: черный фон (ST77XX_BLACK) и голубые глаза (ST77XX_CYAN)
        roboEyes.setDisplayColors(ST77XX_BLACK, ST77XX_CYAN);

        // Включаем авто-моргание (каждые 3 сек +/- 2 сек) и случайное оглядывание
        roboEyes.setAutoblinker(ON, 4, 2);
        roboEyes.setIdleMode(ON, 3, 2);
        roboEyes.setCuriosity(ON);
    }

    void showNotification(const NotificationItem& n, uint8_t index, uint8_t total) {
        roboEyesActive = false;
        showingNotification = true;
        tft.fillScreen(ST77XX_BLACK);
        tft.fillRect(0, 0, 160, 11, ST77XX_BLUE);

        tft.setFont(NULL);
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(1);
        tft.setCursor(2, 2);
        tft.print("NOTIFICATION ");
        tft.print(index + 1);
        tft.print("/");
        tft.print(total);

        drawWrappedText(n.title, 0, 21, ST77XX_YELLOW, 1);
        tft.drawLine(0, 25, 160, 25, ST77XX_BLUE);

        animTotalLines = splitTextToLines(n.message, 0, 8, animLines);
        animCurrentLine = 0;
        animCharInLine = 0;
        lastTypeMs = millis();
    }

    void updateTypewriter() {
        if (!showingNotification || animCurrentLine >= animTotalLines) return;

        uint32_t now = millis();
        if (now - lastTypeMs >= TYPE_SPEED_MS) {
            lastTypeMs = now;
            animCharInLine++;

            tft.setFont(&segoeuiUkr6);
            tft.setTextColor(ST77XX_GREEN);
            tft.setTextWrap(false);
            tft.setCursor(0, 37 + (animCurrentLine * 12));
            tft.print(animLines[animCurrentLine].substring(0, animCharInLine));

            if (animCharInLine >= animLines[animCurrentLine].length()) {
                animCurrentLine++;
                animCharInLine = 0;
            }
        }
    }

    void startRoboEyes(bool isConnected) {
        showingNotification = false;
        if (!roboEyesActive) {
            tft.fillScreen(ST77XX_BLACK);
            roboEyesActive = true;
        }

        // Обновляем индикатор статуса BLE в левом верхнем углу (не пересекается с зоной глаз Y=16..112)
        tft.fillRect(4, 4, 45, 8, ST77XX_BLACK);
        tft.setFont(NULL);
        tft.setTextColor(COLOR_DARKGREY);
        tft.setTextSize(1);
        tft.setCursor(4, 4);
        tft.print(isConnected ? "BLE ON " : "BLE OFF");

        if (isConnected) {
            roboEyes.setDisplayColors(ST77XX_BLACK, ST77XX_CYAN);
            roboEyes.setMood(DEFAULT); // Бодрые голубые глаза
        } else {
            roboEyes.setDisplayColors(ST77XX_BLACK, ST77XX_ORANGE);
            roboEyes.setMood(TIRED);   // «Сонные» оранжевые глаза
        }
    }


    void updateRoboEyes(bool isConnected) {
        if (!roboEyesActive) return;
        roboEyes.update(); // Библиотека сама рисует кадры с учетом FPS

    }

    void updateClock(bool timeSynced) {
        if (!roboEyesActive || !timeSynced) return;

        uint32_t now = millis();
        if (now - lastClockDrawMs < 1000) return;
        lastClockDrawMs = now;

        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) return;

        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), (timeinfo.tm_sec % 2 == 0) ? "%02d:%02d" : "%02d %02d",
                 timeinfo.tm_hour, timeinfo.tm_min);

        tft.fillRect(122, 4, 34, 8, ST77XX_BLACK);
        tft.setFont(NULL);
        tft.setTextColor(COLOR_DARKGREY);
        tft.setTextSize(1);
        tft.setCursor(124, 4);
        tft.print(timeStr);
    }
};

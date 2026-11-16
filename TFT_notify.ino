#include <Arduino.h>
#include "NimBLEDevice.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <utils.h>  // из библиотеки Ukr_GFX_fonts
#include <fonts/segoeuiUkr6.h>  // выберите подходящий размер
#include <fonts/segoeuibUkr6.h>

// ==================== ПИНЫ TFT ====================
#define TFT_CS   10
#define TFT_RST   4
#define TFT_DC    3
// Явно запишемо апаратні піни ESP32-C3 для контролю:
#define TFT_MOSI  7  
#define TFT_SCLK  6  
// ==================================================

// КОНСТРУКТОР ДЛЯ АПАРАТНОГО SPI (Передаємо тільки 3 піни!)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

bool roboEyesActive = false;

// ==================== UUIDs ANCS ====================
static NimBLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static NimBLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static NimBLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static NimBLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pControlPointCharacteristic = nullptr;

// ==================== UUIDs CTS (Current Time Service) ====================
static NimBLEUUID ctsServiceUUID((uint16_t)0x1805);
static NimBLEUUID currentTimeCharUUID((uint16_t)0x2A2B);

bool timeSynced = false;
uint32_t lastClockDrawMs = 0;


// ==================== КАРУСЕЛЬ ====================
#define QUEUE_SIZE 10
#define SHOW_TIME_MS 10000UL

struct NotificationItem {
    char title[64];
    char message[512];
    uint32_t id;
    bool valid;
};

NotificationItem queue[QUEUE_SIZE];
uint8_t queueHead = 0;   // Вказує на найстаріше повідомлення
uint8_t queueCount = 0;  // Скільки всього повідомлень зараз у каруселі

bool showingNotification = false;
uint32_t lastSwitchMs = 0;
uint32_t currentShownUid = 0;
int8_t currentDisplayIdx = -1; // Індекс елемента, який ЗАРАЗ на екрані

struct PendingItem {
    char title[64];
    char message[512];
    uint32_t id;
} pending;

uint8_t latestMessageID[4];
volatile boolean pendingNotification = false;
volatile boolean incomingCall = false;
uint8_t acceptCall = 0;
volatile bool needResubscribe = false;
bool waitingForReconnect = false;

// ==================== Отрисовка и Анимация ====================
String animLines[8];            // Заранее разбитые строки (максимум 8 строк для сообщения)
int animTotalLines = 0;         // Сколько всего строк получилось
int animCurrentLine = 0;        // Индекс текущей печатаемой строки
int animCharInLine = 0;         // Индекс текущего символа в этой строке
uint32_t lastTypeMs = 0;        // Таймер анимации
const uint16_t TYPE_SPEED_MS = 15; // Скорость печати (мс на букву)

int splitTextToLines(const char* utf8Text, int x, int maxLines, String outLines[]) {
    tft.setFont(&segoeuiUkr6);
    String winText = Utf8win1251(utf8Text);
    int len = winText.length();
    int maxWidth = 158 - x;

    int line = 0;
    int pos = 0;

    while (pos < len && line < maxLines) {
        while (pos < len && winText[pos] == ' ') {
            pos++;
        }
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
        if (endPos == pos) {
            endPos++;
        }

        outLines[line] = winText.substring(pos, endPos);
        pos = endPos;
        if (pos < len && winText[pos] == '\n') {
            pos++;
        }
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

void drawNotification(const NotificationItem& n, uint8_t index, uint8_t total) {
    tft.fillScreen(ST77XX_BLACK);
    roboEyesActive = false; // Выключаем глаза, когда показываем текст уведомления
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
    if (!showingNotification || animCurrentLine >= animTotalLines) {
        return;
    }

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

void drawStatus(const char* text, uint16_t color = ST77XX_WHITE) {
    roboEyesActive = false;
    tft.fillScreen(ST77XX_BLACK);
    tft.setFont(&segoeuiUkr6);
    tft.setTextColor(color);
    tft.setCursor(0, 14);
    tft.println(Utf8win1251(text));
}

// ==================== Движок анимированных глаз (с буфером GFXcanvas16) ====================
float eyeCurX = 0, eyeCurY = 0;
int eyeTargetX = 0, eyeTargetY = 0;

int prevLx = -1, prevRx = -1, prevEy = -1;
int prevTotalLid = -1;
uint16_t prevEyeColor = 0;

bool isBlinking = false;
uint8_t blinkPhase = 0;
int lidHeight = 0;

uint32_t lastEyeFrameMs = 0;
uint32_t nextBlinkMs = 2000;
uint32_t nextMoveMs = 1500;

const int EYE_W = 36;
const int EYE_H = 44;
const int EYE_RADIUS = 10;
const int LEFT_EYE_BASE_X = 34;
const int RIGHT_EYE_BASE_X = 90;
const int EYE_BASE_Y = 42;

// Создаем мини-холст в ОЗУ строго под размер одного глаза (занимает всего ~3 КБ)
GFXcanvas16 eyeCanvas(EYE_W, EYE_H);

void startRoboEyes() {
    if (!roboEyesActive) {
        tft.fillScreen(ST77XX_BLACK);
        roboEyesActive = true;
        eyeCurX = 0; eyeCurY = 0;
        eyeTargetX = 0; eyeTargetY = 0;
        lidHeight = 0;
        isBlinking = false;
        
        prevLx = -1; prevRx = -1; prevEy = -1;
        prevTotalLid = -1; prevEyeColor = 0;
        
        tft.setFont(NULL);
        tft.setTextColor(0x4208);
        tft.setTextSize(1);
        tft.setCursor(4, 4);
        tft.print((pClient != nullptr && pClient->isConnected()) ? "BLE ON " : "BLE OFF");
    }
}

// Стираем только те участки, откуда глаз уехал
void clearEyeTrail(int oldX, int oldY, int newX, int newY) {
    if (oldX < 0) return;
    
    if (newX > oldX) {
        tft.fillRect(oldX, oldY, newX - oldX, EYE_H, ST77XX_BLACK);
    } else if (newX < oldX) {
        tft.fillRect(newX + EYE_W, oldY, oldX - newX, EYE_H, ST77XX_BLACK);
    }
    
    if (newY > oldY) {
        tft.fillRect(oldX, oldY, EYE_W, newY - oldY, ST77XX_BLACK);
    } else if (newY < oldY) {
        tft.fillRect(oldX, newY + EYE_H, EYE_W, oldY - newY, ST77XX_BLACK);
    }
}

void updateRoboEyes(bool isConnected) {
    if (!roboEyesActive) return;

    uint32_t now = millis();
    if (now - lastEyeFrameMs < 25) return; // ~40 FPS
    lastEyeFrameMs = now;

    // 1. Движение взгляда
    if (now >= nextMoveMs && !isBlinking) {
        if (isConnected) {
            eyeTargetX = random(-14, 15);
            eyeTargetY = random(-10, 11);
            nextMoveMs = now + random(1500, 4000);
        } else {
            eyeTargetX = 0;
            eyeTargetY = 10;
            nextMoveMs = now + 2000;
        }
    }

    eyeCurX += (eyeTargetX - eyeCurX) * 0.25;
    eyeCurY += (eyeTargetY - eyeCurY) * 0.25;

    // 2. Моргание
    if (now >= nextBlinkMs && !isBlinking) {
        isBlinking = true;
        blinkPhase = 1;
    }

    if (isBlinking) {
        if (blinkPhase == 1) {
            lidHeight += 12;
            if (lidHeight >= EYE_H) {
                lidHeight = EYE_H;
                blinkPhase = 2;
            }
        } else if (blinkPhase == 2) {
            lidHeight -= 12;
            if (lidHeight <= 0) {
                lidHeight = 0;
                isBlinking = false;
                nextBlinkMs = now + random(2500, 6000);
            }
        }
    }

    uint16_t eyeColor = isConnected ? ST77XX_CYAN : ST77XX_ORANGE;
    int sleepLid = isConnected ? 0 : 18;
    int totalLid = max(lidHeight, sleepLid);

    int lx = LEFT_EYE_BASE_X + (int)round(eyeCurX);
    int rx = RIGHT_EYE_BASE_X + (int)round(eyeCurX);
    int ey = EYE_BASE_Y + (int)round(eyeCurY);

    if (lx == prevLx && ey == prevEy && totalLid == prevTotalLid && eyeColor == prevEyeColor) {
        return;
    }

    if (eyeColor != prevEyeColor) {
        tft.fillRect(4, 4, 45, 8, ST77XX_BLACK);
        tft.setFont(NULL);
        tft.setTextColor(0x4208);
        tft.setTextSize(1);
        tft.setCursor(4, 4);
        tft.print(isConnected ? "BLE ON" : "BLE OFF");
    }

    // 3. Рисуем идеальный глаз в памяти (с черным фоном по углам и скруглениями)
    eyeCanvas.fillScreen(ST77XX_BLACK);
    eyeCanvas.fillRoundRect(0, 0, EYE_W, EYE_H, EYE_RADIUS, eyeColor);
    if (totalLid > 0) {
        eyeCanvas.fillRect(0, 0, EYE_W, totalLid, ST77XX_BLACK);
    }

    // 4. Стираем внешний шлейф и выводим готовый кадр одним махом
    clearEyeTrail(prevLx, prevEy, lx, ey);
    clearEyeTrail(prevRx, prevEy, rx, ey);

    tft.drawRGBBitmap(lx, ey, eyeCanvas.getBuffer(), EYE_W, EYE_H);
    tft.drawRGBBitmap(rx, ey, eyeCanvas.getBuffer(), EYE_W, EYE_H);

    prevLx = lx;
    prevRx = rx;
    prevEy = ey;
    prevTotalLid = totalLid;
    prevEyeColor = eyeColor;
}





// ==================== Очередь ====================
bool enqueueNotification(const char* title, const char* msg, uint32_t id) {
    // Якщо карусель заповнена, посуваємо голову (витісняємо найстаріше)
    if (queueCount >= QUEUE_SIZE) {
        queue[queueHead].valid = false;
        queueHead = (queueHead + 1) % QUEUE_SIZE;
        queueCount--;
    }

    // Рахуємо індекс для нового елемента
    uint8_t tail = (queueHead + queueCount) % QUEUE_SIZE;
    
    strncpy(queue[tail].title, title, sizeof(queue[tail].title) - 1);
    queue[tail].title[sizeof(queue[tail].title) - 1] = '\0';
    strncpy(queue[tail].message, msg, sizeof(queue[tail].message) - 1);
    queue[tail].message[sizeof(queue[tail].message) - 1] = '\0';
    queue[tail].id = id;
    queue[tail].valid = true;
    queueCount++;
    
    return true;
}

bool removeNotificationByUid(uint32_t uid) {
    if (queueCount == 0) return false;

    int foundIdx = -1;
    for (int i = 0; i < queueCount; i++) {
        int idx = (queueHead + i) % QUEUE_SIZE;
        if (queue[idx].valid && queue[idx].id == uid) {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx < 0) return false;

    // Зсуваємо елементи, щоб не було «дірок» у каруселі
    for (int i = foundIdx; i < queueCount - 1; i++) {
        int curr = (queueHead + i) % QUEUE_SIZE;
        int next = (queueHead + i + 1) % QUEUE_SIZE;
        queue[curr] = queue[next];
    }

    int last = (queueHead + queueCount - 1) % QUEUE_SIZE;
    queue[last].valid = false;
    queueCount--;

    // Важливо: скидаємо індекс відображення, щоб loop() перерахував позицію
    currentDisplayIdx = -1; 
    
    Serial.printf("Removed notification UID %u from carousel\n", uid);
    return true;
}

// ==================== Парсер Data Source (с поддержкой длинных сообщений) ====================
static uint16_t remainingAttrBytes = 0; // Сколько байт текущего атрибута еще ждем
static uint8_t currentAttrID = 0;       // Какой атрибут сейчас дочитываем (0x01 или 0x03)

static void parseDataSource(uint8_t* pData, size_t length) {
    if (length == 0) return;

    uint32_t expectedUid = ((uint32_t)latestMessageID[0]) | ((uint32_t)latestMessageID[1] << 8) | 
                           ((uint32_t)latestMessageID[2] << 16) | ((uint32_t)latestMessageID[3] << 24);
    size_t idx = 0;

    // 1. Если мы НЕ дочитываем хвост от предыдущего пакета, значит это НОВЫЙ ответ с заголовком
    if (remainingAttrBytes == 0) {
        if (length < 5 || pData[0] != 0x00) return;

        uint32_t packetUid = ((uint32_t)pData[1]) | ((uint32_t)pData[2] << 8) | 
                             ((uint32_t)pData[3] << 16) | ((uint32_t)pData[4] << 24);
        if (packetUid != expectedUid) return;
        
        idx = 5; // Пропускаем CommandID (1 байт) и UID (4 байта)
    }

    // 2. Разбираем данные в пакете (и новые атрибуты, и продолжения старых)
    while (idx < length) {
        // Если мы сейчас НЕ в процессе дочитывания хвоста — читаем заголовок нового атрибута
        if (remainingAttrBytes == 0) {
            if (idx + 3 > length) break; // Не хватает байт даже на заголовок атрибута
            currentAttrID = pData[idx];
            remainingAttrBytes = pData[idx + 1] | (pData[idx + 2] << 8);
            idx += 3;
        }

        // Вычисляем, сколько байт этого атрибута лежит в текущем пакете
        size_t bytesAvailable = length - idx;
        size_t bytesToRead = min((size_t)remainingAttrBytes, bytesAvailable);

        if (currentAttrID == 0x01) { // Title
            size_t currentLen = strlen(pending.title);
            size_t freeSpace = sizeof(pending.title) - currentLen - 1;
            size_t n = min(bytesToRead, freeSpace);
            if (n > 0) {
                memcpy(&pending.title[currentLen], &pData[idx], n);
                pending.title[currentLen + n] = '\0';
            }
        } 
        else if (currentAttrID == 0x03) { // Message
            size_t currentLen = strlen(pending.message);
            size_t freeSpace = sizeof(pending.message) - currentLen - 1;
            size_t n = min(bytesToRead, freeSpace);
            if (n > 0) {
                memcpy(&pending.message[currentLen], &pData[idx], n);
                pending.message[currentLen + n] = '\0';
            }
        }

        idx += bytesToRead;
        remainingAttrBytes -= bytesToRead; // Уменьшаем счетчик оставшихся байт
    }

    // 3. Как только есть заголовок и хотя бы часть сообщения — добавляем или обновляем в карусели
    if (strlen(pending.title) > 0 && strlen(pending.message) > 0) {
        bool exists = false;
        for (int i = 0; i < queueCount; i++) {
            int qIdx = (queueHead + i) % QUEUE_SIZE;
            if (queue[qIdx].id == expectedUid) {
                exists = true;
                strncpy(queue[qIdx].message, pending.message, sizeof(queue[0].message) - 1);
                queue[qIdx].message[sizeof(queue[0].message) - 1] = '\0';
                
                if (showingNotification && currentShownUid == expectedUid) {
                    drawNotification(queue[qIdx], currentDisplayIdx, queueCount);
                }
                break;
            }
        }

        if (!exists) {
            enqueueNotification(pending.title, pending.message, expectedUid);
            Serial.printf(">>> CAROUSEL ADD (UID %u): [%s] %s\n", expectedUid, pending.title, pending.message);
        } else if (remainingAttrBytes == 0) {
            Serial.printf(">>> CAROUSEL UPDATED FULL (UID %u): [%s] %s\n", expectedUid, pending.title, pending.message);
        }
    }
}


// ==================== Callbacks ====================
static void dataSourceNotifyCallback(NimBLERemoteCharacteristic* pChar,
                                     uint8_t* pData, size_t length, bool isNotify) {
    parseDataSource(pData, length);
}

static void NotificationSourceNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (length < 8) return;

    uint32_t uid = ((uint32_t)pData[4]) | ((uint32_t)pData[5] << 8) | ((uint32_t)pData[6] << 16) | ((uint32_t)pData[7] << 24);

    if (pData[0] == 0) { // New notification
        Serial.printf("\n[Notification Source] New notification UID %u\n", uid);
        
        latestMessageID[0] = pData[4];
        latestMessageID[1] = pData[5];
        latestMessageID[2] = pData[6];
        latestMessageID[3] = pData[7];

        // Жестко очищаем буфер ПЕРЕД тем, как loop() отправит запрос к iPhone
        pending.title[0] = '\0';
        pending.message[0] = '\0';
        remainingAttrBytes = 0; // <-- ДОБАВИТЬ ЭТУ СТРОКУ (сброс хвостов от старых пакетов)


        if (pData[2] == 1) {
            incomingCall = true;
        }
        pendingNotification = true; // Флаг для loop() отправить запрос текста
    } 
    else if (pData[0] == 1) {
        Serial.printf("[Notification Source] Modified UID %u\n", uid);
    } 
    else if (pData[0] == 2) {
        Serial.printf("[Notification Source] Removed UID %u\n", uid);
        if (showingNotification && currentShownUid == uid) {
            showingNotification = false;
            currentShownUid = 0;
        }
        removeNotificationByUid(uid);
    }
}

// Функция разбора 10 байт времени от iPhone и установки системного времени ESP32
void syncTimeFromCTS(const uint8_t* data, size_t len) {
    if (len < 7) return;

    uint16_t year = (uint16_t)(*(data + 0)) | ((uint16_t)(*(data + 1)) << 8);
    uint8_t month = *(data + 2);
    uint8_t day   = *(data + 3);
    uint8_t hour  = *(data + 4);
    uint8_t min   = *(data + 5);
    uint8_t sec   = *(data + 6);

    // Записываем время во внутренние часы ESP32 (POSIX time)
    struct tm tm_info = {0};
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon  = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min  = min;
    tm_info.tm_sec  = sec;

    time_t t = mktime(&tm_info);
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);

    timeSynced = true;
    Serial.printf("Time synced from iPhone: %02d.%02d.%04d %02d:%02d:%02d\n", 
                  day, month, year, hour, min, sec);
}

// Callback на случай, если время на iPhone изменится (например, смена часового пояса)
static void ctsNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    syncTimeFromCTS(pData, length);
}

void updateClockDisplay() {
    if (!roboEyesActive || !timeSynced) return;

    uint32_t now = millis();
    if (now - lastClockDrawMs < 1000) return; // Обновляем раз в секунду
    lastClockDrawMs = now;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    char timeStr[6]; // Буфер на 5 символов ("ЧЧ:ММ") + нуль-терминатор
    if (timeinfo.tm_sec % 2 == 0) {
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        snprintf(timeStr, sizeof(timeStr), "%02d %02d", timeinfo.tm_hour, timeinfo.tm_min);
    }

    // Очищаем только маленький прямоугольник в правом верхнем углу
    tft.fillRect(122, 4, 34, 8, ST77XX_BLACK);
    tft.setFont(NULL);
    tft.setTextColor(0x4208);
    tft.setTextSize(1);
    tft.setCursor(124, 4);
    tft.print(timeStr);
}




class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("Client connected: %s\n", connInfo.getAddress().toString().c_str());
        pClient = pServer->getClient(connInfo);
        waitingForReconnect = false;
        needResubscribe = true;

        // Ключевое: активно инициировать согласование безопасности
        NimBLEDevice::startSecurity(connInfo.getConnHandle());
        Serial.println("Security initiated");
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("Client disconnected, reason: %d\n", reason);
        pClient = nullptr;
        pControlPointCharacteristic = nullptr;
        waitingForReconnect = true;
        showingNotification = false;

        // 1. Полностью очищаем очередь уведомлений при отключении
        for (int i = 0; i < QUEUE_SIZE; i++) {
            queue[i].valid = false;
        }
        queueHead = 0;
        queueCount = 0;
        currentShownUid = 0;
        currentDisplayIdx = -2;

        // 2. Сразу запускаем оранжевые глазки ожидания
        startRoboEyes();
    }

};

ServerCallbacks serverCallbacks;

// ==================== Setup ====================
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("Setup start");

    // Ініціалізуємо шину SPI2 на частоті 10 МГц
    // Параметри: SCLK, MISO (-1, не потрібен), MOSI, CS
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); 
    SPI.setFrequency(27000000); 

    // Скидання екрана (апаратний ресет)
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    delay(50);
    digitalWrite(TFT_RST, HIGH);
    delay(120);

    // Старт екрана (використовуйте свій таб, наприклад INITR_BLACKTAB або INITR_GREENTAB)
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    
    // Очищуємо екран червоним або синім кольором для тесту (якщо залити чорним, важче зрозуміти, чи запрацювала матриця)
    tft.fillScreen(ST77XX_RED); 
    delay(500);
    tft.fillScreen(ST77XX_BLACK);

    startRoboEyes();

    NimBLEDevice::init("TFT-Notify");
    NimBLEDevice::setMTU(256);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    NimBLEDevice::setPower(9);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&serverCallbacks);   // <--- передача по адресу
    pServer->advertiseOnDisconnect(true);

    NimBLEService* pService = pServer->createService("12345678-1234-5678-1234-56789abcdef0");
    pService->createCharacteristic(
        "12345678-1234-5678-1234-56789abcdef1",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    pService->start();

    NimBLEAdvertising* pAdvertising = pServer->getAdvertising();
    NimBLEAdvertisementData advData{};
    advData.setFlags(0x06);
    advData.addServiceUUID(ancsServiceUUID);
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();

    Serial.println("Advertising started!");
}

// ==================== Loop ====================
void loop() {
    if (pClient != nullptr && pClient->isConnected()) {
        NimBLEConnInfo connInfo = pClient->getConnInfo();
        if (connInfo.isEncrypted() && connInfo.isAuthenticated()) {
            if (pControlPointCharacteristic == nullptr || needResubscribe) {
                auto pAncsService = pClient->getService(ancsServiceUUID);
                if (pAncsService != nullptr) {
                    auto pNS = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
                    auto pDS = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
                    // --- Считываем и подписываемся на время (CTS) ---
                    auto pCtsService = pClient->getService(ctsServiceUUID);
                    if (pCtsService != nullptr) {
                        auto pTimeChar = pCtsService->getCharacteristic(currentTimeCharUUID);
                        if (pTimeChar != nullptr) {
                            if (pTimeChar->canRead()) {
                                NimBLEAttValue val = pTimeChar->readValue();
                                syncTimeFromCTS(val.data(), val.length());
                            }
                            if (pTimeChar->canNotify()) {
                                pTimeChar->subscribe(true, ctsNotifyCallback);
                            }
                        }
                    }

                    pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
                    if (pNS && pDS && pControlPointCharacteristic) {
                        pDS->subscribe(true, dataSourceNotifyCallback);
                        pNS->subscribe(true, NotificationSourceNotifyCallback);
                        needResubscribe = false;
                        Serial.println("Subscribed to ANCS");
                        if (!showingNotification && queueCount == 0)
                            startRoboEyes();
                    }
                }
            }

            if (pendingNotification) {
                    // Запрос Title (макс. 64 байта -> 0x40, 0x00)
                    uint8_t valTitle[8] = {0x00, latestMessageID[0], latestMessageID[1],
                                        latestMessageID[2], latestMessageID[3],
                                        0x01, 0x40, 0x00};
                    pControlPointCharacteristic->writeValue(valTitle, 8, true);

                    // Запрос Message (макс. 512 байт -> 0x00, 0x02 в little-endian)
                    uint8_t valMsg[8] = {0x00, latestMessageID[0], latestMessageID[1],
                                        latestMessageID[2], latestMessageID[3],
                                        0x03, 0x00, 0x02};
                    pControlPointCharacteristic->writeValue(valMsg, 8, true);
                    
                    pendingNotification = false;
                }


            if (incomingCall && Serial.available() > 0) {
                acceptCall = Serial.read();
                if (acceptCall == '1') {
                    const uint8_t r[] = {0x02, latestMessageID[0], latestMessageID[1],
                                         latestMessageID[2], latestMessageID[3], 0x00};
                    pControlPointCharacteristic->writeValue((uint8_t*)r, 6, true);
                    incomingCall = false;
                } else if (acceptCall == '0') {
                    const uint8_t r[] = {0x02, latestMessageID[0], latestMessageID[1],
                                         latestMessageID[2], latestMessageID[3], 0x01};
                    pControlPointCharacteristic->writeValue((uint8_t*)r, 6, true);
                    incomingCall = false;
                }
                acceptCall = 0;
            }
        }
    }

    // ==================== Оновлена логіка каруселі ====================
    uint32_t now = millis();

    if (queueCount > 0) {
        // Якщо на екрані нічого немає або настав час перемикати слайд
        if (!showingNotification || (now - lastSwitchMs >= SHOW_TIME_MS)) {
            
            // Якщо ми тільки зайшли або індекс збився — починаємо з голови (найстарішого)
            if (currentDisplayIdx < 0) {
                currentDisplayIdx = 0; // Це відносний індекс від 0 до queueCount-1
            } else {
                // Інакше просто переходимо до наступного повідомлення по колу
                currentDisplayIdx = (currentDisplayIdx + 1) % queueCount;
            }

            // Переводимо відносний індекс у реальний індекс масиву кольцевого буфера
            uint8_t realIdx = (queueHead + currentDisplayIdx) % QUEUE_SIZE;

            if (queue[realIdx].valid) {
                NotificationItem& item = queue[realIdx];
                currentShownUid = item.id;
                
                // Виводимо на екран. Передаємо порядковий номер для відображення (наприклад, 1/3, 2/3)
                drawNotification(item, currentDisplayIdx, queueCount);
                
                showingNotification = true;
                lastSwitchMs = now;
            }
        }
    } 
    else {
        // Якщо в каруселі взагалі немає повідомлень (всі видалені або ще не прийшли)
        if (showingNotification || currentDisplayIdx != -2) {
            showingNotification = false;
            currentShownUid = 0;
            currentDisplayIdx = -2; // Спеціальний прапорець, що екран уже оновлено

            // Вместо текстовых статусов запускаем глазки (они сами сменят цвет на оранжевый, если связи нет)
            startRoboEyes();
        }
    }

    updateTypewriter(); // <-- Обновляем анимацию печатной машинки
    updateRoboEyes(pClient != nullptr && pClient->isConnected()); // Обновляем кадр глаз
    updateClockDisplay();
    delay(10);
}

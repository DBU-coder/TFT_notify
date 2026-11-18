#include <Arduino.h>
#include "config.h"
#include "NotificationQueue.h"
#include "DisplayUI.h"
#include "BleAncsClient.h"
#include "WeatherManager.h"

NotificationQueue notifQueue;
DisplayUI ui;
BleManager bleManager;
WeatherManager weatherManager;

uint32_t currentShownUid = 0;
uint32_t lastSwitchMs = 0;

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("System start...");

    ui.begin();
    weatherManager.begin();
    ui.startRoboEyes(false);

    bleManager.begin();
    Serial.println("BLE Advertising started!");
}

void loop() {
    bleManager.update();
    weatherManager.update();

    uint32_t now = millis();
    uint8_t total = notifQueue.getCount();

    if (total > 0) {
        if (!ui.isShowingNotification() || (now - lastSwitchMs >= SHOW_TIME_MS)) {
            NotificationItem* item = notifQueue.nextItem();
            if (item != nullptr) {
                currentShownUid = item->id;
                ui.showNotification(*item, notifQueue.getCurrentDisplayIdx(), total);
                lastSwitchMs = now;
            }
        }
    } else {
        if (!ui.isRoboEyesActive()) {
            currentShownUid = 0;
            ui.startRoboEyes(bleManager.isConnected());
        }
    }

    ui.updateTypewriter();
    ui.updateRoboEyes(bleManager.isConnected());
    ui.updateClock(bleManager.isTimeSynced());
    ui.updateWeatherDisplay();
    delay(1);
}

#pragma once
#include "config.h"
#include "NotificationQueue.h"
#include "DisplayUI.h"

// Ссылки на глобальные объекты из главного скетча
extern NotificationQueue notifQueue;
extern DisplayUI ui;
extern uint32_t currentShownUid;

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pControlPointCharacteristic = nullptr;

static uint8_t latestMessageID[4];
static volatile bool pendingNotification = false;
static volatile bool incomingCall = false;
static volatile bool needResubscribe = false;
static bool timeSynced = false;

static struct {
    char title[64];
    char message[512];
} pending;

static uint16_t remainingAttrBytes = 0;
static uint8_t currentAttrID = 0;

// ==================== Парсер времени (CTS) ====================
static void syncTimeFromCTS(const uint8_t* data, size_t len) {
    if (len < 7) return;
    uint16_t year = (uint16_t)(*(data + 0)) | ((uint16_t)(*(data + 1)) << 8);
    uint8_t month = *(data + 2);
    uint8_t day   = *(data + 3);
    uint8_t hour  = *(data + 4);
    uint8_t min   = *(data + 5);
    uint8_t sec   = *(data + 6);

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
    Serial.printf("Time synced: %02d.%02d.%04d %02d:%02d:%02d\n", day, month, year, hour, min, sec);
}

static void ctsNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    syncTimeFromCTS(pData, length);
}

// ==================== Парсер уведомлений (ANCS Data Source) ====================
static void dataSourceNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (length == 0) return;

    uint32_t expectedUid = ((uint32_t)latestMessageID[0]) | ((uint32_t)latestMessageID[1] << 8) |
                           ((uint32_t)latestMessageID[2] << 16) | ((uint32_t)latestMessageID[3] << 24);
    size_t idx = 0;

    if (remainingAttrBytes == 0) {
        if (length < 5 || pData[0] != 0x00) return;
        uint32_t packetUid = ((uint32_t)pData[1]) | ((uint32_t)pData[2] << 8) |
                             ((uint32_t)pData[3] << 16) | ((uint32_t)pData[4] << 24);
        if (packetUid != expectedUid) return;
        idx = 5;
    }

    while (idx < length) {
        if (remainingAttrBytes == 0) {
            if (idx + 3 > length) break;
            currentAttrID = pData[idx];
            remainingAttrBytes = pData[idx + 1] | (pData[idx + 2] << 8);
            idx += 3;
        }

        size_t bytesToRead = min((size_t)remainingAttrBytes, length - idx);

        if (currentAttrID == 0x01) {
            size_t curLen = strlen(pending.title);
            size_t n = min(bytesToRead, sizeof(pending.title) - curLen - 1);
            if (n > 0) {
                memcpy(&pending.title[curLen], &pData[idx], n);
                pending.title[curLen + n] = '\0';
            }
        } else if (currentAttrID == 0x03) {
            size_t curLen = strlen(pending.message);
            size_t n = min(bytesToRead, sizeof(pending.message) - curLen - 1);
            if (n > 0) {
                memcpy(&pending.message[curLen], &pData[idx], n);
                pending.message[curLen + n] = '\0';
            }
        }
        idx += bytesToRead;
        remainingAttrBytes -= bytesToRead;
    }

    if (strlen(pending.title) > 0 && strlen(pending.message) > 0) {
        NotificationItem* existing = notifQueue.findByUid(expectedUid);
        if (existing != nullptr) {
            strncpy(existing->message, pending.message, sizeof(existing->message) - 1);
            existing->message[sizeof(existing->message) - 1] = '\0';
            if (ui.isShowingNotification() && currentShownUid == expectedUid) {
                ui.showNotification(*existing, notifQueue.getCurrentDisplayIdx(), notifQueue.getCount());
            }
        } else {
            notifQueue.enqueue(pending.title, pending.message, expectedUid);
            Serial.printf(">>> ADDED (UID %u): [%s] %s\n", expectedUid, pending.title, pending.message);
        }
    }
}

static void notificationSourceNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (length < 8) return;
    uint32_t uid = ((uint32_t)pData[4]) | ((uint32_t)pData[5] << 8) | ((uint32_t)pData[6] << 16) | ((uint32_t)pData[7] << 24);

    if (pData[0] == 0) {
        memcpy(latestMessageID, &pData[4], 4);
        pending.title[0] = '\0';
        pending.message[0] = '\0';
        remainingAttrBytes = 0;
        if (pData[2] == 1) incomingCall = true;
        pendingNotification = true;
    } else if (pData[0] == 2) {
        if (ui.isShowingNotification() && currentShownUid == uid) {
            ui.setShowingNotification(false);
            currentShownUid = 0;
        }
        notifQueue.removeByUid(uid);
    }
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("Client connected: %s\n", connInfo.getAddress().toString().c_str());
        pClient = pServer->getClient(connInfo);
        needResubscribe = true;
        NimBLEDevice::startSecurity(connInfo.getConnHandle());
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("Client disconnected, reason: %d\n", reason);
        pClient = nullptr;
        pControlPointCharacteristic = nullptr;
        notifQueue.clear();
        currentShownUid = 0;
        ui.startRoboEyes(false);
    }
};

static ServerCallbacks serverCallbacks;

class BleManager {
public:
    bool isConnected() const { return pClient != nullptr && pClient->isConnected(); }
    bool isTimeSynced() const { return timeSynced; }

    void begin() {
        NimBLEDevice::init("TFT-Notify");
        NimBLEDevice::setMTU(256);
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
        NimBLEDevice::setPower(9);

        NimBLEServer* pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(&serverCallbacks);
        pServer->advertiseOnDisconnect(true);

        NimBLEService* pService = pServer->createService("12345678-1234-5678-1234-56789abcdef0");
        pService->createCharacteristic("12345678-1234-5678-1234-56789abcdef1", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        pService->start();

        NimBLEAdvertising* pAdvertising = pServer->getAdvertising();
        NimBLEAdvertisementData advData{};
        advData.setFlags(0x06);
        advData.addServiceUUID(ancsServiceUUID);
        pAdvertising->setAdvertisementData(advData);
        pAdvertising->start();
    }

    void update() {
        if (!isConnected()) return;

        NimBLEConnInfo connInfo = pClient->getConnInfo();
        if (!connInfo.isEncrypted() || !connInfo.isAuthenticated()) return;

        if (pControlPointCharacteristic == nullptr || needResubscribe) {
            auto pAncsService = pClient->getService(ancsServiceUUID);
            if (pAncsService != nullptr) {
                auto pNS = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
                auto pDS = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);

                auto pCtsService = pClient->getService(ctsServiceUUID);
                if (pCtsService != nullptr) {
                    auto pTimeChar = pCtsService->getCharacteristic(currentTimeCharUUID);
                    if (pTimeChar != nullptr) {
                        if (pTimeChar->canRead()) {
                            NimBLEAttValue val = pTimeChar->readValue();
                            syncTimeFromCTS(val.data(), val.length());
                        }
                        if (pTimeChar->canNotify()) pTimeChar->subscribe(true, ctsNotifyCallback);
                    }
                }

                pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
                if (pNS && pDS && pControlPointCharacteristic) {
                    pDS->subscribe(true, dataSourceNotifyCallback);
                    pNS->subscribe(true, notificationSourceNotifyCallback);
                    needResubscribe = false;
                    if (!ui.isShowingNotification() && notifQueue.getCount() == 0) {
                        ui.startRoboEyes(true);
                    }
                }
            }
        }

        if (pendingNotification && pControlPointCharacteristic != nullptr) {
            uint8_t valTitle[8] = {0x00, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x01, 0x40, 0x00};
            pControlPointCharacteristic->writeValue(valTitle, 8, true);

            uint8_t valMsg[8] = {0x00, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x03, 0x00, 0x02};
            pControlPointCharacteristic->writeValue(valMsg, 8, true);
            pendingNotification = false;
        }

        if (incomingCall && Serial.available() > 0 && pControlPointCharacteristic != nullptr) {
            char cmd = Serial.read();
            if (cmd == '1' || cmd == '0') {
                const uint8_t r[6] = {0x02, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], (uint8_t)(cmd == '1' ? 0x00 : 0x01)};
                pControlPointCharacteristic->writeValue((uint8_t*)r, 6, true);
                incomingCall = false;
            }
        }
    }
};

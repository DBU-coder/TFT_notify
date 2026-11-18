#pragma once
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DisplayUI.h"

extern DisplayUI ui;

class WeatherManager {
private:
    uint32_t lastUpdateMs = 0;
    bool firstRunDone = false;

    void parseWeatherCode(int code, WeatherIconType& outIcon, String& outDesc) {
        if (code == 0) {
            outIcon = ICON_SUN;
            outDesc = "Ясно";
        } else if (code <= 3) {
            outIcon = ICON_CLOUD;
            outDesc = "Облачно";
        } else if (code >= 45 && code <= 48) {
            outIcon = ICON_CLOUD;
            outDesc = "Туман";
        } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
            outIcon = ICON_RAIN;
            outDesc = "Дождь";
        } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
            outIcon = ICON_SNOW;
            outDesc = "Снег";
        } else if (code >= 95) {
            outIcon = ICON_STORM;
            outDesc = "Гроза";
        } else {
            outIcon = ICON_CLOUD;
            outDesc = "";
        }
    }

    void fetchWeatherFromAPI() {
        if (WiFi.status() != WL_CONNECTED) return;

        delay(500);

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(10);

        HTTPClient http;
        http.setTimeout(10000);
        http.setReuse(false);

        String url = "https://";
        url += "api.open-meteo.com";
        url += "/v1/forecast?latitude=";
        url += WEATHER_LAT;
        url += "&longitude=";
        url += WEATHER_LON;
        url += "&current=temperature_2m,weather_code";


        Serial.printf("[Weather] Requesting: %s\n", url.c_str());

        if (http.begin(client, url)) {
            http.addHeader("User-Agent", "ESP32-C3-Weather");
            http.addHeader("Accept-Encoding", "identity");

            int httpCode = http.GET();

            if (httpCode == 200) {
                String payload = http.getString();
                JsonDocument doc;
                if (!deserializeJson(doc, payload)) {
                    float temp = doc["current"]["temperature_2m"];
                    int code = doc["current"]["weather_code"];

                    WeatherIconType icon;
                    String desc;
                    parseWeatherCode(code, icon, desc);

                    char buf[32];
                    int roundedTemp = (int)round(temp);
                    if (roundedTemp > 0) {
                        snprintf(buf, sizeof(buf), "+%d C %s", roundedTemp, desc.c_str());
                    } else {
                        snprintf(buf, sizeof(buf), "%d C %s", roundedTemp, desc.c_str());
                    }

                    ui.setWeather(buf, icon);
                    Serial.printf("[Weather] Success: %s (code %d)\n", buf, code);
                } else {
                    Serial.println("[Weather] JSON parse failed!");
                    Serial.println(payload.substring(0, 150));
                }
            } else {
                Serial.printf("[Weather] HTTP error: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
            }
            http.end();
        }
    }




    void turnOffWiFi() {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("[WiFi] Radio turned OFF to save power and keep BLE clean");
    }

public:
    // Вызывается один раз в setup() ДО запуска Bluetooth
    void begin() {
        // 1. Сразу выводим на экран информацию, чтобы при старте не было черного экрана
        ui.showWifiSetupScreen("TFT-Notify-Setup", "192.168.4.1");

        WiFi.mode(WIFI_STA);
        WiFiManager wm;
        wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
        
        // Сокращаем время попытки подключения к старой сети до 5 секунд (чтобы не ждать долго)
        wm.setConnectTimeout(5); 

        if (wm.autoConnect("TFT-Notify-Setup")) {
            Serial.println("[WiFi] Connected! Fetching initial weather...");
            fetchWeatherFromAPI();
        } else {
            Serial.println("[WiFi] Portal timeout or failed. Continuing in BLE-only mode.");
        }

        turnOffWiFi();
        lastUpdateMs = millis();
        firstRunDone = true;
    }


    // Вызывается в loop() для фонового обновления раз в 30 минут
    void update() {
        if (!firstRunDone) return;

        uint32_t now = millis();
        if (now - lastUpdateMs < WEATHER_INTERVAL_MS) return;
        lastUpdateMs = now;

        // Если в памяти нет сохраненного SSID — не пытаемся подключаться
        if (WiFi.SSID().length() == 0) return;

        Serial.println("[Weather] Periodic update: waking Wi-Fi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(); // Подключаемся с последними сохраненными данными

        uint32_t startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 7000) {
            delay(50);
        }

        if (WiFi.status() == WL_CONNECTED) {
            fetchWeatherFromAPI();
        } else {
            Serial.println("[Weather] Reconnect timeout");
        }

        turnOffWiFi();
    }
};

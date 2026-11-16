#pragma once
#include <Arduino.h>
#include "NimBLEDevice.h"

// ==================== ПИНЫ TFT (ESP32-C3) ====================
#define TFT_CS   10
#define TFT_RST   4
#define TFT_DC    3
#define TFT_MOSI  7
#define TFT_SCLK  6
#define SPI_FREQ  40000000

// ==================== НАСТРОЙКИ КАРУСЕЛИ И АНИМАЦИИ ====================
#define QUEUE_SIZE     10
#define SHOW_TIME_MS   10000UL
#define TYPE_SPEED_MS  15

// ==================== ГЕОМЕТРИЯ ГЛАЗ (ROBOEYES) ====================
#define EYE_W            36
#define EYE_H            44
#define EYE_RADIUS       10
#define LEFT_EYE_BASE_X  34
#define RIGHT_EYE_BASE_X 90
#define EYE_BASE_Y       42
#define COLOR_DARKGREY   0x4208

// ==================== UUIDs BLE (ANCS & CTS) ====================
static NimBLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static NimBLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static NimBLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static NimBLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

static NimBLEUUID ctsServiceUUID((uint16_t)0x1805);
static NimBLEUUID currentTimeCharUUID((uint16_t)0x2A2B);

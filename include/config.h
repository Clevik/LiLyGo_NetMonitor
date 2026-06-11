#pragma once

#include <Arduino.h>

// ---- Дисплей RM67162 (QSPI) ----
constexpr int8_t  DISP_CS      = 6;
constexpr int8_t  DISP_SCK     = 47;
constexpr int8_t  DISP_D0      = 18;
constexpr int8_t  DISP_D1      = 7;
constexpr int8_t  DISP_D2      = 48;
constexpr int8_t  DISP_D3      = 5;
constexpr int8_t  DISP_RST     = 17;
constexpr int8_t  DISP_TE      = 9;
constexpr int8_t  DISP_PWR_PIN = 38;

// ---- Размеры экрана (ландшафт, rotation=1) ----
constexpr uint16_t SCREEN_W = 536;
constexpr uint16_t SCREEN_H = 240;
constexpr uint8_t  RGB565_BYTES_PER_PIXEL = 2;
constexpr size_t   CANVAS_BUFFER_BYTES =
    static_cast<size_t>(SCREEN_W) * SCREEN_H * RGB565_BYTES_PER_PIXEL;

// ---- Кнопки ----
constexpr int8_t  PIN_KEY             = 0;
constexpr uint32_t KEY_LONG_PRESS_MS  = 3000;

// ---- Сенсорная кнопка (CST816T via I2C) ----
constexpr int8_t  TOUCH_SDA           = 3;
constexpr int8_t  TOUCH_SCL           = 2;
constexpr int8_t  TOUCH_IRQ           = 21;
constexpr uint8_t TOUCH_ADDR          = 0x15;
constexpr int16_t TOUCH_BTN_X         = 600;
constexpr int16_t TOUCH_BTN_Y         = 120;

// ---- Таймауты ----
constexpr uint32_t WIFI_CONNECT_TIMEOUT = 20000;

// ---- Зоны экрана (y, высота) ----
constexpr uint16_t ZONE_A_Y = 0;
constexpr uint16_t ZONE_A_H = 70;
constexpr uint16_t ZONE_B_Y = 70;
constexpr uint16_t ZONE_B_H = 64;
constexpr uint16_t ZONE_C_Y = 134;
constexpr uint16_t ZONE_C_H = 106;

// ---- Цвета (RGB565) ----
constexpr uint16_t CLR_BG        = 0x0000;
constexpr uint16_t CLR_TEXT      = 0xFFFF;
constexpr uint16_t CLR_STATUS_UP   = 0x07E0;
constexpr uint16_t CLR_STATUS_DN  = 0xF800;
constexpr uint16_t CLR_STATUS_UNC = 0xFFE0;
constexpr uint16_t CLR_PING      = 0x07FF;
constexpr uint16_t CLR_TRAFF_IN  = 0xAFE5;
constexpr uint16_t CLR_TRAFF_OUT = 0xFD20;
constexpr uint16_t CLR_DIM       = 0x39E7;

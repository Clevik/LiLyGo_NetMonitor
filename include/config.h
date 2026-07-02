#pragma once

#include <Arduino.h>

// Плато-зависимые параметры (пины дисплея, разрешение, touch, тип драйвера)
// выбираются по макросу HW_AMOLED_* из platformio.ini.
#include "hardware/hardware.h"

// ---- Размер canvas (зависит от разрешения платы) ----
constexpr uint8_t  RGB565_BYTES_PER_PIXEL = 2;
constexpr size_t   CANVAS_BUFFER_BYTES =
    static_cast<size_t>(SCREEN_W) * SCREEN_H * RGB565_BYTES_PER_PIXEL;

// ---- Кнопки ----
constexpr int8_t  PIN_KEY             = 0;
constexpr uint32_t KEY_LONG_PRESS_MS  = 3000;

// ---- Таймауты ----
constexpr uint32_t WIFI_CONNECT_TIMEOUT = 20000;

// ---- Телеметрия ----
// Верхний предел скорости (бит/с) для санитарной проверки: значения выше
// считаются артефактом (сброс счётчика / неполный SNMP-ответ) и отбрасываются.
// 100 Гбит/с — недостижимо для WAN, но далеко от мусорных ~1e19.
constexpr double MAX_REASONABLE_BPS = 1e11;

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
constexpr uint16_t CLR_STATUS_CONN = 0xAFE5;
constexpr uint16_t CLR_PING      = 0x07FF;
constexpr uint16_t CLR_DIM       = 0x39E7;

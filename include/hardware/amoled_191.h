#pragma once

// ============================================================
//  LilyGo T-Display S3 AMOLED 1.91"
//  Прямоугольный экран 536x240, драйвер RM67162 (QSPI)
//  Touch: CST816T (I2C 0x15)
// ============================================================

#define HW_DISP_RM67162

// ---- Пины дисплея QSPI (RM67162) ----
constexpr int8_t  DISP_CS      = 6;
constexpr int8_t  DISP_SCK     = 47;
constexpr int8_t  DISP_D0      = 18;
constexpr int8_t  DISP_D1      = 7;
constexpr int8_t  DISP_D2      = 48;
constexpr int8_t  DISP_D3      = 5;
constexpr int8_t  DISP_RST     = 17;
constexpr int8_t  DISP_TE      = 9;
constexpr int8_t  DISP_PWR_PIN = 38;

// ---- Разрешение (горизонтальная ориентация) ----
constexpr uint16_t SCREEN_W = 536;
constexpr uint16_t SCREEN_H = 240;
constexpr uint8_t  DISP_ROTATION = 1;

// ---- Touch: CST816T ----
#define HW_TOUCH_CST816T
constexpr int8_t  TOUCH_SDA   = 3;
constexpr int8_t  TOUCH_SCL   = 2;
constexpr int8_t  TOUCH_IRQ   = 21;
constexpr uint8_t TOUCH_ADDR  = 0x15;
// Координата виртуальной сенсорной кнопки (зона за пределами active area)
constexpr int16_t TOUCH_BTN_X = 600;
constexpr int16_t TOUCH_BTN_Y = 120;

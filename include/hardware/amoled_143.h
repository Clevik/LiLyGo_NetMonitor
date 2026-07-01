#pragma once

// ============================================================
//  LilyGo T-Display S3 AMOLED 1.43" / 1.75"
//  Круглый экран 466x466, драйвер CO5300 (QSPI)
//  (CO5300 совместим с ревизиями H0175Y003AM / DO0143FMST10;
//   для старой ревизии DO0143FAT01 нужен SH8601 — отсутствует
//   в Arduino_GFX 1.4.9, потребуется обновить библиотеку)
//  Touch: CST9217 (I2C, ревизия H0175Y003AM 1.75")
// ============================================================

#define HW_DISP_CO5300

// ---- Пины дисплея QSPI (CO5300) ----
constexpr int8_t  DISP_CS      = 10;
constexpr int8_t  DISP_SCK     = 12;
constexpr int8_t  DISP_D0      = 11;
constexpr int8_t  DISP_D1      = 13;
constexpr int8_t  DISP_D2      = 14;
constexpr int8_t  DISP_D3      = 15;
constexpr int8_t  DISP_RST     = 17;
constexpr int8_t  DISP_TE      = -1;   // не подключён
constexpr int8_t  DISP_PWR_PIN = 16;

// ---- Разрешение (круг/квадрат) ----
constexpr uint16_t SCREEN_W = 466;
constexpr uint16_t SCREEN_H = 466;
constexpr uint8_t  DISP_ROTATION = 0;

// ---- Touch: CST9217 ----
#define HW_TOUCH_CST9217
constexpr int8_t  TOUCH_SDA   = 7;
constexpr int8_t  TOUCH_SCL   = 6;
constexpr int8_t  TOUCH_IRQ   = 9;
constexpr uint8_t TOUCH_ADDR  = 0x5A;

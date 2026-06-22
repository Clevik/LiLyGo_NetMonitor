#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "touch.h"

#if defined(HW_TOUCH_CST816T)

static volatile bool g_irqFlag  = false;
static bool         g_waitRelease = false;

static bool readCst816(int16_t &x, int16_t &y, uint8_t &points) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5) != 5) return false;
  uint8_t buf[5];
  for (int i = 0; i < 5; i++) buf[i] = Wire.read();
  points = buf[0] & 0x0F;
  x = ((buf[1] & 0x0F) << 8) | buf[2];
  y = ((buf[3] & 0x0F) << 8) | buf[4];
  return true;
}

static void IRAM_ATTR onTouchIrq() {
  g_irqFlag = true;
}

bool touchInit() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), onTouchIrq, FALLING);

  Wire.beginTransmission(TOUCH_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[Touch] CST816T not found on I2C");
    return false;
  }

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0xA7);
  Wire.endTransmission(false);
  uint8_t chipId = 0;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)1) == 1) {
    chipId = Wire.read();
  }
  Serial.printf("[Touch] CST816T chipId=0x%02X\n", chipId);
  return true;
}

bool touchButtonPressed() {
  if (!g_irqFlag) return false;
  g_irqFlag = false;

  int16_t x, y;
  uint8_t points;
  if (!readCst816(x, y, points)) return false;

  if (points == 0) {
    g_waitRelease = false;
    return false;
  }

  if (g_waitRelease) return false;

  bool isBtn = (x == TOUCH_BTN_X && y == TOUCH_BTN_Y);
  if (isBtn) {
    Serial.printf("[Touch] button x=%d y=%d\n", x, y);
    g_waitRelease = true;
    return true;
  }
  return false;
}

#else

// Заглушка: сенсорный контроллер на этой плате не поддерживается.
// TODO: реализовать драйвер для FT3168 (T-Display S3 AMOLED 1.43/1.75).

bool touchInit() {
  Serial.println("[Touch] unsupported on this board");
  return false;
}

bool touchButtonPressed() {
  return false;
}

#endif

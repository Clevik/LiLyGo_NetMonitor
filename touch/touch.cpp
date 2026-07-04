#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "touch.h"

static uint16_t g_displayRotation = 0;

void touchSetRotation(uint16_t displayRotation) {
  g_displayRotation = displayRotation;
}

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

bool touchInit(uint16_t displayRotation) {
  touchSetRotation(displayRotation);
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

#elif defined(HW_TOUCH_CST9217)

#include <TouchDrv.hpp>

static constexpr uint32_t TOUCH_DEBOUNCE_MS = 250;

static TouchDrvCST92xx g_cst9217;
static volatile bool g_irqFlag = false;
static bool g_touchReady = false;
static uint32_t g_lastTapMs = 0;

static void IRAM_ATTR onTouchInterrupt() {
  g_irqFlag = true;
}

bool touchInit(uint16_t displayRotation) {
  touchSetRotation(displayRotation);
  g_touchReady = false;
  g_irqFlag = false;

  Wire.begin(TOUCH_SDA, TOUCH_SCL);

  // У CST9217 на этой плате нет отдельного reset-пина.
  g_cst9217.setPins(-1, TOUCH_IRQ);
  if (!g_cst9217.begin(Wire, TOUCH_ADDR, TOUCH_SDA, TOUCH_SCL)) {
    Serial.printf("[Touch] CST9217 not found at 0x%02X\n", TOUCH_ADDR);
    return false;
  }

  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), onTouchInterrupt, FALLING);

  g_touchReady = true;
  Serial.printf("[Touch] %s initialized at 0x%02X\n",
                g_cst9217.getModelName(), TOUCH_ADDR);
  return true;
}

bool touchReadTap(int16_t &x, int16_t &y) {
  if (!g_touchReady || !g_irqFlag) return false;
  g_irqFlag = false;

  const TouchPoints &points = g_cst9217.getTouchPoints();
  if (!points.hasPoints()) return false;
  const TouchPoint &point = points.getPoint(0);
  int16_t rawX = point.x;
  int16_t rawY = point.y;

  uint32_t now = millis();
  if (g_lastTapMs != 0 && now - g_lastTapMs < TOUCH_DEBOUNCE_MS) return false;
  g_lastTapMs = now;

  rawX = constrain(rawX, static_cast<int16_t>(0),
                   static_cast<int16_t>(SCREEN_W - 1));
  rawY = constrain(rawY, static_cast<int16_t>(0),
                   static_cast<int16_t>(SCREEN_H - 1));

  switch (g_displayRotation) {
    case 90:
      x = rawY;
      y = SCREEN_H - 1 - rawX;
      break;
    case 180:
      x = SCREEN_W - 1 - rawX;
      y = SCREEN_H - 1 - rawY;
      break;
    case 270:
      x = SCREEN_W - 1 - rawY;
      y = rawX;
      break;
    default:
      x = rawX;
      y = rawY;
      break;
  }
  Serial.printf("[Touch] tap x=%d y=%d\n", x, y);
  return true;
}

#else

bool touchInit(uint16_t displayRotation) {
  touchSetRotation(displayRotation);
  Serial.println("[Touch] unsupported on this board");
  return false;
}

bool touchButtonPressed() {
  return false;
}

#endif

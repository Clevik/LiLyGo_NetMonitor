#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_GFX_Library.h>

#include "config.h"
#include "ui.h"

static Arduino_DataBus *g_bus = nullptr;
static Arduino_GFX     *g_gfx = nullptr;

static char g_fmtBuf[32];

static void fillZone(uint16_t y, uint16_t h, uint16_t color) {
  g_gfx->fillRect(0, y, SCREEN_W, h, color);
}

bool uiInit() {
  pinMode(DISP_PWR_PIN, OUTPUT);
  digitalWrite(DISP_PWR_PIN, HIGH);

  g_bus = new Arduino_ESP32QSPI(
    DISP_CS, DISP_SCK,
    DISP_D0, DISP_D1, DISP_D2, DISP_D3);

  g_gfx = new Arduino_RM67162(g_bus, DISP_RST, 1, false);

  if (!g_gfx->begin()) {
    Serial.println("[UI] gfx->begin() failed!");
    return false;
  }

  g_gfx->fillScreen(CLR_BG);
  g_gfx->setTextColor(CLR_TEXT, CLR_BG);
  return true;
}

void uiShowSplash() {
  g_gfx->fillScreen(CLR_BG);
  g_gfx->setTextSize(4);
  g_gfx->setCursor(80, 90);
  g_gfx->setTextColor(CLR_PING);
  g_gfx->print("NETMONITOR");
  g_gfx->setTextSize(2);
  g_gfx->setCursor(180, 140);
  g_gfx->setTextColor(CLR_DIM);
  g_gfx->print("boot...");
}

void uiShowApConfig(const char *apName, const char *apIp) {
  g_gfx->fillScreen(CLR_BG);

  g_gfx->setTextSize(3);
  g_gfx->setTextColor(CLR_STATUS_DN);
  g_gfx->setCursor(20, 20);
  g_gfx->print("AP: ");
  g_gfx->setTextColor(CLR_TEXT);
  g_gfx->print(apName);

  g_gfx->setTextSize(2);
  g_gfx->setTextColor(CLR_DIM);
  g_gfx->setCursor(20, 70);
  g_gfx->print("Open http://");
  g_gfx->print(apIp);

  g_gfx->setCursor(20, 100);
  g_gfx->print("to configure device");

  g_gfx->setTextSize(2);
  g_gfx->setTextColor(CLR_PING);
  g_gfx->setCursor(20, 160);
  g_gfx->print("Hold KEY >3s to reset");
}

void uiShowConnecting(const char *ssid) {
  g_gfx->fillScreen(CLR_BG);
  g_gfx->setTextSize(3);
  g_gfx->setTextColor(CLR_TEXT);
  g_gfx->setCursor(20, 60);
  g_gfx->print("Connecting...");
  g_gfx->setTextSize(2);
  g_gfx->setTextColor(CLR_DIM);
  g_gfx->setCursor(20, 110);
  g_gfx->print("SSID: ");
  g_gfx->print(ssid);
}

void uiShowMain(const Telemetry &t) {
  g_gfx->fillScreen(CLR_BG);

  // --- Зона A: шапка ---
  fillZone(ZONE_A_Y, ZONE_A_H, CLR_BG);

  g_gfx->setTextSize(2);
  g_gfx->setTextColor(CLR_DIM);
  g_gfx->setCursor(8, ZONE_A_Y + 4);
  g_gfx->print("ROUTER");

  g_gfx->setTextSize(3);
  g_gfx->setTextColor(CLR_TEXT);
  g_gfx->setCursor(8, ZONE_A_Y + 26);
  g_gfx->print("192.168.1.1");

  // Статус UP/DOWN
  g_gfx->setTextSize(4);
  if (t.dataValid) {
    g_gfx->setTextColor(t.linkUp ? CLR_STATUS_UP : CLR_STATUS_DN);
    g_gfx->setCursor(300, ZONE_A_Y + 10);
    g_gfx->print(t.linkUp ? "UP" : "DOWN");
  } else {
    g_gfx->setTextColor(CLR_DIM);
    g_gfx->setCursor(300, ZONE_A_Y + 10);
    g_gfx->print("--");
  }

  // Пинг
  g_gfx->setTextSize(3);
  g_gfx->setTextColor(CLR_PING);
  g_gfx->setCursor(430, ZONE_A_Y + 4);
  if (t.pingValid) {
    snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%ums", t.pingMs);
    g_gfx->print(g_fmtBuf);
  } else {
    g_gfx->print("-- ms");
  }

  // Разделитель
  g_gfx->drawFastHLine(0, ZONE_A_Y + ZONE_A_H - 1, SCREEN_W, CLR_DIM);

  // --- Зона B: трафик ---
  fillZone(ZONE_B_Y, ZONE_B_H, CLR_BG);

  g_gfx->setTextSize(4);
  g_gfx->setTextColor(CLR_TRAFF_IN);
  g_gfx->setCursor(12, ZONE_B_Y + 10);
  g_gfx->print("\x19 ");
  snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%.1f", t.inMbps);
  g_gfx->print(g_fmtBuf);

  g_gfx->setTextSize(2);
  g_gfx->setCursor(260, ZONE_B_Y + 18);
  g_gfx->print("Mbps");

  g_gfx->setTextSize(4);
  g_gfx->setTextColor(CLR_TRAFF_OUT);
  g_gfx->setCursor(12, ZONE_B_Y + 50);
  g_gfx->print("\x18 ");
  snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%.1f", t.outMbps);
  g_gfx->print(g_fmtBuf);

  g_gfx->setTextSize(2);
  g_gfx->setCursor(260, ZONE_B_Y + 58);
  g_gfx->print("Mbps");

  // Разделитель
  g_gfx->drawFastHLine(0, ZONE_B_Y + ZONE_B_H - 1, SCREEN_W, CLR_DIM);

  // --- Зона C: заглушка графика + OTA IP ---
  fillZone(ZONE_C_Y, ZONE_C_H, CLR_BG);
  g_gfx->setTextSize(2);
  g_gfx->setTextColor(CLR_DIM);
  g_gfx->setCursor(200, ZONE_C_Y + 35);
  g_gfx->print("[graph]");

  g_gfx->setTextSize(2);
  g_gfx->setCursor(8, ZONE_C_Y + ZONE_C_H - 18);
  g_gfx->print("OTA: ");
  g_gfx->print(WiFi.localIP().toString());
}

void uiUpdateMain(const Telemetry &t) {
  uiShowMain(t);
}

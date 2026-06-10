#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_GFX_Library.h>

#include "config.h"
#include "ui.h"

static Arduino_DataBus *g_bus    = nullptr;
static Arduino_GFX     *g_disp   = nullptr;
static Arduino_Canvas  *g_canvas = nullptr;

static char g_fmtBuf[32];
static char g_connectSsid[64];
static char g_routerIp[20] = "192.168.1.1";

bool uiInit() {
  pinMode(DISP_PWR_PIN, OUTPUT);
  digitalWrite(DISP_PWR_PIN, HIGH);

  g_bus = new Arduino_ESP32QSPI(
    DISP_CS, DISP_SCK,
    DISP_D0, DISP_D1, DISP_D2, DISP_D3);

  g_disp = new Arduino_RM67162(g_bus, DISP_RST, 1, false);

  g_canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, g_disp);
  if (!g_canvas->begin()) {
    Serial.println("[UI] canvas->begin() failed!");
    return false;
  }

  g_canvas->fillScreen(CLR_BG);
  g_canvas->flush();
  g_canvas->setTextColor(CLR_TEXT, CLR_BG);
  return true;
}

static void flush() {
  g_canvas->flush();
}

void uiShowSplash() {
  g_canvas->fillScreen(CLR_BG);
  g_canvas->setTextSize(4);
  g_canvas->setCursor(80, 90);
  g_canvas->setTextColor(CLR_PING);
  g_canvas->print("NETMONITOR");
  g_canvas->setTextSize(2);
  g_canvas->setCursor(180, 140);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->print("boot...");
  flush();
}

static void formatSpeed(double bps, char *val, size_t vLen, const char **unit) {
  if (bps < 1000.0) {
    snprintf(val, vLen, "%.0f", bps);
    *unit = "bit/s";
    return;
  }
  double kbps = bps / 1000.0;
  if (kbps < 1.0) {
    snprintf(val, vLen, "%.0f", bps);
    *unit = "bit/s";
    return;
  }
  double mbps = kbps / 1000.0;
  if (mbps < 1.0) {
    snprintf(val, vLen, "%.1f", kbps);
    *unit = "Kbit/s";
  } else {
    snprintf(val, vLen, "%.1f", mbps);
    *unit = "Mbit/s";
  }
}

void uiShowApConfig(const char *apName, const char *apIp) {
  g_canvas->fillScreen(CLR_BG);

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_STATUS_DN);
  g_canvas->setCursor(20, 20);
  g_canvas->print("AP: ");
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->print(apName);

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(20, 70);
  g_canvas->print("Open http://");
  g_canvas->print(apIp);

  g_canvas->setCursor(20, 100);
  g_canvas->print("to configure device");

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_PING);
  g_canvas->setCursor(20, 160);
  g_canvas->print("Hold KEY >3s to reset");
  flush();
}

void uiShowConnecting(const char *ssid) {
  strncpy(g_connectSsid, ssid ? ssid : "", sizeof(g_connectSsid) - 1);
  g_connectSsid[sizeof(g_connectSsid) - 1] = '\0';
  uiUpdateConnecting();
}

void uiSetRouterIp(const char *ip) {
  strncpy(g_routerIp, ip ? ip : "", sizeof(g_routerIp) - 1);
  g_routerIp[sizeof(g_routerIp) - 1] = '\0';
}

void uiUpdateConnecting() {
  static const char *frames[] = {
    "....", "....", "....", "....", "...."
  };
  static const uint8_t active[] = { 0, 1, 2, 3, 4 };
  uint8_t frame = (millis() / 400) % 5;

  g_canvas->fillScreen(CLR_BG);

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(20, 60);
  g_canvas->print("Connecting ");

  for (int i = 0; i < 4; i++) {
    g_canvas->setTextColor(
      (frame < 4 && i == active[frame]) ? CLR_PING : CLR_DIM);
    g_canvas->print(".");
  }

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(20, 110);
  g_canvas->print("SSID: ");
  g_canvas->print(g_connectSsid);
  flush();
}

void uiShowMain(const Telemetry &t) {
  g_canvas->fillScreen(CLR_BG);

  // --- Зона A: шапка ---
  g_canvas->fillRect(0, ZONE_A_Y, SCREEN_W, ZONE_A_H, CLR_BG);

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(8, ZONE_A_Y + 4);
  g_canvas->print("ROUTER");

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(8, ZONE_A_Y + 26);
  g_canvas->print(g_routerIp);

  if (t.dataValid) {
    g_canvas->setTextSize(4);
    if (t.linkUncertain) {
      g_canvas->setTextColor(CLR_STATUS_UNC);
      g_canvas->setCursor(220, ZONE_A_Y + 21);
      g_canvas->print("UP(?)");
    } else if (t.linkUp) {
      g_canvas->setTextColor(CLR_STATUS_UP);
      g_canvas->setCursor(260, ZONE_A_Y + 21);
      g_canvas->print(" UP ");
    } else {
      g_canvas->setTextColor(CLR_STATUS_DN);
      g_canvas->setCursor(240, ZONE_A_Y + 21);
      g_canvas->print("DOWN");
    }
  } else {
    g_canvas->setTextSize(4);
    g_canvas->setTextColor(CLR_DIM);
    g_canvas->setCursor(240, ZONE_A_Y + 21);
    g_canvas->print("----");
  }

  g_canvas->setTextSize(3);

  const char *pingStr = nullptr;
  uint16_t pingColor  = CLR_PING;

  if (!t.dataValid) {
    static const char *frames[] = {"_- ms", "-_ ms", "-- ms"};
    pingStr  = frames[(millis() / 500) % 3];
    pingColor = CLR_DIM;
  } else if (t.pingLoss) {
    pingStr  = "100% loss";
    pingColor = CLR_STATUS_DN;
  } else if (t.pingMs == 0) {
    pingStr  = "-- ms";
  } else {
    snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%u ms", t.pingMs);
    pingStr = g_fmtBuf;
  }

  g_canvas->setTextColor(pingColor);
  {
    int len = 0;
    for (const char *p = pingStr; *p; ++p) len++;
    int16_t px = SCREEN_W - len * 18 - 8;
    g_canvas->setCursor(px, ZONE_A_Y + 26);
    g_canvas->print(pingStr);
  }

  g_canvas->drawFastHLine(0, ZONE_A_Y + ZONE_A_H - 1, SCREEN_W, CLR_DIM);

  // --- Зона B: трафик ---
  g_canvas->fillRect(0, ZONE_B_Y, SCREEN_W, ZONE_B_H, CLR_BG);

  g_canvas->setTextSize(4);
  g_canvas->setTextColor(CLR_TRAFF_IN);
  g_canvas->setCursor(12, ZONE_B_Y + 10);
  g_canvas->print("\x19 ");
  {
    char valBuf[16];
    const char *unit;
    if (t.inBps > 0.0) {
      formatSpeed(t.inBps, valBuf, sizeof(valBuf), &unit);
      g_canvas->print(valBuf);
      int cx = g_canvas->getCursorX();
      g_canvas->setTextSize(3);
      g_canvas->setCursor(cx + 24, ZONE_B_Y + 18);
      g_canvas->print(unit);
    } else {
      g_canvas->print("-");
      g_canvas->setTextSize(3);
      g_canvas->setCursor(12 + 24 * 3 + 24, ZONE_B_Y + 18);
      g_canvas->print("bit/s");
    }
  }

  g_canvas->setTextSize(4);
  g_canvas->setTextColor(CLR_TRAFF_OUT);
  g_canvas->setCursor(12, ZONE_B_Y + 50);
  g_canvas->print("\x18 ");
  {
    char valBuf[16];
    const char *unit;
    if (t.outBps > 0.0) {
      formatSpeed(t.outBps, valBuf, sizeof(valBuf), &unit);
      g_canvas->print(valBuf);
      int cx = g_canvas->getCursorX();
      g_canvas->setTextSize(3);
      g_canvas->setCursor(cx + 24, ZONE_B_Y + 58);
      g_canvas->print(unit);
    } else {
      g_canvas->print("-");
      g_canvas->setTextSize(3);
      g_canvas->setCursor(12 + 24 * 3 + 24, ZONE_B_Y + 58);
      g_canvas->print("bit/s");
    }
  }

  g_canvas->drawFastHLine(0, ZONE_B_Y + ZONE_B_H - 1, SCREEN_W, CLR_DIM);

  // --- Зона C: заглушка графика + OTA IP ---
  g_canvas->fillRect(0, ZONE_C_Y, SCREEN_W, ZONE_C_H, CLR_BG);
  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(200, ZONE_C_Y + 35);
  g_canvas->print("[graph]");

  g_canvas->setTextSize(2);
  g_canvas->setCursor(8, ZONE_C_Y + ZONE_C_H - 18);
  g_canvas->print("OTA: ");
  g_canvas->print(WiFi.localIP().toString());

  flush();
}

void uiUpdateMain(const Telemetry &t) {
  uiShowMain(t);
}

#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_types.h>

#include "config.h"
#include "ui.h"

static Arduino_DataBus *g_bus    = nullptr;
static Arduino_GFX     *g_disp   = nullptr;
static Arduino_Canvas  *g_canvas = nullptr;

static char g_fmtBuf[32];
static char g_connectSsid[64];
static char g_routerIp[20] = "192.168.1.1";

constexpr uint16_t GRAPH_POINTS = 120;
static float g_histIn[GRAPH_POINTS]  = {};
static float g_histOut[GRAPH_POINTS] = {};
static uint16_t g_histCount = 0;
static uint16_t g_histHead  = 0;
static uint32_t g_lastHistMs = 0;

static void logMemoryState(const char *stage) {
  Serial.printf(
    "[UI] memory %s: heap free=%u largest=%u min=%u; psram found=%s size=%u free=%u largest=%u min=%u\n",
    stage,
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(ESP.getMaxAllocHeap()),
    static_cast<unsigned>(ESP.getMinFreeHeap()),
    psramFound() ? "yes" : "no",
    static_cast<unsigned>(ESP.getPsramSize()),
    static_cast<unsigned>(ESP.getFreePsram()),
    static_cast<unsigned>(ESP.getMaxAllocPsram()),
    static_cast<unsigned>(ESP.getMinFreePsram()));
  Serial.printf(
    "[UI] heap caps %s: internal free=%u largest=%u; spiram free=%u largest=%u\n",
    stage,
    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

static void logCanvasFramebuffer(const char *stage) {
  if (!g_canvas) {
    Serial.printf("[UI] canvas %s: object is null\n", stage);
    return;
  }

  uint16_t *fb = g_canvas->getFramebuffer();
  if (!fb) {
    Serial.printf("[UI] canvas %s: framebuffer is null\n", stage);
    return;
  }

  Serial.printf(
    "[UI] canvas %s: framebuffer=%p allocated=%u bytes location=%s\n",
    stage,
    fb,
    static_cast<unsigned>(heap_caps_get_allocated_size(fb)),
    esp_ptr_external_ram(fb) ? "PSRAM" : (esp_ptr_internal(fb) ? "internal" : "unknown"));
}

static void pushHistory(float inBps, float outBps) {
  g_histIn[g_histHead]  = inBps;
  g_histOut[g_histHead] = outBps;
  g_histHead = (g_histHead + 1) % GRAPH_POINTS;
  if (g_histCount < GRAPH_POINTS) g_histCount++;
}

bool uiInit() {
  Serial.printf("[UI] canvas need: %ux%u RGB565 = %u bytes\n",
                static_cast<unsigned>(SCREEN_W),
                static_cast<unsigned>(SCREEN_H),
                static_cast<unsigned>(CANVAS_BUFFER_BYTES));
  logMemoryState("before init");

  pinMode(DISP_PWR_PIN, OUTPUT);
  digitalWrite(DISP_PWR_PIN, HIGH);

  g_bus = new Arduino_ESP32QSPI(
    DISP_CS, DISP_SCK,
    DISP_D0, DISP_D1, DISP_D2, DISP_D3);

  g_disp = new Arduino_RM67162(g_bus, DISP_RST, 1, false);

  g_canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, g_disp);
  if (!g_canvas->begin()) {
    Serial.printf("[UI] canvas->begin() failed: need %u bytes, psram found=%s\n",
                  static_cast<unsigned>(CANVAS_BUFFER_BYTES),
                  psramFound() ? "yes" : "no");
    logCanvasFramebuffer("after failed begin");
    logMemoryState("after failed begin");
    return false;
  }
  logCanvasFramebuffer("after begin");
  logMemoryState("after begin");

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
    *unit = "bit";
    return;
  }
  double kbps = bps / 1000.0;
  if (kbps < 1.0) {
    snprintf(val, vLen, "%.0f", bps);
    *unit = "bit";
    return;
  }
  double mbps = kbps / 1000.0;
  if (mbps < 1.0) {
    snprintf(val, vLen, "%.0f", kbps);
    *unit = "Kbit";
  } else {
    snprintf(val, vLen, "%.1f", mbps);
    *unit = "Mbit";
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
  if (t.dataValid) {
    uint32_t now = millis();
    if (now - g_lastHistMs >= 1000) {
      g_lastHistMs = now;
      pushHistory((float)t.inBps, (float)t.outBps);
    }
  }

  g_canvas->fillScreen(CLR_BG);

  // --- Зона A: шапка (70px) ---
  // Левая колонка: ROUTER + IP
  // Центр: статус UP/DOWN
  // Правая колонка: пинг до хоста + пинг до роутера

  g_canvas->fillRect(0, ZONE_A_Y, SCREEN_W, ZONE_A_H, CLR_BG);

  // ROUTER (textSize 3 = 18x24)
  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(8, ZONE_A_Y + 4);
  g_canvas->print("ROUTER");

  // IP роутера внизу зоны (textSize 3, y + 70 - 24 - 4 = 42)
  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(8, ZONE_A_Y + 44);
  g_canvas->print(g_routerIp);

  // Статус UP/DOWN по центру вертикали (textSize 4 = 24x32, center y = 70/2-16=19)
  if (t.dataValid) {
    g_canvas->setTextSize(4);
    if (t.linkUp) {
      if (t.pingLoss && !t.linkUncertain) {
        g_canvas->setTextColor(CLR_STATUS_DN);
      } else if (t.linkUncertain) {
        g_canvas->setTextColor(CLR_STATUS_UNC);
      } else {
        g_canvas->setTextColor(CLR_STATUS_UP);
      }
      g_canvas->setCursor(275, ZONE_A_Y + 19);
      g_canvas->print(" UP ");
    } else {
      if (t.pingValid && !t.pingLoss) {
        g_canvas->setTextColor(CLR_STATUS_UNC);
      } else {
        g_canvas->setTextColor(CLR_STATUS_DN);
      }
      g_canvas->setCursor(255, ZONE_A_Y + 19);
      g_canvas->print("DOWN");
    }
  } else {
    g_canvas->setTextSize(4);
    g_canvas->setTextColor(CLR_DIM);
    g_canvas->setCursor(255, ZONE_A_Y + 19);
    g_canvas->print("----");
  }

  // Правая колонка: пинги (textSize 3 = 18x24)
  // Верхний: пинг до внешнего хоста
  {
    const char *pingStr = nullptr;
    uint16_t pingColor  = CLR_PING;

    if (!t.dataValid) {
      static const char *frames[] = {"_- ms", "-_ ms", "-- ms"};
      pingStr  = frames[(millis() / 500) % 3];
      pingColor = CLR_DIM;
    } else if (t.pingLoss) {
      pingStr  = "loss";
      pingColor = CLR_STATUS_DN;
    } else if (t.pingMs == 0) {
      pingStr  = "-- ms";
    } else {
      snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%u ms", t.pingMs);
      pingStr = g_fmtBuf;
    }

    g_canvas->setTextSize(3);
    g_canvas->setTextColor(pingColor);
    int len = strlen(pingStr);
    int16_t px = SCREEN_W - len * 18 - 8;
    g_canvas->setCursor(px, ZONE_A_Y + 4);
    g_canvas->print(pingStr);
  }

  // Нижний: пинг до роутера
  {
    g_canvas->setTextSize(3);
    g_canvas->setTextColor(CLR_PING);
    const char *rStr;
    char rBuf[16];
    if (!t.dataValid) {
      rStr = "-- ms";
    } else if (t.routerPingValid) {
      snprintf(rBuf, sizeof(rBuf), "%u ms", t.routerPingMs);
      rStr = rBuf;
    } else {
      rStr = "-- ms";
    }
    int len = strlen(rStr);
    int16_t px = SCREEN_W - len * 18 - 8;
    g_canvas->setCursor(px, ZONE_A_Y + 44);
    g_canvas->print(rStr);
  }

  // --- Зона B: трафик (одна строка) ---
  g_canvas->fillRect(0, ZONE_B_Y, SCREEN_W, ZONE_B_H, CLR_BG);

  {
    const int16_t rowY = ZONE_B_Y + 16;

    // Входящий (слева)
    g_canvas->setTextSize(4);
    g_canvas->setTextColor(CLR_TRAFF_IN);
    g_canvas->setCursor(12, rowY);
    g_canvas->print("\x19 ");
    {
      char valBuf[16];
      const char *unit = "bit";
      if (t.inBps > 0.0) {
        formatSpeed(t.inBps, valBuf, sizeof(valBuf), &unit);
        g_canvas->print(valBuf);
      } else {
        g_canvas->print("-");
      }
      int16_t ux = g_canvas->getCursorX();
      g_canvas->setTextSize(3);
      g_canvas->setCursor(ux, rowY + 8);
      g_canvas->print(" ");
      g_canvas->print(unit);
    }

    // Исходящий (левый край = центр экрана)
    constexpr int16_t OUT_X = SCREEN_W / 2;
    g_canvas->setTextSize(4);
    g_canvas->setTextColor(CLR_TRAFF_OUT);
    g_canvas->setCursor(OUT_X, rowY);
    g_canvas->print("\x18 ");
    {
      char valBuf[16];
      const char *unit = "bit";
      if (t.outBps > 0.0) {
        formatSpeed(t.outBps, valBuf, sizeof(valBuf), &unit);
        g_canvas->print(valBuf);
      } else {
        g_canvas->print("-");
      }
      int16_t ux = g_canvas->getCursorX();
      g_canvas->setTextSize(3);
      g_canvas->setCursor(ux, rowY + 8);
      g_canvas->print(" ");
      g_canvas->print(unit);
    }
  }

  // --- Зона C: график трафика ---
  {
    constexpr int16_t OTA_H = 24;
    constexpr int16_t GX = 0;
    constexpr int16_t GY = ZONE_C_Y;
    constexpr int16_t GW = SCREEN_W;
    constexpr int16_t GH = ZONE_C_H - OTA_H;

    g_canvas->fillRect(0, ZONE_C_Y, SCREEN_W, ZONE_C_H, CLR_BG);

    if (g_histCount >= 2) {
      float maxIn = 1.0f, maxOut = 1.0f;
      uint16_t start = (g_histHead + GRAPH_POINTS - g_histCount) % GRAPH_POINTS;
      for (uint16_t i = 0; i < g_histCount; i++) {
        uint16_t idx = (start + i) % GRAPH_POINTS;
        if (g_histIn[idx] > maxIn)   maxIn = g_histIn[idx];
        if (g_histOut[idx] > maxOut) maxOut = g_histOut[idx];
      }

      constexpr int16_t HALF_W = GW / 2;
      float xStep = (float)HALF_W / (float)(GRAPH_POINTS - 1);
      int16_t bottom = GY + GH - 1;

      auto drawHalf = [&](const float *data, float maxVal, int16_t offsetX, uint16_t lineColor) {
        int16_t prevPx = -1, prevPy = -1;
        for (uint16_t i = 0; i < g_histCount; i++) {
          uint16_t idx = (start + i) % GRAPH_POINTS;
          int16_t px = offsetX + (int16_t)(i * xStep);
          int16_t py = bottom - (int16_t)((data[idx] / maxVal) * (GH - 1));
          if (prevPx >= 0) {
            g_canvas->drawLine(prevPx, prevPy, px, py, lineColor);
            g_canvas->drawLine(prevPx, prevPy + 1, px, py + 1, lineColor);
          }
          prevPx = px;
          prevPy = py;
        }
      };

      drawHalf(g_histIn, maxIn, GX, CLR_TRAFF_IN);
      drawHalf(g_histOut, maxOut, GX + HALF_W, CLR_TRAFF_OUT);
    }

    g_canvas->setTextSize(2);
    g_canvas->setTextColor(CLR_DIM);
    g_canvas->setCursor(8, ZONE_C_Y + ZONE_C_H - 18);
    g_canvas->print("OTA: ");
    g_canvas->print(WiFi.localIP().toString());
  }

  flush();
}

void uiUpdateMain(const Telemetry &t) {
  uiShowMain(t);
}

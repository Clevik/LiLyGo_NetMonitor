#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_types.h>
#include <cmath>
#include <cstring>

#include "config.h"
#if defined(HW_AMOLED_143)
  #include "globe_frames.h"
#endif
#include "ui.h"
#include "logging.h"

static Arduino_DataBus *g_bus    = nullptr;
static Arduino_GFX     *g_disp   = nullptr;
static Arduino_Canvas  *g_canvas = nullptr;

static char g_fmtBuf[32];
static char g_connectSsid[64];
static char g_routerIp[20] = "192.168.1.1";

static constexpr uint8_t BRIGHTNESS_LEVELS[] = {0xFF, 0xBF, 0x80, 0x4D, 0x00};
static constexpr uint8_t BRIGHTNESS_COUNT = 5;
static uint8_t g_brightnessIdx = 0;
static bool g_redrawRequested = false;

#define DISP_CMD_BRIGHTNESS 0x51
#define DISP_CMD_DISPON     0x29
#define DISP_CMD_DISPOFF    0x28

constexpr uint16_t GRAPH_POINTS = 120;
static float g_histIn[GRAPH_POINTS]  = {};
static float g_histOut[GRAPH_POINTS] = {};
static uint16_t g_histCount = 0;
static uint16_t g_histHead  = 0;
static uint32_t g_lastHistMs = 0;

static void logMemoryState(const char *stage) {
  LOG_D(
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
  LOG_D(
    "[UI] heap caps %s: internal free=%u largest=%u; spiram free=%u largest=%u\n",
    stage,
    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

static void logCanvasFramebuffer(const char *stage) {
  if (!g_canvas) {
    LOG_D("[UI] canvas %s: object is null\n", stage);
    return;
  }

  uint16_t *fb = g_canvas->getFramebuffer();
  if (!fb) {
    LOG_D("[UI] canvas %s: framebuffer is null\n", stage);
    return;
  }

  LOG_D(
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
  LOG_D("[UI] canvas need: %ux%u RGB565 = %u bytes\n",
        static_cast<unsigned>(SCREEN_W),
        static_cast<unsigned>(SCREEN_H),
        static_cast<unsigned>(CANVAS_BUFFER_BYTES));
  logMemoryState("before init");

  pinMode(DISP_PWR_PIN, OUTPUT);
  digitalWrite(DISP_PWR_PIN, HIGH);

  g_bus = new Arduino_ESP32QSPI(
    DISP_CS, DISP_SCK,
    DISP_D0, DISP_D1, DISP_D2, DISP_D3);

#if defined(HW_DISP_RM67162)
  g_disp = new Arduino_RM67162(g_bus, DISP_RST, DISP_ROTATION, false);
#elif defined(HW_DISP_CO5300)
  g_disp = new Arduino_CO5300(g_bus, DISP_RST, DISP_ROTATION, false,
                              SCREEN_W, SCREEN_H);
#else
  #error "Неизвестный драйвер дисплея. Определите HW_DISP_* в hardware/*.h"
#endif

  g_canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, g_disp);
  if (!g_canvas->begin()) {
    LOG_E("[UI] canvas->begin() failed: need %u bytes, psram found=%s\n",
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

enum class SpeedFormatStyle : uint8_t {
  RectValue,
  RoundCompact,
  GraphLabel,
};

static void formatSpeedText(double bps,
                            SpeedFormatStyle style,
                            char *val,
                            size_t vLen,
                            const char **unit) {
  if (!val || vLen == 0 || !unit) return;

  if (!std::isfinite(bps) || bps <= 0.0) {
    snprintf(val, vLen, (style == SpeedFormatStyle::RoundCompact) ? "-" : "0");
    *unit = (style == SpeedFormatStyle::RoundCompact) ? "Mbps" :
            (style == SpeedFormatStyle::GraphLabel) ? "bt" : "bit";
    return;
  }

  double kbps = bps / 1000.0;
  double mbps = kbps / 1000.0;

  switch (style) {
    case SpeedFormatStyle::RectValue:
      if (kbps < 1.0) {
        snprintf(val, vLen, "%.0f", bps);
        *unit = "bit";
      } else if (mbps < 1.0) {
        snprintf(val, vLen, "%.0f", kbps);
        *unit = "Kbit";
      } else {
        snprintf(val, vLen, "%.1f", mbps);
        *unit = "Mbit";
      }
      break;

    case SpeedFormatStyle::RoundCompact:
      if (mbps >= 1.0) {
        snprintf(val, vLen, (mbps < 100.0) ? "%.1f" : "%.0f", mbps);
        *unit = "Mbps";
      } else if (kbps >= 1.0) {
        snprintf(val, vLen, "%.0f", kbps);
        *unit = "Kbps";
      } else {
        snprintf(val, vLen, "%.0f", bps);
        *unit = "bps";
      }
      break;

    case SpeedFormatStyle::GraphLabel:
      if (kbps < 1.0) {
        snprintf(val, vLen, "%.0f", bps);
        *unit = "bt";
      } else if (mbps < 1.0) {
        snprintf(val, vLen, "%.0f", kbps);
        *unit = "Kb";
      } else {
        snprintf(val, vLen, (mbps < 10.0) ? "%.1f" : "%.0f", mbps);
        *unit = "Mb";
      }
      break;
  }
}

static constexpr uint32_t ROUTER_INFO_SWITCH_MS = 5000;

enum class RouterInfoMode : uint8_t {
  Ip,
  SystemUptime,
  WanUptime,
};

static RouterInfoMode currentRouterInfoMode(const Telemetry &t) {
  uint8_t count = 1;
  if (t.systemUptimeValid) count++;
  if (t.wanUptimeValid) count++;

  uint8_t slot = (millis() / ROUTER_INFO_SWITCH_MS) % count;
  if (slot == 0) return RouterInfoMode::Ip;
  if (t.systemUptimeValid && slot == 1) return RouterInfoMode::SystemUptime;
  return RouterInfoMode::WanUptime;
}

static void formatInterfaceUptime(uint32_t uptimeSec, char *out, size_t outLen) {
  uint32_t totalMinutes = uptimeSec / 60U;
  uint32_t days = totalMinutes / (24U * 60U);
  uint32_t hours = (totalMinutes / 60U) % 24U;
  uint32_t minutes = totalMinutes % 60U;

  if (days > 0) {
    snprintf(out, outLen, "%lud %luh %lum",
             static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  } else if (hours > 0) {
    snprintf(out, outLen, "%luh %lum",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  } else {
    snprintf(out, outLen, "%lum",
             static_cast<unsigned long>(minutes));
  }
}

static void formatRouterInfo(const Telemetry &t,
                             char *title,
                             size_t titleLen,
                             char *value,
                             size_t valueLen) {
  switch (currentRouterInfoMode(t)) {
    case RouterInfoMode::SystemUptime:
      snprintf(title, titleLen, "UPTIME SYSTEM");
      formatInterfaceUptime(t.systemUptimeSec, value, valueLen);
      return;
    case RouterInfoMode::WanUptime:
      snprintf(title, titleLen, "UPTIME WAN");
      formatInterfaceUptime(t.wanUptimeSec, value, valueLen);
      return;
    case RouterInfoMode::Ip:
    default:
      snprintf(title, titleLen, "ROUTER");
      snprintf(value, valueLen, "%s", g_routerIp);
      return;
  }
}

static void updateTrafficHistory(const Telemetry &t) {
  if (!t.dataValid) return;

  uint32_t now = millis();
  if (now - g_lastHistMs >= 1000) {
    g_lastHistMs = now;
    pushHistory((float)t.inBps, (float)t.outBps);
  }
}

void uiObserveTelemetry(const Telemetry &t) {
  updateTrafficHistory(t);
}

static const char *wanConnectionStateText(WanConnectionState state) {
  switch (state) {
    case WanConnectionState::Disconnected:
      return "DOWN";
    case WanConnectionState::Initializing:
      return "INIT";
    case WanConnectionState::WaitingForNetwork:
      return "WAIT";
    case WanConnectionState::Requesting:
    case WanConnectionState::Other:
      return "CONN";
    case WanConnectionState::Connected:
      return "UP";
    case WanConnectionState::Unknown:
    default:
      return "----";
  }
}

static uint16_t wanConnectionStateRectColor(WanConnectionState state) {
  switch (state) {
    case WanConnectionState::Disconnected:
      return CLR_STATUS_DN;
    case WanConnectionState::Initializing:
    case WanConnectionState::WaitingForNetwork:
      return CLR_STATUS_UNC;
    case WanConnectionState::Requesting:
    case WanConnectionState::Other:
      return CLR_STATUS_CONN;
    case WanConnectionState::Connected:
      return CLR_STATUS_UP;
    case WanConnectionState::Unknown:
    default:
      return CLR_DIM;
  }
}

static const char *statusText(const Telemetry &t) {
  if (!t.dataValid) return "----";
  if (t.wanConnectionStateValid) {
    return wanConnectionStateText(t.wanConnectionState);
  }
  return t.linkUp ? "UP" : "DOWN";
}

static uint16_t statusColorRect(const Telemetry &t) {
  if (!t.dataValid) return CLR_DIM;
  if (t.wanConnectionStateValid) {
    return wanConnectionStateRectColor(t.wanConnectionState);
  }
  if (t.linkUp) {
    if (t.pingLoss && !t.linkUncertain) return CLR_STATUS_DN;
    if (t.linkUncertain) return CLR_STATUS_UNC;
    return CLR_STATUS_UP;
  }
  if (t.pingValid && !t.pingLoss) return CLR_STATUS_UNC;
  return CLR_STATUS_DN;
}

#if defined(HW_AMOLED_143)
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t CLR_ROUND_DOWNLOAD = rgb565(0x00, 0xFF, 0x40);
static constexpr uint16_t CLR_ROUND_UPLOAD   = rgb565(0x00, 0x80, 0xFF);
static constexpr uint16_t CLR_ROUND_WARN     = rgb565(0xFF, 0xB0, 0x00);
static constexpr uint16_t CLR_ROUND_ALARM    = rgb565(0xFF, 0x30, 0x30);
static constexpr uint16_t CLR_ROUND_DIM_BLUE = rgb565(0x00, 0x18, 0x30);
static constexpr uint16_t CLR_ROUND_DIM_GN   = rgb565(0x00, 0x28, 0x10);

namespace RoundLayout {
constexpr int16_t CENTER_X = SCREEN_W / 2;
constexpr int16_t CENTER_Y = SCREEN_H / 2;

constexpr int16_t OUTER_RING_RADIUS = 228;
constexpr int16_t INNER_RING_RADIUS = 223;

constexpr int16_t HISTORY_ARC_RADIUS = 205;
constexpr uint16_t HISTORY_ARC_MAX_SAMPLES = 60;
constexpr float HISTORY_IN_START_DEG = 130.0f;
constexpr float HISTORY_IN_END_DEG = 230.0f;
constexpr float HISTORY_OUT_START_DEG = -50.0f;
constexpr float HISTORY_OUT_END_DEG = 50.0f;

constexpr int16_t LINE_GRAPH_X = 60;
constexpr int16_t LINE_GRAPH_W = 346;
constexpr int16_t LINE_GRAPH_IN_PDF_Y = 72;
constexpr int16_t LINE_GRAPH_IN_H = 28;
constexpr int16_t LINE_GRAPH_OUT_PDF_Y = 35;
constexpr int16_t LINE_GRAPH_OUT_H = 24;

constexpr int16_t LEFT_METRIC_X = 115;
constexpr int16_t RIGHT_METRIC_X = 351;
constexpr int16_t GLOBE_PDF_Y = 255;
constexpr int16_t PING_ICON_PDF_Y = 275;
constexpr int16_t PING_VALUE_PDF_Y = 210;

constexpr int16_t ROUTER_TITLE_Y = 14;
constexpr int16_t ROUTER_VALUE_PDF_Y = 425;
constexpr int16_t STATUS_PDF_Y = 360;

constexpr int16_t SPEED_ARROW_PDF_Y = 120;
constexpr int16_t SPEED_VALUE_PDF_Y = 130;
constexpr int16_t SPEED_UNIT_PDF_Y = 95;
constexpr int16_t SPEED_IN_VALUE_X = 165;
constexpr int16_t SPEED_OUT_ARROW_X = 290;
constexpr int16_t SPEED_OUT_VALUE_X = 340;

constexpr int16_t CENTER_DIVIDER_TOP_PDF_Y = 124;
constexpr int16_t CENTER_DIVIDER_BOTTOM_PDF_Y = 78;

constexpr int16_t DEVICE_TITLE_Y = 426;
constexpr int16_t DEVICE_IP_Y = 452;

constexpr uint16_t GLOBE_FRAME_MS = 42;
constexpr int16_t GLOBE_RADIUS = 80;
constexpr int16_t GLOBE_INNER_RING_RADIUS = 78;

constexpr int16_t PING_LOSS_TEXT_Y_OFFSET = -5;
constexpr int16_t PING_LOSS_UNIT_Y_OFFSET = 30;
constexpr int16_t PING_VALUE_Y_OFFSET = -7;
constexpr int16_t PING_UNIT_Y_OFFSET = 31;
}  // namespace RoundLayout

static int16_t centerYFromPdf(int16_t pdfY) {
  return static_cast<int16_t>(SCREEN_H) - pdfY;
}

static int16_t textPixelWidth(const char *text, uint8_t size) {
  return static_cast<int16_t>(std::strlen(text) * 6U * size);
}

static int16_t textPixelHeight(uint8_t size) {
  return static_cast<int16_t>(8U * size);
}

static void drawTextCentered(const char *text,
                             int16_t centerX,
                             int16_t centerY,
                             uint8_t size,
                             uint16_t color,
                             uint16_t bg = CLR_BG) {
  g_canvas->setTextSize(size);
  g_canvas->setTextColor(color, bg);
  g_canvas->setCursor(centerX - textPixelWidth(text, size) / 2,
                      centerY - textPixelHeight(size) / 2);
  g_canvas->print(text);
}

static void drawTextCenteredGlow(const char *text,
                                 int16_t centerX,
                                 int16_t centerY,
                                 uint8_t size,
                                 uint16_t color,
                                 uint16_t glow) {
  drawTextCentered(text, centerX - 1, centerY, size, glow);
  drawTextCentered(text, centerX + 1, centerY, size, glow);
  drawTextCentered(text, centerX, centerY - 1, size, glow);
  drawTextCentered(text, centerX, centerY + 1, size, glow);
  drawTextCentered(text, centerX, centerY, size, color);
}

static uint16_t wanConnectionStateRoundColor(WanConnectionState state) {
  switch (state) {
    case WanConnectionState::Disconnected:
      return CLR_ROUND_ALARM;
    case WanConnectionState::Initializing:
    case WanConnectionState::WaitingForNetwork:
      return CLR_ROUND_WARN;
    case WanConnectionState::Requesting:
    case WanConnectionState::Other:
      return CLR_STATUS_CONN;
    case WanConnectionState::Connected:
      return CLR_ROUND_DOWNLOAD;
    case WanConnectionState::Unknown:
    default:
      return CLR_DIM;
  }
}

static uint16_t statusColor(const Telemetry &t) {
  if (!t.dataValid) return CLR_DIM;
  if (t.wanConnectionStateValid) {
    return wanConnectionStateRoundColor(t.wanConnectionState);
  }
  if (t.linkUp) {
    if (t.pingLoss && !t.linkUncertain) return CLR_ROUND_ALARM;
    if (t.linkUncertain) return CLR_ROUND_WARN;
    return CLR_ROUND_DOWNLOAD;
  }
  if (t.pingValid && !t.pingLoss) return CLR_ROUND_WARN;
  return CLR_ROUND_ALARM;
}

static uint16_t historyStartIndex() {
  return (g_histHead + GRAPH_POINTS - g_histCount) % GRAPH_POINTS;
}

static uint16_t historyIndexFromOldest(uint16_t offset) {
  return (historyStartIndex() + offset) % GRAPH_POINTS;
}

static float historyMax(const float *data, uint16_t offset, uint16_t count) {
  float maxVal = 1.0f;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = historyIndexFromOldest(offset + i);
    if (data[idx] > maxVal) maxVal = data[idx];
  }
  return maxVal;
}

static void drawLineGraph(const float *data,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          uint16_t color,
                          uint16_t glowColor) {
  if (g_histCount < 2) return;

  uint16_t samples = g_histCount;
  if (samples > static_cast<uint16_t>(w)) samples = static_cast<uint16_t>(w);
  uint16_t offset = g_histCount - samples;
  float maxVal = historyMax(data, offset, samples);

  int16_t prevX = -1;
  int16_t prevY = -1;
  for (uint16_t i = 0; i < samples; i++) {
    uint16_t idx = historyIndexFromOldest(offset + i);
    int16_t px = x + static_cast<int16_t>((static_cast<int32_t>(i) * (w - 1)) / (samples - 1));
    int16_t py = y + h - 1 - static_cast<int16_t>((data[idx] / maxVal) * (h - 1));
    if (prevX >= 0) {
      g_canvas->drawLine(prevX, prevY + 2, px, py + 2, glowColor);
      g_canvas->drawLine(prevX, prevY + 1, px, py + 1, glowColor);
      g_canvas->drawLine(prevX, prevY, px, py, color);
    }
    prevX = px;
    prevY = py;
  }
}

static void drawArcSegment(int16_t cx,
                           int16_t cy,
                           int16_t radius,
                           float startDeg,
                           float endDeg,
                           uint16_t color,
                           uint8_t thickness = 1) {
  static constexpr float DEG = 3.1415926535f / 180.0f;
  int16_t prevX = 0;
  int16_t prevY = 0;
  bool havePrev = false;
  float step = (endDeg >= startDeg) ? 4.0f : -4.0f;
  for (float a = startDeg; (step > 0.0f) ? (a <= endDeg) : (a >= endDeg); a += step) {
    float rad = a * DEG;
    int16_t px = cx + static_cast<int16_t>(cosf(rad) * radius);
    int16_t py = cy - static_cast<int16_t>(sinf(rad) * radius);
    if (havePrev) {
      for (uint8_t t = 0; t < thickness; t++) {
        g_canvas->drawLine(prevX, prevY + t, px, py + t, color);
      }
    }
    prevX = px;
    prevY = py;
    havePrev = true;
  }
}

static void drawHistoryArc(const float *data,
                           float startDeg,
                           float endDeg,
                           uint16_t color,
                           uint16_t dimColor) {
  static constexpr float DEG = 3.1415926535f / 180.0f;

  drawArcSegment(RoundLayout::CENTER_X, RoundLayout::CENTER_Y,
                 RoundLayout::HISTORY_ARC_RADIUS, startDeg, endDeg,
                 dimColor, 2);

  if (g_histCount < 2) return;

  uint16_t samples = g_histCount;
  if (samples > RoundLayout::HISTORY_ARC_MAX_SAMPLES) {
    samples = RoundLayout::HISTORY_ARC_MAX_SAMPLES;
  }
  uint16_t offset = g_histCount - samples;
  float maxVal = historyMax(data, offset, samples);

  for (uint16_t i = 0; i < samples; i++) {
    uint16_t idx = historyIndexFromOldest(offset + i);
    float pos = (samples <= 1) ? 0.0f : (float)i / (float)(samples - 1);
    float angle = startDeg + (endDeg - startDeg) * pos;
    float norm = data[idx] / maxVal;
    if (norm < 0.04f) norm = 0.04f;
    if (norm > 1.0f) norm = 1.0f;

    int16_t len = 4 + static_cast<int16_t>(norm * 24.0f);
    float rad = angle * DEG;
    int16_t x0 = RoundLayout::CENTER_X +
                 static_cast<int16_t>(cosf(rad) * (RoundLayout::HISTORY_ARC_RADIUS + 2));
    int16_t y0 = RoundLayout::CENTER_Y -
                 static_cast<int16_t>(sinf(rad) * (RoundLayout::HISTORY_ARC_RADIUS + 2));
    int16_t x1 = RoundLayout::CENTER_X +
                 static_cast<int16_t>(cosf(rad) *
                                      (RoundLayout::HISTORY_ARC_RADIUS + 2 - len));
    int16_t y1 = RoundLayout::CENTER_Y -
                 static_cast<int16_t>(sinf(rad) *
                                      (RoundLayout::HISTORY_ARC_RADIUS + 2 - len));
    g_canvas->drawLine(x0, y0, x1, y1, color);
    g_canvas->drawLine(x0, y0 + 1, x1, y1 + 1, color);
  }
}

static void drawEllipse(int16_t cx,
                        int16_t cy,
                        int16_t rx,
                        int16_t ry,
                        uint16_t color) {
  int16_t prevX = 0;
  int16_t prevY = 0;
  bool havePrev = false;
  for (int deg = 0; deg <= 360; deg += 8) {
    float rad = deg * (3.1415926535f / 180.0f);
    int16_t px = cx + static_cast<int16_t>(cosf(rad) * rx);
    int16_t py = cy + static_cast<int16_t>(sinf(rad) * ry);
    if (havePrev) g_canvas->drawLine(prevX, prevY, px, py, color);
    prevX = px;
    prevY = py;
    havePrev = true;
  }
}

static void drawWifiIcon(int16_t x, int16_t y, uint16_t color) {
  int16_t baseY = y + 22;
  drawArcSegment(x, baseY, 36, 46.0f, 134.0f, color, 4);
  drawArcSegment(x, baseY, 25, 50.0f, 130.0f, color, 4);
  drawArcSegment(x, baseY, 14, 55.0f, 125.0f, color, 4);
  g_canvas->fillCircle(x, baseY - 1, 5, color);
}

static void drawGlobeIcon(int16_t x, int16_t y, uint16_t color) {
  g_canvas->drawCircle(x, y, 29, color);
  g_canvas->drawCircle(x, y, 28, color);
  g_canvas->drawLine(x - 28, y, x + 28, y, color);
  g_canvas->drawLine(x, y - 28, x, y + 28, color);
  drawEllipse(x, y, 12, 28, color);
  drawEllipse(x, y, 22, 28, color);
}

static void drawDownArrow(int16_t x, int16_t y, uint16_t color) {
  g_canvas->fillRect(x - 5, y - 26, 10, 34, color);
  g_canvas->fillTriangle(x - 22, y + 4, x + 22, y + 4, x, y + 29, color);
}

static void drawUpArrow(int16_t x, int16_t y, uint16_t color) {
  g_canvas->fillRect(x - 5, y - 8, 10, 34, color);
  g_canvas->fillTriangle(x - 22, y - 4, x + 22, y - 4, x, y - 29, color);
}

static void drawGlobeFrame(int16_t centerX, int16_t centerY) {
  uint8_t frame = (millis() / RoundLayout::GLOBE_FRAME_MS) % GLOBE_FRAME_COUNT;
  int16_t x0 = centerX - (GLOBE_MASK_W * GLOBE_CELL_PX) / 2;
  int16_t y0 = centerY - (GLOBE_MASK_H * GLOBE_CELL_PX) / 2;

  g_canvas->fillCircle(centerX, centerY, RoundLayout::GLOBE_RADIUS,
                       rgb565(0x00, 0x06, 0x10));
  g_canvas->drawCircle(centerX, centerY, RoundLayout::GLOBE_RADIUS,
                       CLR_ROUND_UPLOAD);
  g_canvas->drawCircle(centerX, centerY, RoundLayout::GLOBE_INNER_RING_RADIUS,
                       CLR_ROUND_DIM_BLUE);

  for (uint8_t y = 0; y < GLOBE_MASK_H; y++) {
    for (uint8_t byteX = 0; byteX < GLOBE_MASK_W / 8; byteX++) {
      uint16_t offset = y * (GLOBE_MASK_W / 8) + byteX;
      uint8_t mask = pgm_read_byte(&GLOBE_FRAME_DATA[frame][offset]);
      if (!mask) continue;
      for (uint8_t bit = 0; bit < 8; bit++) {
        if (mask & (0x80 >> bit)) {
          int16_t px = x0 + (byteX * 8 + bit) * GLOBE_CELL_PX;
          int16_t py = y0 + y * GLOBE_CELL_PX;
          g_canvas->fillRect(px, py, GLOBE_CELL_PX, GLOBE_CELL_PX, CLR_ROUND_UPLOAD);
        }
      }
    }
  }
}

static void drawPingBlock(int16_t centerX,
                          int16_t valueY,
                          bool valid,
                          bool loss,
                          uint32_t ms,
                          uint16_t color) {
  if (loss || !valid) {
    drawTextCentered(loss ? "loss" : "--", centerX,
                     valueY + RoundLayout::PING_LOSS_TEXT_Y_OFFSET, 4,
                     loss ? CLR_ROUND_ALARM : CLR_DIM);
    drawTextCentered("ms", centerX,
                     valueY + RoundLayout::PING_LOSS_UNIT_Y_OFFSET, 3,
                     loss ? CLR_ROUND_ALARM : CLR_DIM);
    return;
  }

  char buf[12];
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ms));
  drawTextCenteredGlow(buf, centerX,
                       valueY + RoundLayout::PING_VALUE_Y_OFFSET, 5,
                       color, CLR_ROUND_DIM_BLUE);
  drawTextCentered("ms", centerX, valueY + RoundLayout::PING_UNIT_Y_OFFSET,
                   3, color);
}

static void drawSpeedBlock(int16_t arrowX,
                           int16_t valueX,
                           bool upload,
                           double bps) {
  uint16_t color = upload ? CLR_ROUND_UPLOAD : CLR_ROUND_DOWNLOAD;
  char value[16];
  const char *unit = "Mbps";
  formatSpeedText(bps, SpeedFormatStyle::RoundCompact, value, sizeof(value),
                  &unit);

  int16_t valueY = centerYFromPdf(RoundLayout::SPEED_VALUE_PDF_Y);
  int16_t unitY  = centerYFromPdf(RoundLayout::SPEED_UNIT_PDF_Y);
  if (upload) {
    drawUpArrow(arrowX, centerYFromPdf(RoundLayout::SPEED_ARROW_PDF_Y), color);
  } else {
    drawDownArrow(arrowX, centerYFromPdf(RoundLayout::SPEED_ARROW_PDF_Y),
                  color);
  }
  drawTextCenteredGlow(value, valueX, valueY, 5, CLR_TEXT,
                       upload ? CLR_ROUND_DIM_BLUE : CLR_ROUND_DIM_GN);
  drawTextCentered(unit, valueX, unitY, 3, CLR_TEXT);
}
#endif  // defined(HW_AMOLED_143)

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
  std::strncpy(g_connectSsid, ssid ? ssid : "", sizeof(g_connectSsid) - 1);
  g_connectSsid[sizeof(g_connectSsid) - 1] = '\0';
  uiUpdateConnecting();
}

void uiSetRouterIp(const char *ip) {
  std::strncpy(g_routerIp, ip ? ip : "", sizeof(g_routerIp) - 1);
  g_routerIp[sizeof(g_routerIp) - 1] = '\0';
}

void uiUpdateConnecting() {
  uint8_t frame = (millis() / 400) % 5;

  g_canvas->fillScreen(CLR_BG);

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(20, 60);
  g_canvas->print("Connecting ");

  for (int i = 0; i < 4; i++) {
    g_canvas->setTextColor(
      (frame < 4 && i == frame) ? CLR_PING : CLR_DIM);
    g_canvas->print(".");
  }

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(20, 110);
  g_canvas->print("SSID: ");
  g_canvas->print(g_connectSsid);
  flush();
}

void uiShowReconnectWait(const char *ssid, uint32_t remainSec) {
  g_canvas->fillScreen(CLR_BG);

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_STATUS_DN);
  g_canvas->setCursor(20, 50);
  g_canvas->print("Wi-Fi lost");

  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(20, 95);
  g_canvas->print("Retry in ");
  g_canvas->print(remainSec);
  g_canvas->print("s");

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(20, 145);
  g_canvas->print("SSID: ");
  g_canvas->print(ssid ? ssid : "");

  g_canvas->setTextSize(2);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(20, 185);
  g_canvas->print("Hold KEY >3s to reset");
  flush();
}

#if defined(HW_AMOLED_143)
static void uiShowMainRound(const Telemetry &t) {
  g_canvas->fillScreen(CLR_BG);

  // Тонкие внутренние кольца помогают круглому экрану выглядеть как макет,
  // но остаются внутри физической active area.
  g_canvas->drawCircle(RoundLayout::CENTER_X, RoundLayout::CENTER_Y,
                       RoundLayout::OUTER_RING_RADIUS,
                       rgb565(0x28, 0x2D, 0x33));
  g_canvas->drawCircle(RoundLayout::CENTER_X, RoundLayout::CENTER_Y,
                       RoundLayout::INNER_RING_RADIUS,
                       rgb565(0x11, 0x16, 0x1C));

  drawHistoryArc(g_histIn, RoundLayout::HISTORY_IN_START_DEG,
                 RoundLayout::HISTORY_IN_END_DEG, CLR_ROUND_DOWNLOAD,
                 CLR_ROUND_DIM_GN);
  drawHistoryArc(g_histOut, RoundLayout::HISTORY_OUT_START_DEG,
                 RoundLayout::HISTORY_OUT_END_DEG, CLR_ROUND_UPLOAD,
                 CLR_ROUND_DIM_BLUE);

  drawLineGraph(g_histIn, RoundLayout::LINE_GRAPH_X,
                centerYFromPdf(RoundLayout::LINE_GRAPH_IN_PDF_Y) -
                    RoundLayout::LINE_GRAPH_IN_H,
                RoundLayout::LINE_GRAPH_W, RoundLayout::LINE_GRAPH_IN_H,
                CLR_ROUND_DOWNLOAD, CLR_ROUND_DIM_GN);
  drawLineGraph(g_histOut, RoundLayout::LINE_GRAPH_X,
                centerYFromPdf(RoundLayout::LINE_GRAPH_OUT_PDF_Y) -
                    RoundLayout::LINE_GRAPH_OUT_H,
                RoundLayout::LINE_GRAPH_W, RoundLayout::LINE_GRAPH_OUT_H,
                CLR_ROUND_UPLOAD, CLR_ROUND_DIM_BLUE);

  drawGlobeFrame(RoundLayout::CENTER_X,
                 centerYFromPdf(RoundLayout::GLOBE_PDF_Y));

  drawWifiIcon(RoundLayout::LEFT_METRIC_X,
               centerYFromPdf(RoundLayout::PING_ICON_PDF_Y),
               CLR_ROUND_DOWNLOAD);
  drawGlobeIcon(RoundLayout::RIGHT_METRIC_X,
                centerYFromPdf(RoundLayout::PING_ICON_PDF_Y),
                CLR_ROUND_UPLOAD);

  {
    char title[48];
    char value[32];
    formatRouterInfo(t, title, sizeof(title), value, sizeof(value));
    drawTextCentered(title, RoundLayout::CENTER_X,
                     RoundLayout::ROUTER_TITLE_Y, 2, CLR_DIM);
    drawTextCentered(value, RoundLayout::CENTER_X,
                     centerYFromPdf(RoundLayout::ROUTER_VALUE_PDF_Y),
                     4, CLR_TEXT);
  }
  drawTextCenteredGlow(statusText(t), RoundLayout::CENTER_X,
                       centerYFromPdf(RoundLayout::STATUS_PDF_Y),
                       9, statusColor(t), statusColor(t));

  bool routerValid = t.dataValid && t.routerPingValid;
  drawPingBlock(RoundLayout::LEFT_METRIC_X,
                centerYFromPdf(RoundLayout::PING_VALUE_PDF_Y),
                routerValid, t.dataValid && !t.routerPingValid,
                t.routerPingMs, CLR_ROUND_DOWNLOAD);
  drawPingBlock(RoundLayout::RIGHT_METRIC_X,
                centerYFromPdf(RoundLayout::PING_VALUE_PDF_Y),
                t.dataValid && t.pingValid, t.dataValid && t.pingLoss,
                t.pingMs, CLR_ROUND_UPLOAD);

  g_canvas->drawLine(RoundLayout::CENTER_X,
                     centerYFromPdf(RoundLayout::CENTER_DIVIDER_TOP_PDF_Y),
                     RoundLayout::CENTER_X,
                     centerYFromPdf(RoundLayout::CENTER_DIVIDER_BOTTOM_PDF_Y),
                     rgb565(0x80, 0x80, 0x80));
  drawSpeedBlock(RoundLayout::LEFT_METRIC_X, RoundLayout::SPEED_IN_VALUE_X,
                 false, t.inBps);
  drawSpeedBlock(RoundLayout::SPEED_OUT_ARROW_X,
                 RoundLayout::SPEED_OUT_VALUE_X, true, t.outBps);

  String ip = WiFi.localIP().toString();
  drawTextCentered("DEVICE IP", RoundLayout::CENTER_X,
                   RoundLayout::DEVICE_TITLE_Y, 2, CLR_TEXT);
  drawTextCentered(ip.c_str(), RoundLayout::CENTER_X,
                   RoundLayout::DEVICE_IP_Y, 3, CLR_TEXT);

  flush();
}
#endif  // defined(HW_AMOLED_143)

static void drawRectGlobeIcon(int16_t cx, int16_t cy, uint16_t color) {
  g_canvas->drawCircle(cx, cy, 8, color);
  g_canvas->drawLine(cx - 7, cy, cx + 7, cy, color);
  g_canvas->drawLine(cx, cy - 8, cx, cy + 8, color);
  g_canvas->drawLine(cx - 4, cy - 6, cx - 2, cy + 6, color);
  g_canvas->drawLine(cx + 4, cy - 6, cx + 2, cy + 6, color);
}

static void drawRectRouterIcon(int16_t cx, int16_t cy, uint16_t color) {
  g_canvas->drawRect(cx - 8, cy - 4, 16, 9, color);
  g_canvas->fillCircle(cx - 4, cy + 1, 1, color);
  g_canvas->fillCircle(cx, cy + 1, 1, color);
  g_canvas->drawLine(cx - 5, cy - 4, cx - 8, cy - 9, color);
  g_canvas->drawLine(cx + 5, cy - 4, cx + 8, cy - 9, color);
}

static void drawRectPingValue(const char *value,
                              int16_t y,
                              uint16_t color,
                              bool externalPing) {
  constexpr uint8_t TEXT_SIZE = 3;
  constexpr int16_t ICON_W = 18;
  constexpr int16_t ICON_RIGHT_PAD = 8;
  constexpr int16_t ICON_GAP = 8;

  int16_t iconRight = SCREEN_W - ICON_RIGHT_PAD;
  int16_t iconCenterX = iconRight - ICON_W / 2;
  int16_t valueRight = iconRight - ICON_W - ICON_GAP;
  int16_t valueX =
      valueRight - static_cast<int16_t>(std::strlen(value) * 6U * TEXT_SIZE);

  g_canvas->setTextSize(TEXT_SIZE);
  g_canvas->setTextColor(color);
  g_canvas->setCursor(valueX, y);
  g_canvas->print(value);

  if (externalPing) {
    drawRectGlobeIcon(iconCenterX, y + 12, color);
  } else {
    drawRectRouterIcon(iconCenterX, y + 12, color);
  }
}

static void uiShowMainRect(const Telemetry &t) {
  g_canvas->fillScreen(CLR_BG);

  // --- Зона A: шапка (70px) ---
  // Левая колонка: ROUTER + IP
  // Центр: статус UP/DOWN
  // Правая колонка: пинг до хоста + пинг до роутера

  g_canvas->fillRect(0, ZONE_A_Y, SCREEN_W, ZONE_A_H, CLR_BG);

  char routerTitle[48];
  char routerInfo[32];
  formatRouterInfo(t, routerTitle, sizeof(routerTitle),
                   routerInfo, sizeof(routerInfo));

  // ROUTER / UPTIME (textSize 3 = 18x24)
  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_DIM);
  g_canvas->setCursor(8, ZONE_A_Y + 4);
  g_canvas->print(routerTitle);

  // IP роутера или uptime интерфейса внизу зоны.
  g_canvas->setTextSize(3);
  g_canvas->setTextColor(CLR_TEXT);
  g_canvas->setCursor(8, ZONE_A_Y + 44);
  g_canvas->print(routerInfo);

  constexpr uint8_t RECT_STATUS_TEXT_SIZE = 3;
  constexpr int16_t RECT_STATUS_Y = ZONE_A_Y + 44;
  const char *rectStatusText = "----";
  uint16_t rectStatusColor = CLR_DIM;
  int16_t rectStatusX = 255;

  if (t.dataValid) {
    rectStatusColor = statusColorRect(t);
    rectStatusText = statusText(t);
    if (std::strcmp(rectStatusText, "UP") == 0) {
      rectStatusText = " UP ";
      rectStatusX = 275;
    }
  }

  int16_t statusCenterX =
      rectStatusX +
      static_cast<int16_t>(std::strlen(rectStatusText) * 6U * RECT_STATUS_TEXT_SIZE) / 2;

  if (t.interfaceAliasValid && t.interfaceAlias[0] != '\0') {
    g_canvas->setTextSize(3);
    g_canvas->setTextColor(CLR_TEXT);
    int16_t aliasX =
        statusCenterX -
        static_cast<int16_t>(std::strlen(t.interfaceAlias) * 6U * 3U) / 2;
    g_canvas->setCursor(aliasX, ZONE_A_Y + 4);
    g_canvas->print(t.interfaceAlias);
  }

  g_canvas->setTextSize(RECT_STATUS_TEXT_SIZE);
  g_canvas->setTextColor(rectStatusColor);
  g_canvas->setCursor(rectStatusX, RECT_STATUS_Y);
  g_canvas->print(rectStatusText);

  // Правая колонка: пинги (textSize 3 = 18x24)
  // Верхний: пинг до внешнего хоста
  {
    const char *pingStr = nullptr;
    uint16_t pingColor  = CLR_PING;

    if (!t.dataValid) {
      static const char *frames[] = {"_-", "-_", "--"};
      pingStr  = frames[(millis() / 500) % 3];
      pingColor = CLR_DIM;
    } else if (t.pingLoss) {
      pingStr  = "loss";
      pingColor = CLR_STATUS_DN;
    } else if (t.pingMs == 0) {
      pingStr  = "--";
    } else {
      snprintf(g_fmtBuf, sizeof(g_fmtBuf), "%u", t.pingMs);
      pingStr = g_fmtBuf;
    }

    drawRectPingValue(pingStr, ZONE_A_Y + 4, pingColor, true);
  }

  // Нижний: пинг до роутера
  {
    const char *rStr;
    char rBuf[16];
    uint16_t routerPingColor = CLR_PING;
    if (!t.dataValid) {
      rStr = "--";
      routerPingColor = CLR_DIM;
    } else if (t.routerPingValid) {
      snprintf(rBuf, sizeof(rBuf), "%u", t.routerPingMs);
      rStr = rBuf;
    } else {
      rStr = "--";
      routerPingColor = CLR_DIM;
    }
    drawRectPingValue(rStr, ZONE_A_Y + 44, routerPingColor, false);
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
        formatSpeedText(t.inBps, SpeedFormatStyle::RectValue, valBuf,
                        sizeof(valBuf), &unit);
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
        formatSpeedText(t.outBps, SpeedFormatStyle::RectValue, valBuf,
                        sizeof(valBuf), &unit);
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

  // --- Зона C: графики трафика ---
  {
    constexpr int16_t OTA_H  = 24;
    constexpr int16_t GX     = 0;
    constexpr int16_t GY     = ZONE_C_Y;
    constexpr int16_t GW     = SCREEN_W;
    constexpr int16_t GH     = ZONE_C_H - OTA_H;
    constexpr int16_t HALF_W = GW / 2;

    g_canvas->fillRect(0, ZONE_C_Y, SCREEN_W, ZONE_C_H, CLR_BG);

    if (g_histCount >= 2) {
      float maxIn = 1.0f, maxOut = 1.0f;
      uint16_t start = (g_histHead + GRAPH_POINTS - g_histCount) % GRAPH_POINTS;
      for (uint16_t i = 0; i < g_histCount; i++) {
        uint16_t idx = (start + i) % GRAPH_POINTS;
        if (g_histIn[idx]  > maxIn)  maxIn  = g_histIn[idx];
        if (g_histOut[idx] > maxOut) maxOut = g_histOut[idx];
      }

      float xStep = static_cast<float>(HALF_W) / static_cast<float>(GRAPH_POINTS - 1);
      int16_t bottom = GY + GH - 1;

      auto drawHalf = [&](const float *data, float maxVal, int16_t offsetX, uint16_t lineColor) {
        int16_t prevPx = -1, prevPy = -1;
        for (uint16_t i = 0; i < g_histCount; i++) {
          uint16_t idx = (start + i) % GRAPH_POINTS;
          int16_t px = offsetX + static_cast<int16_t>(i * xStep);
          int16_t py = bottom - static_cast<int16_t>((data[idx] / maxVal) * (GH - 1));
          if (prevPx >= 0) {
            g_canvas->drawLine(prevPx, prevPy, px, py, lineColor);
            g_canvas->drawLine(prevPx, prevPy + 1, px, py + 1, lineColor);
          }
          prevPx = px;
          prevPy = py;
        }
      };

      drawHalf(g_histIn,  maxIn,  GX,         CLR_TRAFF_IN);
      drawHalf(g_histOut, maxOut, GX + HALF_W, CLR_TRAFF_OUT);

      constexpr uint16_t CLR_LABEL_BG = 0x39E7;
      constexpr int16_t PAD   = 4;
      constexpr int16_t RAD   = 3;
      constexpr int16_t CH_W  = 12;
      constexpr int16_t CH_H  = 16;

      auto drawPill = [&](float bps, int16_t px, int16_t py) {
        char v[16];
        const char *u = "bt";
        formatSpeedText(bps, SpeedFormatStyle::GraphLabel, v, sizeof(v), &u);
        int textLen = std::strlen(v) + 1 + std::strlen(u);
        int16_t w = textLen * CH_W + PAD * 2;
        int16_t h = CH_H + PAD * 2;
        g_canvas->fillRoundRect(px, py, w, h, RAD, CLR_LABEL_BG);
        g_canvas->setTextSize(2);
        g_canvas->setTextColor(CLR_TEXT, CLR_LABEL_BG);
        g_canvas->setCursor(px + PAD, py + PAD);
        g_canvas->print(v);
        g_canvas->print(" ");
        g_canvas->print(u);
      };

      constexpr int16_t PILL_Y = GY + 2;
      drawPill(maxIn,  GX + 4,          PILL_Y);
      drawPill(maxOut, GX + HALF_W + 4, PILL_Y);
    }

    g_canvas->setTextSize(2);
    g_canvas->setTextColor(CLR_DIM);
    g_canvas->setCursor(8, ZONE_C_Y + ZONE_C_H - 18);
    g_canvas->print("OTA: ");
    g_canvas->print(WiFi.localIP().toString());
  }

  flush();
}

void uiShowMain(const Telemetry &t) {
  if (!uiDisplayEnabled()) return;
#if defined(HW_AMOLED_143)
  uiShowMainRound(t);
#else
  uiShowMainRect(t);
#endif
}

bool uiDisplayEnabled() {
  return BRIGHTNESS_LEVELS[g_brightnessIdx] > 0;
}

bool uiConsumeRedrawRequest() {
  bool requested = g_redrawRequested;
  g_redrawRequested = false;
  return requested;
}

void uiCycleBrightness() {
  bool wasEnabled = uiDisplayEnabled();
  g_brightnessIdx = (g_brightnessIdx + 1) % BRIGHTNESS_COUNT;
  uint8_t val = BRIGHTNESS_LEVELS[g_brightnessIdx];
  g_bus->beginWrite();
  g_bus->writeC8D8(DISP_CMD_BRIGHTNESS, val);
  if (val > 0) {
    g_bus->writeCommand(DISP_CMD_DISPON);
  } else {
    g_bus->writeCommand(DISP_CMD_DISPOFF);
  }
  g_bus->endWrite();
  if (!wasEnabled && val > 0) {
    g_redrawRequested = true;
  }
  LOG_I("[UI] brightness -> %d (0x%02X)\n", g_brightnessIdx, val);
}

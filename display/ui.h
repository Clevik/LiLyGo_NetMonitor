#pragma once

#include <Arduino.h>
#include "settings.h"

constexpr uint32_t UI_ROUND_FRAME_MS = 112;

enum class WanConnectionState : uint8_t {
  Unknown,
  Disconnected,
  Initializing,
  WaitingForNetwork,
  Requesting,
  Connected,
  Other,
};

struct Telemetry {
  bool     linkUp         = false;
  bool     linkUncertain  = false;
  uint32_t pingMs         = 0;
  bool     pingValid      = false;
  bool     pingLoss       = false;
  uint32_t routerPingMs   = 0;
  bool     routerPingValid = false;
  double   inBps          = 0.0;
  double   outBps         = 0.0;
  uint32_t systemUptimeSec = 0;
  bool     systemUptimeValid = false;
  uint32_t wanUptimeSec = 0;
  bool     wanUptimeValid = false;
  WanConnectionState wanConnectionState = WanConnectionState::Unknown;
  bool     wanConnectionStateValid = false;
  char     interfaceAlias[32] = {};
  bool     interfaceAliasValid = false;
  bool     dataValid      = false;
};

bool uiInit(uint16_t displayRotation,
            ColorScheme colorScheme,
            BrightnessLevel startupBrightness);
bool uiApplyDisplaySettings(uint16_t displayRotation,
                            ColorScheme colorScheme);
void uiShowSplash();
void uiShowApConfig(const char *apName, const char *apIp);
void uiShowConnecting(const char *ssid);
void uiUpdateConnecting();
void uiShowReconnectWait(const char *ssid, uint32_t remainSec);
void uiSetRouterIp(const char *ip);
void uiObserveTelemetry(const Telemetry &t);
void uiShowMain(const Telemetry &t);
#if defined(HW_AMOLED_143)
bool uiHandleTap(int16_t x, int16_t y);
#endif
void uiSetBrightness(BrightnessLevel level);
BrightnessLevel uiBrightness();
bool uiDisplayEnabled();
bool uiConsumeRedrawRequest();

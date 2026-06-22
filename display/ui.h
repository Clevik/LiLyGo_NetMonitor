#pragma once

#include <Arduino.h>

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
  uint32_t interfaceUptimeSec = 0;
  bool     interfaceUptimeValid = false;
  uint32_t lastUpdateMs   = 0;
  bool     dataValid      = false;
};

bool uiInit();
void uiShowSplash();
void uiShowApConfig(const char *apName, const char *apIp);
void uiShowConnecting(const char *ssid);
void uiUpdateConnecting();
void uiShowReconnectWait(const char *ssid, uint32_t remainSec);
void uiSetRouterIp(const char *ip);
void uiObserveTelemetry(const Telemetry &t);
void uiShowMain(const Telemetry &t);
void uiUpdateMain(const Telemetry &t);
void uiCycleBrightness();
bool uiDisplayEnabled();
bool uiConsumeRedrawRequest();

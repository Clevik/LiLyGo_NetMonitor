#pragma once

#include <Arduino.h>

struct Telemetry {
  bool     linkUp        = false;
  uint32_t pingMs        = 0;
  bool     pingValid     = false;
  bool     pingLoss      = false;
  double   inMbps        = 0.0;
  double   outMbps       = 0.0;
  uint32_t lastUpdateMs  = 0;
  bool     dataValid     = false;
};

bool uiInit();
void uiShowSplash();
void uiShowApConfig(const char *apName, const char *apIp);
void uiShowConnecting(const char *ssid);
void uiShowMain(const Telemetry &t);
void uiUpdateMain(const Telemetry &t);

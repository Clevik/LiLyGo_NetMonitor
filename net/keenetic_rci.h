#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct KeeneticRciData {
  uint32_t wanUptimeSec = 0;
  bool     wanUptimeValid = false;
  char     wanConnectionState[32] = {};
  bool     wanConnectionStateValid = false;
};

bool keeneticRciFetchWanData(IPAddress routerIp,
                             const char *login,
                             const char *password,
                             KeeneticRciData &out);

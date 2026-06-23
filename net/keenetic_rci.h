#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct KeeneticRciData {
  uint32_t wanUptimeSec = 0;
  bool     wanUptimeValid = false;
};

bool keeneticRciFetchWanUptime(IPAddress routerIp,
                               const char *login,
                               const char *password,
                               KeeneticRciData &out);

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct SnmpData {
  bool     linkUp     = false;
  uint32_t inOctets   = 0;
  uint32_t outOctets  = 0;
  bool     valid      = false;
};

void snmpInit(IPAddress ip, uint16_t port, const char *community,
              int version, uint32_t ifIndex);
void snmpCleanup();
bool snmpPoll(SnmpData &out);

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct SnmpData {
  bool     linkUp     = false;
  uint64_t inOctets   = 0;
  uint64_t outOctets  = 0;
  bool     valid      = false;
  bool     isHC       = false;
};

void snmpInit(IPAddress ip, uint16_t port, const char *community,
              int version, uint32_t ifIndex);
void snmpCleanup();
bool snmpPoll(SnmpData &out);

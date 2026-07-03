#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct SnmpData {
  bool     linkUp                    = false;
  uint64_t inOctets                  = 0;
  uint64_t outOctets                 = 0;
  uint32_t systemUptimeSec           = 0;
  char     interfaceAlias[32]        = {};
  bool     valid                     = false;
  bool     isHC                      = false;
  bool     countersValid             = false;
  bool     systemUptimeValid         = false;
  bool     interfaceAliasValid       = false;
};

void snmpInit(IPAddress ip, uint16_t port, const char *community,
              int version, uint32_t ifIndex);
void snmpCleanup();
bool snmpPoll(SnmpData &out);

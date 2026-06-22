#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define SNMP_PACKET_LENGTH 512
#include <Arduino_SNMP_Manager.h>

#include "snmp_client.h"

static WiFiUDP       g_udp;
static SNMPManager  *g_mgr  = nullptr;
static SNMPGet      *g_req  = nullptr;

static IPAddress     g_ip;
static uint16_t      g_port = 161;

static int           g_operStatus;
static uint32_t      g_inOctets;
static uint32_t      g_outOctets;
static uint64_t      g_hcInOctets;
static uint64_t      g_hcOutOctets;
static uint32_t      g_sysUpTimeTicks;
static uint32_t      g_ifLastChangeTicks;
static char          g_ifAliasRaw[SNMP_OCTETSTRING_MAX_LENGTH];
static char         *g_ifAliasValue = g_ifAliasRaw;

static ValueCallback *g_cbOper         = nullptr;
static ValueCallback *g_cbIn           = nullptr;
static ValueCallback *g_cbOut          = nullptr;
static ValueCallback *g_cbHcIn         = nullptr;
static ValueCallback *g_cbHcOut        = nullptr;
static ValueCallback *g_cbSysUpTime    = nullptr;
static ValueCallback *g_cbIfLastChange = nullptr;
static ValueCallback *g_cbIfAlias      = nullptr;

static char g_oidOper[64];
static char g_oidIn[64];
static char g_oidOut[64];
static char g_oidHcIn[64];
static char g_oidHcOut[64];
static char g_oidIfLastChange[64];
static char g_oidIfAlias[64];

static int  g_counterMode = -1;
static bool g_parseOk     = false;

// Маркер «значение не получено». В эти переменные счётчики сбрасываются
// перед опросом; если после ответа маркер остался — счётчик не пришёл.
static constexpr uint64_t COUNTER64_SENTINEL = UINT64_MAX;
static constexpr uint32_t COUNTER32_SENTINEL = UINT32_MAX;
static constexpr uint32_t TIMETICKS_SENTINEL = UINT32_MAX;

void snmpInit(IPAddress ip, uint16_t port, const char *community,
              int version, uint32_t ifIndex) {
  if (g_mgr || g_req) {
    Serial.println("[SNMP] init skipped: already initialized");
    return;
  }

  g_ip   = ip;
  g_port = port;

  snprintf(g_oidOper, sizeof(g_oidOper),
           ".1.3.6.1.2.1.2.2.1.8.%u", ifIndex);
  snprintf(g_oidIn, sizeof(g_oidIn),
           ".1.3.6.1.2.1.2.2.1.10.%u", ifIndex);
  snprintf(g_oidOut, sizeof(g_oidOut),
           ".1.3.6.1.2.1.2.2.1.16.%u", ifIndex);
  snprintf(g_oidHcIn, sizeof(g_oidHcIn),
           ".1.3.6.1.2.1.31.1.1.1.6.%u", ifIndex);
  snprintf(g_oidHcOut, sizeof(g_oidHcOut),
           ".1.3.6.1.2.1.31.1.1.1.10.%u", ifIndex);
  snprintf(g_oidIfLastChange, sizeof(g_oidIfLastChange),
           ".1.3.6.1.2.1.2.2.1.9.%u", ifIndex);
  snprintf(g_oidIfAlias, sizeof(g_oidIfAlias),
           ".1.3.6.1.2.1.31.1.1.1.18.%u", ifIndex);

  g_mgr = new SNMPManager(community);
  g_mgr->callbacks->value = nullptr;
  g_mgr->_udp = nullptr;
  g_mgr->setUDP(&g_udp);

  g_cbOper         = g_mgr->addIntegerHandler(ip, g_oidOper, &g_operStatus);
  g_cbIn           = g_mgr->addCounter32Handler(ip, g_oidIn, &g_inOctets);
  g_cbOut          = g_mgr->addCounter32Handler(ip, g_oidOut, &g_outOctets);
  g_cbHcIn         = g_mgr->addCounter64Handler(ip, g_oidHcIn, &g_hcInOctets);
  g_cbHcOut        = g_mgr->addCounter64Handler(ip, g_oidHcOut, &g_hcOutOctets);
  g_cbSysUpTime    = g_mgr->addTimestampHandler(ip, ".1.3.6.1.2.1.1.3.0",
                                                &g_sysUpTimeTicks);
  g_cbIfLastChange = g_mgr->addTimestampHandler(ip, g_oidIfLastChange,
                                                &g_ifLastChangeTicks);
  g_cbIfAlias      = g_mgr->addStringHandler(ip, g_oidIfAlias,
                                             &g_ifAliasValue);

  g_req = new SNMPGet(community, version);
  g_req->setPort(port);

  g_operStatus  = 0;
  g_inOctets    = 0;
  g_outOctets   = 0;
  g_hcInOctets  = 0;
  g_hcOutOctets = 0;
  g_sysUpTimeTicks = 0;
  g_ifLastChangeTicks = 0;
  g_ifAliasRaw[0] = '\0';
  g_ifAliasValue = g_ifAliasRaw;
  g_counterMode = -1;

  Serial.printf("[SNMP] init %s v%d ifIndex=%u port=%u\n",
                ip.toString().c_str(), version, ifIndex, port);
}

void snmpCleanup() {
  if (g_req) { delete g_req; g_req = nullptr; }
  if (g_mgr) { delete g_mgr; g_mgr = nullptr; }
  g_cbOper         = nullptr;
  g_cbIn           = nullptr;
  g_cbOut          = nullptr;
  g_cbHcIn         = nullptr;
  g_cbHcOut        = nullptr;
  g_cbSysUpTime    = nullptr;
  g_cbIfLastChange = nullptr;
  g_cbIfAlias      = nullptr;
}

static bool pollOnce(ValueCallback *cbIn, ValueCallback *cbOut,
                     uint32_t timeoutMs) {
  g_operStatus = 0;
  g_sysUpTimeTicks = TIMETICKS_SENTINEL;
  g_ifLastChangeTicks = TIMETICKS_SENTINEL;
  memset(g_ifAliasRaw, 0, sizeof(g_ifAliasRaw));
  g_ifAliasValue = g_ifAliasRaw;
  g_parseOk    = false;

  g_req->addOIDPointer(g_cbOper);
  g_req->addOIDPointer(cbIn);
  g_req->addOIDPointer(cbOut);
  g_req->addOIDPointer(g_cbSysUpTime);
  g_req->addOIDPointer(g_cbIfLastChange);
  g_req->addOIDPointer(g_cbIfAlias);
  g_req->setIP(WiFi.localIP());
  g_req->setUDP(&g_udp);
  g_req->setRequestID(rand() % 5555);

  bool sent = g_req->sendTo(g_ip);
  g_req->clearOIDList();

  if (!sent) {
    Serial.println("[SNMP] send failed");
    return false;
  }

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (g_mgr->loop()) {
      g_parseOk = true;
    }
    if (g_operStatus != 0) {
      return true;
    }
    delay(5);
  }

  Serial.println("[SNMP] timeout or no data");
  return false;
}

static void fillOutput(SnmpData &out, bool hc) {
  out.valid  = true;
  out.linkUp = (g_operStatus == 1);
  out.isHC   = hc;
  out.interfaceStateUptimeValid = false;
  out.interfaceStateUptimeSec = 0;
  out.interfaceAliasValid = false;
  out.interfaceAlias[0] = '\0';
  if (hc) {
    out.countersValid = (g_hcInOctets  != COUNTER64_SENTINEL &&
                         g_hcOutOctets != COUNTER64_SENTINEL);
    out.inOctets  = g_hcInOctets;
    out.outOctets = g_hcOutOctets;
  } else {
    out.countersValid = (g_inOctets  != COUNTER32_SENTINEL &&
                         g_outOctets != COUNTER32_SENTINEL);
    out.inOctets  = (uint64_t)g_inOctets;
    out.outOctets = (uint64_t)g_outOctets;
  }

  bool ticksValid = (g_sysUpTimeTicks != TIMETICKS_SENTINEL &&
                     g_ifLastChangeTicks != TIMETICKS_SENTINEL);
  if (out.linkUp && ticksValid && g_ifLastChangeTicks > 0) {
    uint32_t stateTicks = g_sysUpTimeTicks - g_ifLastChangeTicks;
    out.interfaceStateUptimeSec = stateTicks / 100U;
    out.interfaceStateUptimeValid = true;
  }

  g_ifAliasRaw[sizeof(g_ifAliasRaw) - 1] = '\0';
  if (g_ifAliasRaw[0] != '\0') {
    strncpy(out.interfaceAlias, g_ifAliasRaw, sizeof(out.interfaceAlias) - 1);
    out.interfaceAlias[sizeof(out.interfaceAlias) - 1] = '\0';
    out.interfaceAliasValid = true;
  }
}

bool snmpPoll(SnmpData &out) {
  if (!g_mgr || !g_req) return false;

  if (g_counterMode == -1) {
    Serial.println("[SNMP] detecting counter mode...");

    g_hcInOctets  = COUNTER64_SENTINEL;
    g_hcOutOctets = COUNTER64_SENTINEL;
    if (pollOnce(g_cbHcIn, g_cbHcOut, 2000) && g_parseOk) {
      g_counterMode = 1;
      Serial.println("[SNMP] using HC (64-bit) counters");
      fillOutput(out, true);
      Serial.printf("[SNMP] HC status=%d in=%llu out=%llu (linkUp=%d)\n",
                    g_operStatus, out.inOctets, out.outOctets, out.linkUp);
      return true;
    }

    Serial.println("[SNMP] HC not available, trying 32-bit counters");
    g_inOctets  = COUNTER32_SENTINEL;
    g_outOctets = COUNTER32_SENTINEL;
    if (pollOnce(g_cbIn, g_cbOut, 2000)) {
      g_counterMode = 0;
      Serial.println("[SNMP] using 32-bit counters");
      fillOutput(out, false);
      Serial.printf("[SNMP] 32 status=%d in=%llu out=%llu (linkUp=%d)\n",
                    g_operStatus, out.inOctets, out.outOctets, out.linkUp);
      return true;
    }

    g_counterMode = -1;
    return false;
  }

  if (g_counterMode == 1) {
    g_hcInOctets  = COUNTER64_SENTINEL;
    g_hcOutOctets = COUNTER64_SENTINEL;
    if (pollOnce(g_cbHcIn, g_cbHcOut, 2000)) {
      fillOutput(out, true);
      Serial.printf("[SNMP] HC status=%d in=%llu out=%llu (linkUp=%d)\n",
                    g_operStatus, out.inOctets, out.outOctets, out.linkUp);
      return true;
    }
    g_counterMode = -1;
    return false;
  }

  g_inOctets  = COUNTER32_SENTINEL;
  g_outOctets = COUNTER32_SENTINEL;
  if (pollOnce(g_cbIn, g_cbOut, 2000)) {
    fillOutput(out, false);
    Serial.printf("[SNMP] 32 status=%d in=%llu out=%llu (linkUp=%d)\n",
                  g_operStatus, out.inOctets, out.outOctets, out.linkUp);
    return true;
  }
  g_counterMode = -1;
  return false;
}

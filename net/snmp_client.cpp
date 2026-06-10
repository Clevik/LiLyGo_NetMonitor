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

static ValueCallback *g_cbOper = nullptr;
static ValueCallback *g_cbIn   = nullptr;
static ValueCallback *g_cbOut  = nullptr;

static char g_oidOper[64];
static char g_oidIn[64];
static char g_oidOut[64];

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

  g_mgr = new SNMPManager(community);
  g_mgr->callbacks->value = nullptr;
  g_mgr->_udp = nullptr;
  g_mgr->setUDP(&g_udp);

  g_cbOper = g_mgr->addIntegerHandler(ip, g_oidOper, &g_operStatus);
  g_cbIn   = g_mgr->addCounter32Handler(ip, g_oidIn, &g_inOctets);
  g_cbOut  = g_mgr->addCounter32Handler(ip, g_oidOut, &g_outOctets);

  g_req = new SNMPGet(community, version);
  g_req->setPort(port);

  g_operStatus = 0;
  g_inOctets   = 0;
  g_outOctets  = 0;

  Serial.printf("[SNMP] init %s v%d ifIndex=%u port=%u\n",
                ip.toString().c_str(), version, ifIndex, port);
}

void snmpCleanup() {
  if (g_req) { delete g_req; g_req = nullptr; }
  if (g_mgr) { delete g_mgr; g_mgr = nullptr; }
  g_cbOper = nullptr;
  g_cbIn   = nullptr;
  g_cbOut  = nullptr;
}

bool snmpPoll(SnmpData &out) {
  if (!g_mgr || !g_req) return false;

  g_operStatus = 0;
  g_inOctets   = 0;
  g_outOctets  = 0;

  g_req->addOIDPointer(g_cbOper);
  g_req->addOIDPointer(g_cbIn);
  g_req->addOIDPointer(g_cbOut);
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
  bool ok = false;
  while (millis() - start < 2000) {
    g_mgr->loop();
    if (g_operStatus != 0 || g_inOctets != 0 || g_outOctets != 0) {
      ok = true;
      break;
    }
    delay(5);
  }

  if (!ok) {
    Serial.println("[SNMP] timeout or no data");
    return false;
  }

  out.valid    = true;
  out.linkUp   = (g_operStatus == 1);
  out.inOctets  = g_inOctets;
  out.outOctets = g_outOctets;

  Serial.printf("[SNMP] status=%d in=%u out=%u (linkUp=%d)\n",
                g_operStatus, g_inOctets, g_outOctets, out.linkUp);
  return true;
}

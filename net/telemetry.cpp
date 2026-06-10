#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "telemetry.h"
#include "snmp_client.h"

static SemaphoreHandle_t g_mutex      = nullptr;
static volatile bool     g_running    = false;
static TaskHandle_t      g_task       = nullptr;
static Telemetry         g_tel;

static char     g_pingHost[64];
static uint32_t g_pingIntervalMs = 5000;

static bool     g_snmpReady  = false;

static uint32_t g_s1InOctets  = 0;
static uint32_t g_s1OutOctets = 0;
static uint32_t g_s1Ms        = 0;
static bool     g_haveSample1 = false;

static uint32_t g_s2InOctets  = 0;
static uint32_t g_s2OutOctets = 0;
static uint32_t g_s2Ms        = 0;
static bool     g_haveSample2 = false;

static uint8_t  g_snmpFailCount = 0;

static IPAddress g_routerIP;
static uint16_t  g_snmpPort      = 161;
static char      g_snmpCommunity[32] = "public";
static int       g_snmpVersion   = 1;
static uint32_t  g_ifIndex       = 0;

static void netTask(void *arg) {
  Serial.printf("[NetTask] started on core %d\n", xPortGetCoreID());

  while (g_running) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!g_snmpReady && g_ifIndex > 0) {
      Serial.printf("[SNMP] init after WiFi connect: %s ifIndex=%u\n",
                    g_routerIP.toString().c_str(), g_ifIndex);
      snmpInit(g_routerIP, g_snmpPort, g_snmpCommunity,
               g_snmpVersion, g_ifIndex);
      g_snmpReady = true;
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    Telemetry t = g_tel;
    t.dataValid    = true;
    t.lastUpdateMs = millis();
    t.pingMs       = 0;
    t.pingValid    = false;
    t.pingLoss     = false;
    t.linkUncertain = false;

    if (g_snmpReady) {
      SnmpData snmp;
      if (snmpPoll(snmp)) {
        g_snmpFailCount = 0;
        t.linkUp = snmp.linkUp;

        uint32_t now = millis();

        if (!g_haveSample1) {
          g_s1InOctets  = snmp.inOctets;
          g_s1OutOctets = snmp.outOctets;
          g_s1Ms        = now;
          g_haveSample1 = true;
        } else {
          g_s2InOctets  = snmp.inOctets;
          g_s2OutOctets = snmp.outOctets;
          g_s2Ms        = now;
          g_haveSample2 = true;
        }

        if (g_haveSample1 && g_haveSample2) {
          float dt = (g_s2Ms - g_s1Ms) / 1000.0f;
          if (dt >= 0.5f) {
            uint32_t dIn  = g_s2InOctets  - g_s1InOctets;
            uint32_t dOut = g_s2OutOctets - g_s1OutOctets;
            double bpsIn  = (double)dIn  / dt * 8.0;
            double bpsOut = (double)dOut / dt * 8.0;
            if (bpsIn  < 1000000000.0) t.inBps  = bpsIn;
            if (bpsOut < 1000000000.0) t.outBps = bpsOut;
          }

          g_s1InOctets  = g_s2InOctets;
          g_s1OutOctets = g_s2OutOctets;
          g_s1Ms        = g_s2Ms;
        }
      } else {
        g_snmpFailCount++;
      }
    }

    bool ok = Ping.ping(g_pingHost, 3);
    if (ok) {
      t.pingMs    = (uint32_t)Ping.averageTime();
      t.pingValid = true;
    } else {
      t.pingLoss = true;
    }

    if (g_snmpFailCount == 0) {
      // статус от SNMP — уже записан
    } else if (g_snmpFailCount == 1) {
      t.linkUp = g_tel.linkUp;
    } else {
      t.linkUp = t.pingValid;
      t.linkUncertain = true;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      g_tel = t;
      xSemaphoreGive(g_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(g_pingIntervalMs));
  }

  vTaskDelete(nullptr);
}

void telemetryStart(const Settings &settings) {
  if (g_task) return;

  strncpy(g_pingHost, settings.pingHost.c_str(), sizeof(g_pingHost) - 1);
  g_pingHost[sizeof(g_pingHost) - 1] = '\0';
  g_pingIntervalMs = settings.pingIntervalSec * 1000;

  g_routerIP.fromString(settings.routerHost);
  g_snmpPort = settings.snmpPort;
  strncpy(g_snmpCommunity, settings.snmpCommunity.c_str(), sizeof(g_snmpCommunity) - 1);
  g_snmpCommunity[sizeof(g_snmpCommunity) - 1] = '\0';
  g_snmpVersion = (settings.snmpVersion == SnmpVersion::V1) ? 0 : 1;
  g_ifIndex = settings.ifIndex;

  g_snmpReady      = false;
  g_haveSample1    = false;
  g_haveSample2    = false;
  g_snmpFailCount  = 0;
  g_s1InOctets    = 0;
  g_s1OutOctets   = 0;
  g_s1Ms          = 0;
  g_s2InOctets    = 0;
  g_s2OutOctets   = 0;
  g_s2Ms          = 0;

  if (!g_mutex) {
    g_mutex = xSemaphoreCreateMutex();
  }

  memset((void *)&g_tel, 0, sizeof(g_tel));
  g_running = true;

  xTaskCreatePinnedToCore(
    netTask,
    "netTask",
    16384,
    nullptr,
    1,
    &g_task,
    0
  );
}

void telemetryStop() {
  g_running = false;
  g_task = nullptr;
  snmpCleanup();
  g_snmpReady = false;
}

Telemetry telemetrySnapshot() {
  Telemetry snap;
  if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = g_tel;
    xSemaphoreGive(g_mutex);
  }
  return snap;
}

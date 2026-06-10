#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "telemetry.h"
#include "snmp_client.h"

static SemaphoreHandle_t g_mutex      = nullptr;
static SemaphoreHandle_t g_stopDone   = nullptr;
static volatile bool     g_running    = false;
static TaskHandle_t      g_task       = nullptr;
static portMUX_TYPE      g_taskMux    = portMUX_INITIALIZER_UNLOCKED;
static Telemetry         g_tel;

static char     g_pingHost[64];
static uint32_t g_pingIntervalMs = 5000;
static uint32_t g_snmpIntervalMs = 5000;

static constexpr uint32_t ROUTER_PING_INTERVAL_MS = 5000;
static constexpr uint32_t NET_TASK_TICK_MS = 100;

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

static TaskHandle_t currentTaskHandle() {
  portENTER_CRITICAL(&g_taskMux);
  TaskHandle_t task = g_task;
  portEXIT_CRITICAL(&g_taskMux);
  return task;
}

static void setTaskHandle(TaskHandle_t task) {
  portENTER_CRITICAL(&g_taskMux);
  g_task = task;
  portEXIT_CRITICAL(&g_taskMux);
}

static bool delayOrStop(uint32_t delayMs) {
  if (!g_running) return true;
  return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delayMs)) > 0 || !g_running;
}

static bool intervalDue(uint32_t now, uint32_t lastMs, uint32_t intervalMs) {
  return lastMs == 0 || now - lastMs >= intervalMs;
}

static void signalTaskStopped() {
  snmpCleanup();
  g_snmpReady = false;
  g_running = false;
  setTaskHandle(nullptr);

  if (g_stopDone) {
    xSemaphoreGive(g_stopDone);
  }
}

static void netTask(void *arg) {
  Serial.printf("[NetTask] started on core %d\n", xPortGetCoreID());
  Serial.printf("[NetTask] intervals: snmp=%u ms ping=%u ms router=%u ms\n",
                g_snmpIntervalMs, g_pingIntervalMs, ROUTER_PING_INTERVAL_MS);

  uint32_t lastSnmpMs = 0;
  uint32_t lastRouterPingMs = 0;
  uint32_t lastExternalPingMs = 0;

  while (g_running) {
    if (WiFi.status() != WL_CONNECTED) {
      if (delayOrStop(1000)) break;
      continue;
    }

    if (!g_snmpReady && g_ifIndex > 0) {
      Serial.printf("[SNMP] init after WiFi connect: %s ifIndex=%u\n",
                    g_routerIP.toString().c_str(), g_ifIndex);
      snmpInit(g_routerIP, g_snmpPort, g_snmpCommunity,
               g_snmpVersion, g_ifIndex);
      g_snmpReady = true;
      lastSnmpMs = 0;
      lastRouterPingMs = 0;
      lastExternalPingMs = 0;
    }

    Telemetry t = g_tel;
    bool changed = false;
    uint32_t now = millis();

    if (g_snmpReady && intervalDue(now, lastSnmpMs, g_snmpIntervalMs)) {
      lastSnmpMs = now;
      SnmpData snmp;
      if (snmpPoll(snmp)) {
        g_snmpFailCount = 0;
        t.linkUp = snmp.linkUp;

        uint32_t sampleMs = millis();

        if (!g_haveSample1) {
          g_s1InOctets  = snmp.inOctets;
          g_s1OutOctets = snmp.outOctets;
          g_s1Ms        = sampleMs;
          g_haveSample1 = true;
        } else {
          g_s2InOctets  = snmp.inOctets;
          g_s2OutOctets = snmp.outOctets;
          g_s2Ms        = sampleMs;
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
      changed = true;
    }

    if (!g_running) break;

    now = millis();
    if (intervalDue(now, lastRouterPingMs, ROUTER_PING_INTERVAL_MS)) {
      lastRouterPingMs = now;
      if (Ping.ping(g_routerIP, 1)) {
        t.routerPingMs    = (uint32_t)Ping.averageTime();
        t.routerPingValid = true;
      } else {
        t.routerPingMs    = 0;
        t.routerPingValid = false;
      }
      changed = true;
    }

    if (!g_running) break;

    now = millis();
    if (intervalDue(now, lastExternalPingMs, g_pingIntervalMs)) {
      lastExternalPingMs = now;
      bool ok = Ping.ping(g_pingHost, 3);
      if (ok) {
        t.pingMs    = (uint32_t)Ping.averageTime();
        t.pingValid = true;
        t.pingLoss  = false;
      } else {
        t.pingMs    = 0;
        t.pingValid = false;
        t.pingLoss  = true;
      }
      changed = true;
    }

    if (!changed) {
      if (delayOrStop(NET_TASK_TICK_MS)) break;
      continue;
    }

    t.dataValid    = true;
    t.lastUpdateMs = millis();
    t.linkUncertain = false;

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

    if (delayOrStop(NET_TASK_TICK_MS)) break;
  }

  signalTaskStopped();
  Serial.println("[NetTask] stopped");
  vTaskDelete(nullptr);
}

bool telemetryStart(const Settings &settings) {
  if (currentTaskHandle()) {
    Serial.println("[NetTask] start skipped: previous task is still running");
    return false;
  }

  if (!g_mutex) {
    g_mutex = xSemaphoreCreateMutex();
  }
  if (!g_stopDone) {
    g_stopDone = xSemaphoreCreateBinary();
  }
  if (!g_mutex || !g_stopDone) {
    Serial.println("[NetTask] start failed: cannot create synchronization primitives");
    return false;
  }
  if (settings.ifIndex == 0) {
    Serial.println("[NetTask] start failed: ifIndex must be greater than 0");
    return false;
  }
  while (xSemaphoreTake(g_stopDone, 0) == pdTRUE) {}

  strncpy(g_pingHost, settings.pingHost.c_str(), sizeof(g_pingHost) - 1);
  g_pingHost[sizeof(g_pingHost) - 1] = '\0';
  g_pingIntervalMs = clampSettingsIntervalSec(static_cast<long>(settings.pingIntervalSec)) * 1000UL;
  g_snmpIntervalMs = clampSettingsIntervalSec(static_cast<long>(settings.updateIntervalSec)) * 1000UL;

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

  memset((void *)&g_tel, 0, sizeof(g_tel));
  g_running = true;

  BaseType_t created = xTaskCreatePinnedToCore(
    netTask,
    "netTask",
    16384,
    nullptr,
    1,
    &g_task,
    0
  );

  if (created != pdPASS || !currentTaskHandle()) {
    g_running = false;
    setTaskHandle(nullptr);
    Serial.println("[NetTask] start failed: xTaskCreatePinnedToCore");
    return false;
  }

  return true;
}

bool telemetryStop(uint32_t timeoutMs) {
  TaskHandle_t task = currentTaskHandle();
  if (!task) {
    g_running = false;
    g_snmpReady = false;
    return true;
  }

  if (task == xTaskGetCurrentTaskHandle()) {
    Serial.println("[NetTask] stop refused: cannot join current task");
    return false;
  }

  if (!g_stopDone) {
    g_stopDone = xSemaphoreCreateBinary();
    if (!g_stopDone) {
      Serial.println("[NetTask] stop failed: cannot create completion semaphore");
      return false;
    }
  }

  while (xSemaphoreTake(g_stopDone, 0) == pdTRUE) {}

  g_running = false;
  xTaskNotifyGive(task);

  if (xSemaphoreTake(g_stopDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
    return true;
  }

  Serial.printf("[NetTask] stop timeout after %u ms; old task is still stopping\n",
                timeoutMs);
  return false;
}

Telemetry telemetrySnapshot() {
  Telemetry snap;
  if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = g_tel;
    xSemaphoreGive(g_mutex);
  }
  return snap;
}

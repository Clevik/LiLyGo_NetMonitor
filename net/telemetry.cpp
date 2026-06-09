#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "telemetry.h"

static SemaphoreHandle_t g_mutex      = nullptr;
static volatile bool     g_running    = false;
static TaskHandle_t       g_task      = nullptr;
static Telemetry          g_tel;

static char    g_pingHost[64];
static uint32_t g_intervalMs = 5000;

static void netTask(void *arg) {
  Serial.printf("[NetTask] started on core %d\n", xPortGetCoreID());

  while (g_running) {
    Telemetry t;

    if (WiFi.status() == WL_CONNECTED) {
      bool ok = Ping.ping(g_pingHost, 3);
      if (ok) {
        t.pingMs    = (uint32_t)Ping.averageTime();
        t.pingValid = true;
      }
    }

    t.dataValid    = true;
    t.lastUpdateMs = millis();

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      g_tel = t;
      xSemaphoreGive(g_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(g_intervalMs));
  }

  vTaskDelete(nullptr);
}

void telemetryStart(const char *pingHost, uint32_t intervalSec) {
  if (g_task) return;

  strncpy(g_pingHost, pingHost, sizeof(g_pingHost) - 1);
  g_pingHost[sizeof(g_pingHost) - 1] = '\0';
  g_intervalMs = intervalSec * 1000;

  if (!g_mutex) {
    g_mutex = xSemaphoreCreateMutex();
  }

  memset((void *)&g_tel, 0, sizeof(g_tel));
  g_running = true;

  xTaskCreatePinnedToCore(
    netTask,
    "netTask",
    4096,
    nullptr,
    1,
    &g_task,
    0
  );
}

void telemetryStop() {
  g_running = false;
  g_task = nullptr;
}

Telemetry telemetrySnapshot() {
  Telemetry snap;
  if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = g_tel;
    xSemaphoreGive(g_mutex);
  }
  return snap;
}

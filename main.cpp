#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "settings.h"
#include "display/ui.h"
#include "portal/portal.h"
#include "net/ota.h"
#include "net/telemetry.h"
#include "touch/touch.h"

enum class AppState : uint8_t {
  Boot,
  ApConfig,
  WifiConnect,
  Running,
};

static AppState   g_state = AppState::Boot;
static Settings   g_settings;
static Telemetry  g_telemetry;

static uint32_t   g_stateEnteredMs = 0;
static uint32_t   g_lastUiMs       = 0;
static uint32_t   g_lastTelemetryMs = 0;
static uint32_t   g_keyDownMs      = 0;

#if defined(HW_AMOLED_143)
static constexpr uint32_t UI_RUNNING_FRAME_MS = UI_ROUND_FRAME_MS;
#else
static constexpr uint32_t UI_RUNNING_FRAME_MS = 500;
#endif
static constexpr uint32_t TELEMETRY_SNAPSHOT_MS = 500;

// Ретрай Wi-Fi внутри состояния WifiConnect.
static bool       g_wifiWaiting    = false;  // идёт ли пауза между попытками
static uint32_t   g_nextAttemptMs  = 0;      // момент следующей попытки
static uint32_t   g_lastWaitSec    = 0;      // последний показанный остаток (сек)
static uint32_t   g_lastWaitUiMs   = 0;      // когда последний раз перерисовывали ожидание

static void enterState(AppState next) {
  if (g_state == AppState::ApConfig && next != AppState::ApConfig) {
    portalEnd();
  }
  if (g_state == AppState::Running && next != AppState::Running) {
    otaEnd();
    if (!telemetryStop()) {
      Serial.println("[FSM] telemetry stop did not finish before state change");
    }
  }

  g_state = next;
  g_stateEnteredMs = millis();

  switch (next) {
    case AppState::ApConfig:
      Serial.println("[FSM] -> AP_CONFIG");
      portalBegin(g_settings);
      uiShowApConfig(portalGetApName(), WiFi.softAPIP().toString().c_str());
      break;

    case AppState::WifiConnect:
      Serial.println("[FSM] -> WIFI_CONNECT");
      g_wifiWaiting = false;
      g_nextAttemptMs = 0;
      WiFi.mode(WIFI_STA);
      WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPassword.c_str());
      uiShowConnecting(g_settings.wifiSsid.c_str());
      break;

    case AppState::Running:
      Serial.println("[FSM] -> RUNNING");
      g_lastUiMs = 0;
      g_lastTelemetryMs = 0;
      otaBegin(g_settings);
      uiSetRouterIp(g_settings.routerHost.c_str());
      if (!telemetryStart(g_settings)) {
        Serial.println("[FSM] telemetry start skipped");
      }
      uiShowMain(g_telemetry);
      break;

    case AppState::Boot:
      break;
  }
}

static bool keyLongPressed() {
  bool pressed = (digitalRead(PIN_KEY) == LOW);
  if (pressed) {
    if (g_keyDownMs == 0) g_keyDownMs = millis();
    if (millis() - g_keyDownMs >= KEY_LONG_PRESS_MS) {
      g_keyDownMs = 0;
      return true;
    }
  } else {
    g_keyDownMs = 0;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[NETMONITOR] boot");

  bool haveConfig = SettingsStore::load(g_settings);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  }

  pinMode(PIN_KEY, INPUT_PULLUP);

#if !defined(HW_AMOLED_143)
  touchInit(g_settings.displayRotation);
#endif

  if (!uiInit(g_settings.displayRotation, g_settings.colorScheme)) {
    Serial.println("[UI] display init failed, halting");
    while (true) delay(1000);
  }

#if defined(HW_AMOLED_143)
  delay(50);
  if (!touchInit(g_settings.displayRotation)) {
    Serial.println("[Touch] initialization failed");
  }
#endif

  uiShowSplash();
  delay(1500);

  enterState(haveConfig ? AppState::WifiConnect : AppState::ApConfig);
}

void loop() {
  const uint32_t now = millis();

  if (g_state == AppState::Running || g_state == AppState::WifiConnect) {
    if (keyLongPressed()) {
      Serial.println("[FSM] KEY long press -> reset config");
      SettingsStore::clear();
      g_settings = Settings{};
      uiApplyDisplaySettings(g_settings.displayRotation,
                             g_settings.colorScheme);
      touchSetRotation(g_settings.displayRotation);
      WiFi.disconnect(true, true);
      enterState(AppState::ApConfig);
      return;
    }
  }

  switch (g_state) {
    case AppState::ApConfig: {
      portalLoop();
      if (portalConfigSaved()) {
        Serial.println("[Portal] config saved, connecting...");
        SettingsStore::save(g_settings);
        uiApplyDisplaySettings(g_settings.displayRotation,
                               g_settings.colorScheme);
        touchSetRotation(g_settings.displayRotation);
        enterState(AppState::WifiConnect);
      }
      break;
    }

    case AppState::WifiConnect: {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] connected, IP: ");
        Serial.println(WiFi.localIP());
        g_wifiWaiting = false;
        enterState(AppState::Running);
        break;
      }

      if (g_wifiWaiting) {
        // Пауза между попытками: показываем обратный отсчёт и ждём.
        uint32_t remainMs = (g_nextAttemptMs > now) ? (g_nextAttemptMs - now) : 0;
        uint32_t remainSec = (remainMs + 500) / 1000;
        if (now - g_lastWaitUiMs >= 500 || remainSec != g_lastWaitSec) {
          g_lastWaitUiMs = now;
          g_lastWaitSec = remainSec;
          uiShowReconnectWait(g_settings.wifiSsid.c_str(), remainSec);
        }
        if (now >= g_nextAttemptMs) {
          Serial.println("[WiFi] retry attempt");
          g_wifiWaiting = false;
          WiFi.disconnect(true, true);
          WiFi.mode(WIFI_STA);
          WiFi.begin(g_settings.wifiSsid.c_str(),
                     g_settings.wifiPassword.c_str());
          g_stateEnteredMs = now;
          uiShowConnecting(g_settings.wifiSsid.c_str());
        }
        break;
      }

      // Идёт попытка подключения.
      if (now - g_stateEnteredMs > WIFI_CONNECT_TIMEOUT) {
        Serial.printf("[WiFi] connect timeout, retry in %u s\n",
                      static_cast<unsigned>(g_settings.wifiRetryDelaySec));
        WiFi.disconnect(true, true);
        g_wifiWaiting = true;
        g_nextAttemptMs = now + g_settings.wifiRetryDelaySec * 1000ULL;
        g_lastWaitSec = 0xFFFFFFFF;
        g_lastWaitUiMs = 0;
      } else {
        uiUpdateConnecting();
      }
      break;
    }

    case AppState::Running: {
      if (otaSettingsSaved()) {
        Serial.println("[OTA] settings saved");
        SettingsStore::save(g_settings);
        uiApplyDisplaySettings(g_settings.displayRotation,
                               g_settings.colorScheme);
        touchSetRotation(g_settings.displayRotation);
        if (otaTelemetrySettingsChanged()) {
          Serial.println("[OTA] telemetry settings changed, restarting telemetry...");
          if (telemetryStop()) {
            if (!telemetryStart(g_settings)) {
              Serial.println("[OTA] telemetry restart skipped");
            }
            uiSetRouterIp(g_settings.routerHost.c_str());
            g_lastTelemetryMs = 0;
          } else {
            Serial.println("[OTA] telemetry restart skipped: old task is still stopping");
          }
        }
        g_lastUiMs = 0;
      }
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] lost -> reconnect");
        enterState(AppState::WifiConnect);
        break;
      }

#if defined(HW_AMOLED_143)
      {
        int16_t touchX = 0;
        int16_t touchY = 0;
        if (touchReadTap(touchX, touchY)) {
          uiHandleTap(touchX, touchY);
        }
      }
#else
      if (touchButtonPressed()) {
        uiCycleBrightness();
      }
#endif

      bool redrawRequested = uiConsumeRedrawRequest();
      if (g_lastTelemetryMs == 0 || now - g_lastTelemetryMs >= TELEMETRY_SNAPSHOT_MS) {
        g_lastTelemetryMs = now;
        g_telemetry = telemetrySnapshot();
        uiObserveTelemetry(g_telemetry);
      }

      uint32_t uiFrameMs = uiDisplayEnabled() ? UI_RUNNING_FRAME_MS : 0;
      if (uiFrameMs > 0 &&
          (redrawRequested || g_lastUiMs == 0 || now - g_lastUiMs >= uiFrameMs)) {
        g_lastUiMs = now;
        uiShowMain(g_telemetry);
      }
      break;
    }

    case AppState::Boot:
    default:
      enterState(AppState::ApConfig);
      break;
  }
}

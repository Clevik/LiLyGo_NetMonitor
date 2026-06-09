#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"
#include "display/ui.h"
#include "portal/portal.h"
#include "net/ota.h"
#include "net/telemetry.h"

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
static uint32_t   g_keyDownMs      = 0;

static void enterState(AppState next) {
  if (g_state == AppState::ApConfig && next != AppState::ApConfig) {
    portalEnd();
  }
  if (g_state == AppState::Running && next != AppState::Running) {
    otaEnd();
    telemetryStop();
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
      WiFi.mode(WIFI_STA);
      WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPassword.c_str());
      uiShowConnecting(g_settings.wifiSsid.c_str());
      break;

    case AppState::Running:
      Serial.println("[FSM] -> RUNNING");
      g_lastUiMs = 0;
      otaBegin(g_settings);
      telemetryStart(g_settings.pingHost.c_str(), g_settings.pingIntervalSec);
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

  pinMode(PIN_KEY, INPUT_PULLUP);

  if (!uiInit()) {
    Serial.println("[UI] display init failed, halting");
    while (true) delay(1000);
  }
  uiShowSplash();
  delay(1500);

  bool haveConfig = SettingsStore::load(g_settings);
  enterState(haveConfig ? AppState::WifiConnect : AppState::ApConfig);
}

void loop() {
  const uint32_t now = millis();

  if (g_state == AppState::Running || g_state == AppState::WifiConnect) {
    if (keyLongPressed()) {
      Serial.println("[FSM] KEY long press -> reset config");
      SettingsStore::clear();
      g_settings = Settings{};
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
        enterState(AppState::WifiConnect);
      }
      break;
    }

    case AppState::WifiConnect: {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] connected, IP: ");
        Serial.println(WiFi.localIP());
        enterState(AppState::Running);
      } else if (now - g_stateEnteredMs > WIFI_CONNECT_TIMEOUT) {
        Serial.println("[WiFi] connect timeout -> AP_CONFIG");
        WiFi.disconnect(true, true);
        enterState(AppState::ApConfig);
      } else {
        uiUpdateConnecting();
      }
      break;
    }

    case AppState::Running: {
      if (otaSettingsSaved()) {
        Serial.println("[OTA] settings saved, restarting telemetry...");
        SettingsStore::save(g_settings);
        telemetryStop();
        telemetryStart(g_settings.pingHost.c_str(), g_settings.pingIntervalSec);
        g_lastUiMs = 0;
      }
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] lost -> reconnect");
        enterState(AppState::WifiConnect);
        break;
      }
      if (now - g_lastUiMs >= 500) {
        g_lastUiMs = now;
        g_telemetry = telemetrySnapshot();
        uiUpdateMain(g_telemetry);
      }
      break;
    }

    case AppState::Boot:
    default:
      enterState(AppState::ApConfig);
      break;
  }
}

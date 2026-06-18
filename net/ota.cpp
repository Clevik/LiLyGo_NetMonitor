#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "ota.h"

static constexpr size_t MAX_SETTINGS_BODY = 2048;

static AsyncWebServer *g_srv   = nullptr;
static Settings       *g_cfg   = nullptr;
static bool            g_saved = false;

static void handleApiSettings(AsyncWebServerRequest *req) {
  if (!g_cfg) {
    req->send(500, "application/json", "{}");
    return;
  }
  JsonDocument doc;
  doc["router"] = g_cfg->routerHost;
  doc["sport"]  = g_cfg->snmpPort;
  doc["sver"]   = g_cfg->snmpVersion == SnmpVersion::V1 ? 1 : 2;
  doc["scom"]   = g_cfg->snmpCommunity;
  doc["ifidx"]  = g_cfg->ifIndex;
  doc["ping"]   = g_cfg->pingHost;
  doc["pintv"]  = g_cfg->pingIntervalSec;
  doc["intv"]   = g_cfg->updateIntervalSec;
  doc["wretry"] = g_cfg->wifiRetryDelaySec;
  String json;
  serializeJson(doc, json);
  req->send(200, "application/json", json);
}

static void handleSave(AsyncWebServerRequest *req, uint8_t *data, size_t len) {
  if (!g_cfg) {
    req->send(500, "text/plain", "Internal error");
    return;
  }

  if (len > MAX_SETTINGS_BODY) {
    req->send(413, "text/plain", "Request too large");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    req->send(400, "text/plain", "Invalid JSON");
    return;
  }

  Settings next = *g_cfg;
  next.routerHost      = doc["router"] | next.routerHost;
  long snmpPort        = doc["sport"]  | static_cast<long>(next.snmpPort);
  next.snmpVersion     = (doc["sver"] | 2) == 1
                           ? SnmpVersion::V1 : SnmpVersion::V2C;
  next.snmpCommunity   = doc["scom"]   | next.snmpCommunity;
  long ifIndex         = doc["ifidx"]  | static_cast<long>(next.ifIndex);
  next.pingHost        = doc["ping"]   | next.pingHost;
  long pingIntervalSec = doc["pintv"]  | static_cast<long>(next.pingIntervalSec);
  long updateIntervalSec = doc["intv"] | static_cast<long>(next.updateIntervalSec);
  long wifiRetryDelaySec = doc["wretry"] | static_cast<long>(next.wifiRetryDelaySec);
  next.snmpPort        = (snmpPort >= 1 && snmpPort <= 65535)
                           ? static_cast<uint16_t>(snmpPort) : 0;
  next.ifIndex         = ifIndex > 0 ? static_cast<uint32_t>(ifIndex) : 0;
  next.pingIntervalSec = static_cast<uint32_t>(pingIntervalSec);
  next.updateIntervalSec = static_cast<uint32_t>(updateIntervalSec);
  next.wifiRetryDelaySec = static_cast<uint32_t>(wifiRetryDelaySec);

  normalizeSettings(next);

  String error;
  if (!validateSettings(next, &error)) {
    req->send(400, "text/plain", error);
    return;
  }

  *g_cfg = next;
  g_saved = true;
  req->send(200, "text/plain", "Saved! Applying...");
}

static void handleUpload(AsyncWebServerRequest *req, const String &filename,
                         size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    Serial.printf("[OTA] start update: %s (%u bytes)\n",
                  filename.c_str(), req->contentLength());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.println("[OTA] not enough space");
      Update.printError(Serial);
    }
  }
  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("[OTA] success, %u bytes\n", req->contentLength());
      req->send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    } else {
      Serial.print("[OTA] FAILED: ");
      Update.printError(Serial);
      req->send(500, "text/plain", "Update failed");
    }
  }
}

static void handlePostResult(AsyncWebServerRequest *req) {
  bool hasError = Update.hasError();
  if (!hasError) {
    req->send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
  } else {
    req->send(500, "text/plain", "Update failed");
  }
}

void otaBegin(Settings &settings) {
  if (g_srv) return;
  g_cfg   = &settings;
  g_saved = false;

  g_srv = new AsyncWebServer(80);

  g_srv->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/ota/index.html", "text/html");
  });

  g_srv->on("/api/settings", HTTP_GET, handleApiSettings);

  g_srv->on("/save", HTTP_ANY,
    [](AsyncWebServerRequest *req) {
      req->redirect("/");
    },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      handleSave(req, data, len);
    }
  );

  g_srv->on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/ota/ota.html", "text/html");
  });

  g_srv->on("/update", HTTP_POST, handlePostResult, handleUpload);

  g_srv->begin();
  Serial.printf("[OTA] server started on http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void otaEnd() {
  if (g_srv) {
    g_srv->end();
    delete g_srv;
    g_srv = nullptr;
  }
  g_cfg = nullptr;
}

bool otaSettingsSaved() {
  if (g_saved) {
    g_saved = false;
    return true;
  }
  return false;
}

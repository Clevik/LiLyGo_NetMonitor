#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "ota.h"
#include "display/color_schemes.h"

static constexpr size_t MAX_SETTINGS_BODY = 2048;

static AsyncWebServer *g_srv   = nullptr;
static Settings       *g_cfg   = nullptr;
static bool            g_saved = false;
static bool            g_telemetrySettingsChanged = false;

static bool telemetrySettingsEqual(const Settings &left,
                                   const Settings &right) {
  return left.routerHost == right.routerHost &&
         left.snmpPort == right.snmpPort &&
         left.snmpVersion == right.snmpVersion &&
         left.snmpCommunity == right.snmpCommunity &&
         left.ifIndex == right.ifIndex &&
         left.ifName == right.ifName &&
         left.routerApiLogin == right.routerApiLogin &&
         left.routerApiPassword == right.routerApiPassword &&
         left.rciIntervalSec == right.rciIntervalSec &&
         left.pingHost == right.pingHost &&
         left.pingIntervalSec == right.pingIntervalSec &&
         left.updateIntervalSec == right.updateIntervalSec;
}

static void addSupportedDisplayRotations(JsonDocument &doc) {
  JsonArray rotations = doc["supportedRotations"].to<JsonArray>();
  for (size_t i = 0; i < SUPPORTED_DISPLAY_ROTATION_COUNT; ++i) {
    rotations.add(SUPPORTED_DISPLAY_ROTATIONS[i]);
  }
}

static void addColorSchemes(JsonDocument &doc) {
  JsonArray schemes = doc["colorSchemes"].to<JsonArray>();
  for (size_t i = 0; i < COLOR_SCHEME_DEFINITION_COUNT; ++i) {
    const ColorSchemeDefinition &definition =
        COLOR_SCHEME_DEFINITIONS[i];
    JsonObject scheme = schemes.add<JsonObject>();
    scheme["id"] = static_cast<uint8_t>(definition.id);
    scheme["name"] = definition.name;
    char incoming[8];
    char outgoing[8];
    snprintf(incoming, sizeof(incoming), "#%06lX",
             static_cast<unsigned long>(definition.incomingRgb));
    snprintf(outgoing, sizeof(outgoing), "#%06lX",
             static_cast<unsigned long>(definition.outgoingRgb));
    scheme["incoming"] = incoming;
    scheme["outgoing"] = outgoing;
  }
}

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
  doc["apilogin"] = g_cfg->routerApiLogin;
  doc["apipass"]  = g_cfg->routerApiPassword;
  doc["rciintv"] = g_cfg->rciIntervalSec;
  doc["ping"]   = g_cfg->pingHost;
  doc["pintv"]  = g_cfg->pingIntervalSec;
  doc["intv"]   = g_cfg->updateIntervalSec;
  doc["wretry"] = g_cfg->wifiRetryDelaySec;
  doc["rotation"] = g_cfg->displayRotation;
  addSupportedDisplayRotations(doc);
  doc["colorScheme"] = static_cast<uint8_t>(g_cfg->colorScheme);
  addColorSchemes(doc);
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
  next.routerApiLogin  = doc["apilogin"] | next.routerApiLogin;
  next.routerApiPassword = doc["apipass"] | next.routerApiPassword;
  long rciIntervalSec = doc["rciintv"] | static_cast<long>(next.rciIntervalSec);
  next.pingHost        = doc["ping"]   | next.pingHost;
  long pingIntervalSec = doc["pintv"]  | static_cast<long>(next.pingIntervalSec);
  long updateIntervalSec = doc["intv"] | static_cast<long>(next.updateIntervalSec);
  long wifiRetryDelaySec = doc["wretry"] | static_cast<long>(next.wifiRetryDelaySec);
  long displayRotation = doc["rotation"] |
                         static_cast<long>(next.displayRotation);
  long colorScheme = doc["colorScheme"] |
                     static_cast<long>(
                         static_cast<uint8_t>(next.colorScheme));
  next.snmpPort        = (snmpPort >= 1 && snmpPort <= 65535)
                           ? static_cast<uint16_t>(snmpPort) : 0;
  next.ifIndex         = ifIndex > 0 ? static_cast<uint32_t>(ifIndex) : 0;
  next.rciIntervalSec  = static_cast<uint32_t>(rciIntervalSec);
  next.pingIntervalSec = static_cast<uint32_t>(pingIntervalSec);
  next.updateIntervalSec = static_cast<uint32_t>(updateIntervalSec);
  next.wifiRetryDelaySec = static_cast<uint32_t>(wifiRetryDelaySec);
  next.displayRotation =
      (displayRotation >= 0 && displayRotation <= UINT16_MAX)
          ? static_cast<uint16_t>(displayRotation)
          : UINT16_MAX;
  next.colorScheme =
      (colorScheme >= 0 && colorScheme <= UINT8_MAX)
          ? static_cast<ColorScheme>(
                static_cast<uint8_t>(colorScheme))
          : static_cast<ColorScheme>(UINT8_MAX);

  normalizeSettings(next);

  String error;
  if (!validateSettings(next, &error)) {
    req->send(400, "text/plain", error);
    return;
  }

  g_telemetrySettingsChanged = !telemetrySettingsEqual(*g_cfg, next);
  *g_cfg = next;
  g_saved = true;
  req->send(200, "text/plain", "Saved! Applying...");
}

static void handleSaveRequest(AsyncWebServerRequest *req) {
  if (req->contentLength() > MAX_SETTINGS_BODY) {
    if (req->_tempObject) {
      delete static_cast<String *>(req->_tempObject);
      req->_tempObject = nullptr;
    }
    req->send(413, "text/plain", "Request too large");
    return;
  }

  String *body = static_cast<String *>(req->_tempObject);
  req->_tempObject = nullptr;
  if (!body) {
    req->send(400, "text/plain", "Missing JSON");
    return;
  }

  handleSave(req,
             reinterpret_cast<uint8_t *>(const_cast<char *>(body->c_str())),
             body->length());
  delete body;
}

static void handleSaveBody(AsyncWebServerRequest *req,
                           uint8_t *data,
                           size_t len,
                           size_t index,
                           size_t total) {
  if (total > MAX_SETTINGS_BODY) return;

  if (index == 0) {
    if (req->_tempObject) {
      delete static_cast<String *>(req->_tempObject);
      req->_tempObject = nullptr;
    }
    auto *body = new String();
    body->reserve(total + 1);
    req->_tempObject = body;
  }

  auto *body = static_cast<String *>(req->_tempObject);
  if (!body) return;
  for (size_t i = 0; i < len; i++) {
    *body += static_cast<char>(data[i]);
  }
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
  g_telemetrySettingsChanged = false;

  g_srv = new AsyncWebServer(80);

  g_srv->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/ota/index.html", "text/html");
  });

  g_srv->on("/api/settings", HTTP_GET, handleApiSettings);

  g_srv->on("/save", HTTP_POST, handleSaveRequest, nullptr, handleSaveBody);
  g_srv->on("/save", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("/");
  });

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

bool otaTelemetrySettingsChanged() {
  return g_telemetrySettingsChanged;
}

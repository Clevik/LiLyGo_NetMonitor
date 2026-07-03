#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "ota.h"
#include "ota_update.h"
#include "telemetry.h"
#include "time_service.h"
#include "display/color_schemes.h"

static constexpr size_t MAX_SETTINGS_BODY = 4096;

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
         left.routerApiInterface == right.routerApiInterface &&
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

static void addPowerSettings(JsonDocument &doc) {
  JsonObject object = doc["powerSafe"].to<JsonObject>();
  object["startupBrightness"] =
      static_cast<uint8_t>(g_cfg->powerSave.startupBrightness);
  object["autoOffEnabled"] = g_cfg->powerSave.autoOffEnabled;
  object["autoOffMinutes"] = g_cfg->powerSave.autoOffMinutes;
  object["scheduleEnabled"] = g_cfg->powerSave.scheduleEnabled;
  object["nightStartMinute"] = g_cfg->powerSave.nightStartMinute;
  object["nightEndMinute"] = g_cfg->powerSave.nightEndMinute;
  object["nightBrightness"] =
      static_cast<uint8_t>(g_cfg->powerSave.nightBrightness);
  object["timeZone"] = g_cfg->powerSave.timeZone;
}

static bool readPowerSettings(const JsonDocument &doc,
                              Settings &settings,
                              String &error) {
  JsonVariantConst value = doc["powerSafe"];
  if (value.isNull()) return true;
  if (!value.is<JsonObjectConst>()) {
    error = "Power Safe must be an object";
    return false;
  }
  JsonObjectConst power = value.as<JsonObjectConst>();
  const char *numericFields[] = {
      "startupBrightness", "autoOffMinutes", "nightStartMinute",
      "nightEndMinute", "nightBrightness"};
  for (const char *field : numericFields) {
    if (!power[field].isNull() && !power[field].is<long>()) {
      error = String("Power Safe field must be an integer: ") + field;
      return false;
    }
  }
  const char *booleanFields[] = {"autoOffEnabled", "scheduleEnabled"};
  for (const char *field : booleanFields) {
    if (!power[field].isNull() && !power[field].is<bool>()) {
      error = String("Power Safe field must be boolean: ") + field;
      return false;
    }
  }
  if (!power["timeZone"].isNull() &&
      !power["timeZone"].is<const char *>()) {
    error = "Power Safe timeZone must be a string";
    return false;
  }

  long startupBrightness =
      power["startupBrightness"] |
      static_cast<long>(
          static_cast<uint8_t>(settings.powerSave.startupBrightness));
  long autoOffMinutes =
      power["autoOffMinutes"] |
      static_cast<long>(settings.powerSave.autoOffMinutes);
  long nightStartMinute =
      power["nightStartMinute"] |
      static_cast<long>(settings.powerSave.nightStartMinute);
  long nightEndMinute =
      power["nightEndMinute"] |
      static_cast<long>(settings.powerSave.nightEndMinute);
  long nightBrightness =
      power["nightBrightness"] |
      static_cast<long>(
          static_cast<uint8_t>(settings.powerSave.nightBrightness));

  settings.powerSave.startupBrightness =
      startupBrightness >= 0 && startupBrightness <= UINT8_MAX
          ? static_cast<BrightnessLevel>(
                static_cast<uint8_t>(startupBrightness))
          : static_cast<BrightnessLevel>(UINT8_MAX);
  settings.powerSave.autoOffEnabled =
      power["autoOffEnabled"] | settings.powerSave.autoOffEnabled;
  settings.powerSave.autoOffMinutes =
      autoOffMinutes >= 0 && autoOffMinutes <= UINT16_MAX
          ? static_cast<uint16_t>(autoOffMinutes)
          : 0;
  settings.powerSave.scheduleEnabled =
      power["scheduleEnabled"] | settings.powerSave.scheduleEnabled;
  settings.powerSave.nightStartMinute =
      nightStartMinute >= 0 && nightStartMinute <= UINT16_MAX
          ? static_cast<uint16_t>(nightStartMinute)
          : UINT16_MAX;
  settings.powerSave.nightEndMinute =
      nightEndMinute >= 0 && nightEndMinute <= UINT16_MAX
          ? static_cast<uint16_t>(nightEndMinute)
          : UINT16_MAX;
  settings.powerSave.nightBrightness =
      nightBrightness >= 0 && nightBrightness <= UINT8_MAX
          ? static_cast<BrightnessLevel>(
                static_cast<uint8_t>(nightBrightness))
          : static_cast<BrightnessLevel>(UINT8_MAX);
  settings.powerSave.timeZone =
      power["timeZone"] | settings.powerSave.timeZone;
  return true;
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
  doc["apiinterface"] = g_cfg->routerApiInterface;
  doc["rciintv"] = g_cfg->rciIntervalSec;
  doc["ping"]   = g_cfg->pingHost;
  doc["pintv"]  = g_cfg->pingIntervalSec;
  doc["intv"]   = g_cfg->updateIntervalSec;
  doc["wretry"] = g_cfg->wifiRetryDelaySec;
  doc["rotation"] = g_cfg->displayRotation;
  addSupportedDisplayRotations(doc);
  doc["colorScheme"] = static_cast<uint8_t>(g_cfg->colorScheme);
  addColorSchemes(doc);
  addPowerSettings(doc);
  String json;
  serializeJson(doc, json);
  req->send(200, "application/json", json);
}

static void handleSave(AsyncWebServerRequest *req, uint8_t *data, size_t len) {
  if (!g_cfg) {
    req->send(500, "text/plain", "Internal error");
    return;
  }
  if (otaUpdateBusy()) {
    req->send(409, "text/plain", "Settings are locked during OTA");
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
  next.routerApiInterface =
      doc["apiinterface"] | next.routerApiInterface;
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
  String error;
  if (!readPowerSettings(doc, next, error)) {
    req->send(400, "text/plain", error);
    return;
  }

  normalizeSettings(next);

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

static String *takeRequestBody(AsyncWebServerRequest *req) {
  String *body = static_cast<String *>(req->_tempObject);
  req->_tempObject = nullptr;
  return body;
}

static void collectRequestBody(AsyncWebServerRequest *req,
                               uint8_t *data,
                               size_t len,
                               size_t index,
                               size_t total) {
  if (total > MAX_SETTINGS_BODY) return;
  if (index == 0) {
    if (req->_tempObject) {
      delete static_cast<String *>(req->_tempObject);
    }
    auto *body = new String();
    body->reserve(total + 1);
    req->_tempObject = body;
  }
  auto *body = static_cast<String *>(req->_tempObject);
  if (!body) return;
  for (size_t i = 0; i < len; ++i) {
    *body += static_cast<char>(data[i]);
  }
}

static void sendOtaStatus(AsyncWebServerRequest *req, int status = 200) {
  JsonDocument doc;
  otaUpdateAddStatus(doc);
  String json;
  serializeJson(doc, json);
  req->send(status, "application/json", json);
}

static void handleOtaStart(AsyncWebServerRequest *req) {
  String *body = takeRequestBody(req);
  if (!body) {
    req->send(400, "text/plain", "Missing JSON");
    return;
  }
  JsonDocument doc;
  DeserializationError parseError = deserializeJson(doc, *body);
  delete body;
  if (parseError) {
    req->send(400, "text/plain", "Invalid JSON");
    return;
  }

  String error;
  if (!otaUpdateStart(doc, error)) {
    req->send(otaUpdateBusy() ? 409 : 400, "text/plain", error);
    return;
  }
  sendOtaStatus(req, 202);
}

static String uploadType(AsyncWebServerRequest *req) {
  if (!req->hasParam("type")) return "";
  return req->getParam("type")->value();
}

static size_t uploadSize(AsyncWebServerRequest *req) {
  if (!req->hasParam("size")) return 0;
  return static_cast<size_t>(
      strtoull(req->getParam("size")->value().c_str(), nullptr, 10));
}

static void setUploadError(AsyncWebServerRequest *req, const String &error) {
  if (req->_tempObject) return;
  req->_tempObject = new String(error);
}

static void handleOtaUploadData(AsyncWebServerRequest *req,
                                const String &filename,
                                size_t index,
                                uint8_t *data,
                                size_t len,
                                bool final) {
  String type = uploadType(req);
  if (index == 0) {
    if (req->_tempObject) {
      delete static_cast<String *>(req->_tempObject);
      req->_tempObject = nullptr;
    }
    Serial.printf("[OTA] upload %s: %s (%u bytes)\n",
                  type.c_str(), filename.c_str(),
                  static_cast<unsigned>(uploadSize(req)));
    String error;
    if (!otaUpdateBeginUpload(type, uploadSize(req), error)) {
      setUploadError(req, error);
      return;
    }
  }

  if (req->_tempObject) return;
  String error;
  if (!otaUpdateWrite(type, index, data, len, final, error)) {
    setUploadError(req, error);
  }
}

static void handleOtaUploadResult(AsyncWebServerRequest *req) {
  String *error = takeRequestBody(req);
  if (error) {
    req->send(400, "text/plain", *error);
    delete error;
    return;
  }
  sendOtaStatus(req);
}

static void handleOtaCommit(AsyncWebServerRequest *req) {
  String error;
  if (!otaUpdateCommit(error)) {
    req->send(409, "text/plain", error);
    return;
  }
  sendOtaStatus(req);
}

static void handleOtaCancel(AsyncWebServerRequest *req) {
  otaUpdateCancel();
  sendOtaStatus(req);
}

static void handleOtaRollback(AsyncWebServerRequest *req) {
  String error;
  if (!otaUpdateManualRollback(error)) {
    req->send(409, "text/plain", error);
    return;
  }
  sendOtaStatus(req);
}

void otaRegisterUpdateRoutes(AsyncWebServer &server) {
  server.on("/api/ota/status", HTTP_GET,
            [](AsyncWebServerRequest *req) { sendOtaStatus(req); });
  server.on("/api/ota/start", HTTP_POST, handleOtaStart, nullptr,
            collectRequestBody);
  server.on("/api/ota/upload", HTTP_POST,
            handleOtaUploadResult, handleOtaUploadData);
  server.on("/api/ota/commit", HTTP_POST, handleOtaCommit);
  server.on("/api/ota/cancel", HTTP_POST, handleOtaCancel);
  server.on("/api/ota/rollback", HTTP_POST, handleOtaRollback);
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
  g_srv->on("/api/timezones", HTTP_GET,
            [](AsyncWebServerRequest *req) {
              AsyncResponseStream *response =
                  req->beginResponseStream("application/json");
              timeServicePrintZones(*response);
              req->send(response);
            });

  g_srv->on("/save", HTTP_POST, handleSaveRequest, nullptr, handleSaveBody);
  g_srv->on("/save", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("/");
  });

  g_srv->on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/ota/ota.html", "text/html");
  });

  otaRegisterUpdateRoutes(*g_srv);

  g_srv->begin();
  Serial.printf("[OTA] server started on http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void otaLoop(const Settings &settings) {
  otaUpdateLoop();
  if (otaUpdateTelemetryPauseRequested()) {
    otaUpdateSetTelemetryPaused(telemetryStop());
  }
  if (otaUpdateTelemetryResumeRequested()) {
    if (telemetryStart(settings)) {
      otaUpdateSetTelemetryResumed();
    }
  }
}

void otaEnd() {
  otaUpdateCancel();
  otaUpdateSetTelemetryResumed();
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

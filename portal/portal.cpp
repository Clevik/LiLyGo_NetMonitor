#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "portal.h"
#include "net/ota.h"
#include "net/ota_update.h"
#include "net/time_service.h"
#include "display/color_schemes.h"

static const char *AP_PASSWORD = "12345678";

static DNSServer         *g_dns   = nullptr;
static AsyncWebServer    *g_http  = nullptr;
static Settings          *g_cfg   = nullptr;
static bool               g_saved = false;
static char               g_apName[32];

static constexpr size_t MAX_SETTINGS_BODY = 4096;

enum ScanState { IDLE, REQUESTED, DONE };
static ScanState g_scanState = IDLE;
static String    g_scanJson;

static void addDisplaySettings(JsonDocument &doc) {
  doc["rotation"] = g_cfg ? g_cfg->displayRotation
                           : DEFAULT_DISPLAY_ROTATION;
  JsonArray rotations = doc["supportedRotations"].to<JsonArray>();
  for (size_t i = 0; i < SUPPORTED_DISPLAY_ROTATION_COUNT; ++i) {
    rotations.add(SUPPORTED_DISPLAY_ROTATIONS[i]);
  }

  doc["colorScheme"] = static_cast<uint8_t>(
      g_cfg ? g_cfg->colorScheme : DEFAULT_COLOR_SCHEME);
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
  const PowerSaveSettings power =
      g_cfg ? g_cfg->powerSave : PowerSaveSettings{};
  JsonObject object = doc["powerSafe"].to<JsonObject>();
  object["startupBrightness"] =
      static_cast<uint8_t>(power.startupBrightness);
  object["autoOffEnabled"] = power.autoOffEnabled;
  object["autoOffMinutes"] = power.autoOffMinutes;
  object["scheduleEnabled"] = power.scheduleEnabled;
  object["nightStartMinute"] = power.nightStartMinute;
  object["nightEndMinute"] = power.nightEndMinute;
  object["nightBrightness"] =
      static_cast<uint8_t>(power.nightBrightness);
  object["timeZone"] = power.timeZone;
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
  JsonDocument doc;
  doc["apiinterface"] = g_cfg
      ? g_cfg->routerApiInterface
      : DEFAULT_ROUTER_API_INTERFACE;
  addDisplaySettings(doc);
  addPowerSettings(doc);
  String json;
  serializeJson(doc, json);
  req->send(200, "application/json", json);
}

static void handleScan(AsyncWebServerRequest *req) {
  if (g_scanState == DONE) {
    g_scanState = IDLE;
    req->send(200, "application/json", g_scanJson);
    return;
  }
  if (g_scanState == IDLE) {
    g_scanState = REQUESTED;
  }
  req->send(202, "application/json", "[\"scanning\"]");
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
  next.wifiSsid          = doc["ssid"]   | "";
  next.wifiPassword      = doc["wpass"]  | "";
  next.routerHost        = doc["router"] | "";
  long snmpPort          = doc["sport"]  | 161L;
  next.snmpVersion       = (doc["sver"] | 2) == 1
                             ? SnmpVersion::V1 : SnmpVersion::V2C;
  next.snmpCommunity     = doc["scom"]   | "public";
  long ifIndex           = doc["ifidx"]  | 0L;
  next.routerApiLogin    = doc["apilogin"] | "";
  next.routerApiPassword = doc["apipass"]  | "";
  next.routerApiInterface =
      doc["apiinterface"] | DEFAULT_ROUTER_API_INTERFACE;
  long rciIntervalSec    = doc["rciintv"] | static_cast<long>(DEFAULT_RCI_INTERVAL_SEC);
  next.pingHost          = doc["ping"]   | "8.8.8.8";
  long pingIntervalSec   = doc["pintv"]  | 5L;
  long updateIntervalSec = doc["intv"]   | 5L;
  long wifiRetryDelaySec = doc["wretry"] | 20L;
  long displayRotation   = doc["rotation"] |
                           static_cast<long>(DEFAULT_DISPLAY_ROTATION);
  long colorScheme       = doc["colorScheme"] |
                           static_cast<long>(
                               static_cast<uint8_t>(
                                   DEFAULT_COLOR_SCHEME));
  next.snmpPort          = (snmpPort >= 1 && snmpPort <= 65535)
                             ? static_cast<uint16_t>(snmpPort) : 0;
  next.ifIndex           = ifIndex > 0 ? static_cast<uint32_t>(ifIndex) : 0;
  next.rciIntervalSec    = static_cast<uint32_t>(rciIntervalSec);
  next.pingIntervalSec   = static_cast<uint32_t>(pingIntervalSec);
  next.updateIntervalSec = static_cast<uint32_t>(updateIntervalSec);
  next.wifiRetryDelaySec = static_cast<uint32_t>(wifiRetryDelaySec);
  next.displayRotation   =
      (displayRotation >= 0 && displayRotation <= UINT16_MAX)
          ? static_cast<uint16_t>(displayRotation)
          : UINT16_MAX;
  next.colorScheme       =
      (colorScheme >= 0 && colorScheme <= UINT8_MAX)
          ? static_cast<ColorScheme>(
                static_cast<uint8_t>(colorScheme))
          : static_cast<ColorScheme>(UINT8_MAX);
  String error;
  if (!readPowerSettings(doc, next, error)) {
    req->send(400, "text/plain", error);
    return;
  }
  next.configured        = true;

  normalizeSettings(next);

  if (!validateSettings(next, &error)) {
    req->send(400, "text/plain", error);
    return;
  }

  *g_cfg = next;
  g_saved = true;
  req->send(200, "text/plain", "Saved! Reconnecting...");
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

void portalBegin(Settings &settings) {
  g_cfg   = &settings;
  g_saved = false;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_apName, sizeof(g_apName), "NETMONITOR-%02X%02X",
           mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_apName, AP_PASSWORD);
  WiFi.disconnect(true);

  Serial.printf("[Portal] AP started: %s, IP: %s\n",
                g_apName, WiFi.softAPIP().toString().c_str());

  g_dns = new DNSServer();
  g_dns->start(53, "*", WiFi.softAPIP());

  g_http = new AsyncWebServer(80);
  g_http->on("/api/settings", HTTP_GET, handleApiSettings);
  g_http->on("/api/timezones", HTTP_GET,
    [](AsyncWebServerRequest *req) {
      AsyncResponseStream *response =
          req->beginResponseStream("application/json");
      timeServicePrintZones(*response);
      req->send(response);
    });
  g_http->on("/scan", HTTP_GET, handleScan);
  g_http->on("/save", HTTP_POST, handleSaveRequest, nullptr, handleSaveBody);
  g_http->on("/save", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->redirect("/");
  });
  g_http->serveStatic("/", LittleFS, "/portal/").setDefaultFile("index.html");
  g_http->onNotFound([](AsyncWebServerRequest *req) {
    if (req->method() == HTTP_GET) {
      req->redirect("http://" + WiFi.softAPIP().toString() + "/");
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });

  g_http->on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/ota/ota.html", "text/html");
  });
  otaRegisterUpdateRoutes(*g_http);

  g_http->begin();

  Serial.println("[Portal] web server started");
}

void portalLoop() {
  if (g_dns) g_dns->processNextRequest();
  otaUpdateLoop();
  if (otaUpdateTelemetryPauseRequested()) {
    otaUpdateSetTelemetryPaused(true);
  }
  if (otaUpdateTelemetryResumeRequested()) {
    otaUpdateSetTelemetryResumed();
  }
  if (g_scanState == REQUESTED) {
    int n = WiFi.scanNetworks(false, true, false, 300);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["ssid"] = WiFi.SSID(i);
      obj["rssi"] = WiFi.RSSI(i);
      obj["auth"] = WiFi.encryptionType(i);
    }
    WiFi.scanDelete();
    g_scanJson.reserve(measureJson(doc) + 1);
    serializeJson(doc, g_scanJson);
    g_scanState = DONE;
    Serial.printf("[Portal] scan done, %d networks\n", n);
  }
}

bool portalConfigSaved() {
  return g_saved;
}

void portalEnd() {
  otaUpdateCancel();
  otaUpdateSetTelemetryResumed();
  if (g_http) { g_http->end(); delete g_http; g_http = nullptr; }
  if (g_dns)  { delete g_dns; g_dns = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[Portal] stopped");
}

const char *portalGetApName() {
  return g_apName;
}

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <LittleFS.h>

#include "portal.h"

static const char *AP_PASSWORD = "12345678";

static DNSServer         *g_dns   = nullptr;
static AsyncWebServer    *g_http  = nullptr;
static Settings          *g_cfg   = nullptr;
static bool               g_saved = false;
static char               g_apName[32];

static constexpr size_t MAX_SETTINGS_BODY = 2048;

enum ScanState { IDLE, REQUESTED, DONE };
static ScanState g_scanState = IDLE;
static String    g_scanJson;

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
  next.pingHost          = doc["ping"]   | "8.8.8.8";
  long pingIntervalSec   = doc["pintv"]  | 5L;
  long updateIntervalSec = doc["intv"]   | 5L;
  next.snmpPort          = (snmpPort >= 1 && snmpPort <= 65535)
                             ? static_cast<uint16_t>(snmpPort) : 0;
  next.ifIndex           = ifIndex > 0 ? static_cast<uint32_t>(ifIndex) : 0;
  next.pingIntervalSec   = static_cast<uint32_t>(pingIntervalSec);
  next.updateIntervalSec = static_cast<uint32_t>(updateIntervalSec);
  next.configured        = true;

  normalizeSettings(next);

  String error;
  if (!validateSettings(next, &error)) {
    req->send(400, "text/plain", error);
    return;
  }

  *g_cfg = next;
  g_saved = true;
  req->send(200, "text/plain", "Saved! Reconnecting...");
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
  g_http->serveStatic("/", LittleFS, "/portal/").setDefaultFile("index.html");
  g_http->on("/scan", HTTP_GET, handleScan);
  g_http->on("/save", HTTP_ANY,
    [](AsyncWebServerRequest *req) {
      req->redirect("/");
    },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      handleSave(req, data, len);
    }
  );
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
  g_http->on("/update", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      if (Update.hasError()) {
        req->send(500, "text/plain", "Update failed");
      } else {
        req->send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
      }
    },
    [](AsyncWebServerRequest *req, const String &filename,
       size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("[OTA] portal upload: %s (%u bytes)\n",
                      filename.c_str(), req->contentLength());
        Update.begin(UPDATE_SIZE_UNKNOWN);
      }
      Update.write(data, len);
      if (final) Update.end(true);
    }
  );

  g_http->begin();

  Serial.println("[Portal] web server started");
}

void portalLoop() {
  if (g_dns) g_dns->processNextRequest();
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
  if (g_http) { g_http->end(); delete g_http; g_http = nullptr; }
  if (g_dns)  { delete g_dns; g_dns = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[Portal] stopped");
}

const char *portalGetApName() {
  return g_apName;
}

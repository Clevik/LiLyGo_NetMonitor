#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "portal.h"

static const char *AP_PASSWORD = "12345678";

static DNSServer         *g_dns   = nullptr;
static AsyncWebServer    *g_http  = nullptr;
static Settings          *g_cfg   = nullptr;
static bool               g_saved = false;
static char               g_apName[32];

static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>NETMONITOR Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;padding:16px}
h1{color:#07ff;text-align:center;margin:12px 0}
.card{background:#16213e;border-radius:10px;padding:16px;margin:12px 0}
label{display:block;margin:8px 0 4px;font-size:14px;color:#aaa}
input,select{width:100%;padding:10px;border:1px solid #333;border-radius:6px;
  background:#0f3460;color:#fff;font-size:16px}
input:focus{border-color:#07ff}
.btn{display:block;width:100%;padding:14px;margin-top:20px;
  background:#07ff;color:#000;border:none;border-radius:8px;
  font-size:18px;font-weight:700;cursor:pointer;text-align:center}
.btn:hover{background:#05d}
.wifi-item{padding:10px;margin:4px 0;background:#0f3460;border-radius:6px;
  cursor:pointer;display:flex;justify-content:space-between}
.wifi-item:hover{background:#1a5276}
.rssi{color:#aaa;font-size:12px}
small{color:#666;font-size:12px}
#status{text-align:center;padding:8px;color:#07ff;display:none}
</style></head><body>
<h1>NETMONITOR</h1>

<div class='card'>
<label>Wi-Fi Network</label>
<div id='wifiList'>Scanning...</div>
<input id='ssid' name='ssid' placeholder='Enter SSID or select above'>
</div>

<div class='card'>
<label>Wi-Fi Password</label>
<input id='wpass' type='password' placeholder='Password'>
</div>

<div class='card'>
<label>Router IP / Host</label>
<input id='router' value='192.168.1.1'>
<label>SNMP Port</label>
<input id='sport' type='number' value='161'>
<label>SNMP Version</label>
<select id='sver'><option value='1'>v1</option><option value='2' selected>v2c</option></select>
<label>SNMP Community</label>
<input id='scom' value='public'>
<label>Interface Index</label>
<input id='ifidx' type='number' value='0'>
<small>0 = auto-detect WAN</small>
</div>

<div class='card'>
<label>Ping Host</label>
<input id='ping' value='8.8.8.8'>
<label>Update Interval (sec)</label>
<input id='intv' type='number' value='5'>
</div>

<div id='status'></div>
<button class='btn' onclick='doSave()'>Save &amp; Connect</button>
<a class='btn' href='/ota' style='margin-top:8px;background:#555;color:#fff'>Firmware Update (OTA)</a>

<script>
function scan(){
  fetch('/scan').then(r=>r.json()).then(nets=>{
    var h='';
    nets.sort((a,b)=>b.rssi-a.rssi);
    nets.forEach(n=>{
      var lock=n.auth>0?'&#128274;':'';
      h+='<div class="wifi-item" onclick="pick(\''+n.ssid.replace(/'/g,"\\'")+'\')">'
        +lock+' <b>'+n.ssid+'</b><span class="rssi">'+n.rssi+' dBm</span></div>';
    });
    document.getElementById('wifiList').innerHTML=h||'No networks found';
  }).catch(()=>{document.getElementById('wifiList').innerHTML='Scan failed';});
}
function pick(s){document.getElementById('ssid').value=s;}
function doSave(){
  var d={
    ssid:document.getElementById('ssid').value,
    wpass:document.getElementById('wpass').value,
    router:document.getElementById('router').value,
    sport:+document.getElementById('sport').value,
    sver:+document.getElementById('sver').value,
    scom:document.getElementById('scom').value,
    ifidx:+document.getElementById('ifidx').value,
    ping:document.getElementById('ping').value,
    intv:+document.getElementById('intv').value
  };
  if(!d.ssid){alert('Enter SSID');return;}
  var st=document.getElementById('status');
  st.style.display='block'; st.textContent='Saving...';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(d)})
    .then(r=>r.text()).then(t=>{st.textContent=t;})
    .catch(e=>{st.textContent='Error: '+e;});
}
scan();
</script></body></html>
)rawliteral";

static void handleScan(AsyncWebServerRequest *req) {
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"";
    json += WiFi.SSID(i);
    json += "\",\"rssi\":";
    json += WiFi.RSSI(i);
    json += ",\"auth\":";
    json += WiFi.encryptionType(i);
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  req->send(200, "application/json", json);
}

static void handleSave(AsyncWebServerRequest *req, uint8_t *data, size_t len) {
  if (!g_cfg) {
    req->send(500, "text/plain", "Internal error");
    return;
  }

  String body;
  body.reserve(len + 1);
  body.concat((const char *)data, len);

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    req->send(400, "text/plain", "Invalid JSON");
    return;
  }

  g_cfg->wifiSsid         = doc["ssid"]   | "";
  g_cfg->wifiPassword     = doc["wpass"]  | "";
  g_cfg->routerHost       = doc["router"] | "";
  g_cfg->snmpPort         = doc["sport"]  | 161;
  g_cfg->snmpVersion      = (doc["sver"] | 2) == 1
                              ? SnmpVersion::V1 : SnmpVersion::V2C;
  g_cfg->snmpCommunity    = doc["scom"]   | "public";
  g_cfg->ifIndex          = doc["ifidx"]  | 0;
  g_cfg->pingHost         = doc["ping"]   | "8.8.8.8";
  g_cfg->updateIntervalSec = doc["intv"]  | 5;
  g_cfg->configured       = true;

  if (g_cfg->wifiSsid.length() == 0) {
    req->send(400, "text/plain", "SSID is empty");
    return;
  }

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
  g_http->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", HTML_PAGE);
  });
  g_http->on("/scan", HTTP_GET, handleScan);
  g_http->on("/save", HTTP_POST, nullptr, nullptr,
             [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
               handleSave(req, data, len);
             });
  g_http->onNotFound([](AsyncWebServerRequest *req) {
    req->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });

  g_http->on("/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", HTML_PAGE);
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

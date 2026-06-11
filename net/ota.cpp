#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "ota.h"

static constexpr size_t MAX_SETTINGS_BODY = 2048;

static AsyncWebServer *g_srv   = nullptr;
static Settings       *g_cfg   = nullptr;
static bool            g_saved = false;

static const char SETTINGS_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>NETMONITOR Settings</title>
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
.btn2{margin-top:8px;background:#555;color:#fff;text-decoration:none;
  display:block;width:100%;padding:14px;border-radius:8px;
  font-size:18px;font-weight:700;text-align:center;box-sizing:border-box}
.btn2:hover{background:#777}
small{color:#666;font-size:12px}
#status{text-align:center;padding:8px;color:#07ff;display:none}
</style></head><body>
<h1>NETMONITOR</h1>
<div class='card'>
<label>Router IP</label>
<input id='router' value='{{ROUTER}}'>
<label>SNMP Port</label>
<input id='sport' type='number' value='{{SPORT}}'>
<label>SNMP Version</label>
<select id='sver'><option value='1'>v1</option><option value='2'>v2c</option></select>
<label>SNMP Community</label>
<input id='scom' value='{{SCOM}}'>
<label>Interface Index</label>
<input id='ifidx' type='number' min='1' step='1' value='{{IFIDX}}'>
<small>Required WAN interface index. 0 is not supported.</small>
</div>
<div class='card'>
<label>Ping Host</label>
<input id='ping' value='{{PING}}'>
<label>External Ping Interval (sec)</label>
<input id='pintv' type='number' min='1' max='3600' step='1' value='{{PINTV}}'>
<small>How often to ping the external host for latency and loss.</small>
<label>SNMP Poll Interval (sec)</label>
<input id='intv' type='number' min='1' max='3600' step='1' value='{{INTV}}'>
<small>How often to poll router SNMP status and traffic counters.</small>
</div>
<div id='status'></div>
<button class='btn' onclick='doSave()'>Save</button>
<a class='btn2' href='/ota'>Firmware Update (OTA)</a>
<script>
document.getElementById('sver').value='{{SVER}}';
function validInterval(v){return v>=1&&v<=3600&&Math.floor(v)===v;}
function validPort(v){return v>=1&&v<=65535&&Math.floor(v)===v;}
function doSave(){
  var ifidx=+document.getElementById('ifidx').value;
  var sport=+document.getElementById('sport').value;
  var pintv=+document.getElementById('pintv').value;
  var intv=+document.getElementById('intv').value;
  if(!ifidx||ifidx<1||Math.floor(ifidx)!==ifidx){alert('Enter Interface Index greater than 0');return;}
  if(!validPort(sport)){alert('SNMP Port must be 1..65535');return;}
  if(!validInterval(pintv)||!validInterval(intv)){alert('Ping and SNMP intervals must be 1..3600 sec');return;}
  var d={
    router:document.getElementById('router').value,
    sport:sport,
    sver:+document.getElementById('sver').value,
    scom:document.getElementById('scom').value,
    ifidx:ifidx,
    ping:document.getElementById('ping').value,
    pintv:pintv,
    intv:intv
  };
  if(!d.router.trim()){alert('Enter Router IP');return;}
  if(!d.scom.trim()){alert('Enter SNMP Community');return;}
  if(!d.ping.trim()){alert('Enter Ping Host');return;}
  var st=document.getElementById('status');
  st.style.display='block'; st.textContent='Saving...';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(d)})
    .then(r=>r.text()).then(t=>{st.textContent=t;})
    .catch(e=>{st.textContent='Error: '+e;});
}
</script></body></html>
)rawliteral";

static const char OTA_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>NETMONITOR OTA</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;padding:16px}
h1{color:#07ff;text-align:center;margin:12px 0}
.card{background:#16213e;border-radius:10px;padding:16px;margin:12px 0}
.btn{display:block;width:100%;padding:14px;margin-top:12px;
  background:#07ff;color:#000;border:none;border-radius:8px;
  font-size:18px;font-weight:700;cursor:pointer;text-align:center}
.btn:hover{background:#05d}
.btn:disabled{background:#555;cursor:not-allowed}
.btn2{margin-top:8px;background:#555;color:#fff;text-decoration:none;
  display:block;width:100%;padding:14px;border-radius:8px;
  font-size:18px;font-weight:700;text-align:center;box-sizing:border-box}
.btn2:hover{background:#777}
input[type=file]{width:100%;padding:10px;color:#ccc}
#bar{width:100%;height:24px;background:#333;border-radius:12px;
  margin-top:12px;overflow:hidden}
#fill{height:100%;width:0%;background:#07ff;border-radius:12px;
  transition:width .3s}
#msg{text-align:center;margin-top:8px;color:#aaa}
</style></head><body>
<h1>NETMONITOR</h1>
<div class='card'>
<input type='file' id='fw' accept='.bin'>
<button class='btn' id='go' onclick='upload()'>Upload Firmware</button>
<div id='bar'><div id='fill'></div></div>
<div id='msg'>Select firmware .bin file</div>
</div>
<a class='btn2' href='/'>Back to Settings</a>
<script>
function upload(){
  var f=document.getElementById('fw').files[0];
  if(!f){alert('Select file');return;}
  var go=document.getElementById('go');
  go.disabled=true;
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      var p=Math.round(e.loaded/e.total*100);
      document.getElementById('fill').style.width=p+'%';
      document.getElementById('msg').textContent='Uploading: '+p+'%';
    }
  };
  xhr.onload=function(){
    document.getElementById('msg').textContent=
      xhr.status===200?'Update OK! Rebooting...':'Error: '+xhr.status;
    go.disabled=false;
  };
  xhr.onerror=function(){
    document.getElementById('msg').textContent='Network error';
    go.disabled=false;
  };
  xhr.send(f);
}
</script></body></html>
)rawliteral";

static String buildSettingsPage() {
  String html;
  html.reserve(sizeof(SETTINGS_PAGE) + 128);
  html = FPSTR(SETTINGS_PAGE);
  html.replace("{{ROUTER}}", g_cfg ? g_cfg->routerHost : "");
  html.replace("{{SPORT}}", g_cfg ? String(g_cfg->snmpPort) : "161");
  html.replace("{{SVER}}", g_cfg ? (g_cfg->snmpVersion == SnmpVersion::V1 ? "1" : "2") : "2");
  html.replace("{{SCOM}}", g_cfg ? g_cfg->snmpCommunity : "public");
  html.replace("{{IFIDX}}", g_cfg && g_cfg->ifIndex > 0 ? String(g_cfg->ifIndex) : "");
  html.replace("{{PING}}", g_cfg ? g_cfg->pingHost : "8.8.8.8");
  html.replace("{{PINTV}}", g_cfg ? String(g_cfg->pingIntervalSec) : "5");
  html.replace("{{INTV}}", g_cfg ? String(g_cfg->updateIntervalSec) : "5");
  return html;
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
  next.snmpPort        = (snmpPort >= 1 && snmpPort <= 65535)
                           ? static_cast<uint16_t>(snmpPort) : 0;
  next.ifIndex         = ifIndex > 0 ? static_cast<uint32_t>(ifIndex) : 0;
  next.pingIntervalSec = static_cast<uint32_t>(pingIntervalSec);
  next.updateIntervalSec = static_cast<uint32_t>(updateIntervalSec);

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
    req->send(200, "text/html", buildSettingsPage());
  });

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
    req->send_P(200, "text/html", OTA_PAGE);
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

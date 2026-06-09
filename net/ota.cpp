#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

#include "ota.h"

static AsyncWebServer *g_otaSrv = nullptr;

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

void otaBegin() {
  if (g_otaSrv) return;
  g_otaSrv = new AsyncWebServer(80);

  g_otaSrv->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", OTA_PAGE);
  });

  g_otaSrv->on("/update", HTTP_POST, handlePostResult, handleUpload);

  g_otaSrv->begin();
  Serial.printf("[OTA] server started on http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void otaEnd() {
  if (g_otaSrv) {
    g_otaSrv->end();
    delete g_otaSrv;
    g_otaSrv = nullptr;
  }
}

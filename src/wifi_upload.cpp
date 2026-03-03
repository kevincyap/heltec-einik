#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "wifi_upload.h"
#include <Fonts/FreeSans9pt7b.h>

static const char *AP_SSID = "EinkReader";
static const IPAddress AP_IP(192, 168, 4, 1);

static AsyncWebServer *_server = nullptr;
static DNSServer *_dns = nullptr;
static bool _active = false;
static DISPLAY_TYPE *_display_ptr = nullptr;

// Upload status shown on e-ink after each file
static char _last_upload[64] = {};
static size_t _last_upload_size = 0;

// ── Embedded HTML ──────────────────────────────────────────────────────────

static const char UPLOAD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>E-Ink Reader</title>
<style>
*{box-sizing:border-box;font-family:system-ui,sans-serif}
body{max-width:480px;margin:20px auto;padding:0 16px;background:#f5f5f5}
h2{margin:0 0 16px}
.card{background:#fff;border-radius:8px;padding:16px;margin-bottom:16px;box-shadow:0 1px 3px rgba(0,0,0,.12)}
input[type=file]{margin:8px 0}
button,input[type=submit]{background:#2563eb;color:#fff;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;font-size:14px}
button:hover,input[type=submit]:hover{background:#1d4ed8}
.del{background:#dc2626;padding:6px 12px;font-size:12px}
.del:hover{background:#b91c1c}
table{width:100%;border-collapse:collapse}
td{padding:6px 8px;border-bottom:1px solid #eee}
td:last-child{text-align:right;white-space:nowrap}
.bar{height:4px;background:#e5e7eb;border-radius:2px;margin:8px 0;display:none}
.bar>div{height:100%;background:#2563eb;border-radius:2px;width:0%;transition:width .2s}
#msg{margin:8px 0;font-weight:600;color:#16a34a}
.space{color:#666;font-size:13px}
</style>
</head><body>
<h2>&#128214; E-Ink Reader</h2>
<div class="card">
<b>Upload Book</b>
<form id="uf" enctype="multipart/form-data">
<br><input type="file" name="file" accept=".txt" required>
<br><input type="submit" value="Upload">
</form>
<div class="bar" id="bar"><div id="fill"></div></div>
<div id="msg"></div>
</div>
<div class="card">
<b>Books on Device</b> <span class="space" id="space"></span>
<table id="ft"><tr><td>Loading...</td></tr></table>
</div>
<script>
function esc(s){let d=document.createElement('div');d.textContent=s;return d.innerHTML}
function fmt(b){return b<1024?b+' B':(b/1024).toFixed(1)+' KB'}
function load(){
  fetch('/files').then(r=>r.json()).then(d=>{
    let h='';
    d.files.forEach(f=>{
      h+='<tr><td>'+esc(f.name)+'</td><td>'+fmt(f.size)+
      ' <button class="del" onclick="del(\''+esc(f.name)+'\')">Delete</button></td></tr>';
    });
    if(!d.files.length) h='<tr><td>No books yet</td></tr>';
    document.getElementById('ft').innerHTML=h;
    document.getElementById('space').textContent=fmt(d.free)+' free';
  });
}
function del(n){
  if(!confirm('Delete '+n+'?'))return;
  fetch('/files?name='+encodeURIComponent(n),{method:'DELETE'}).then(()=>load());
}
document.getElementById('uf').onsubmit=function(e){
  e.preventDefault();
  let f=new FormData(this);
  let x=new XMLHttpRequest();
  let bar=document.getElementById('bar');
  let fill=document.getElementById('fill');
  let msg=document.getElementById('msg');
  bar.style.display='block';fill.style.width='0%';msg.textContent='';
  x.upload.onprogress=function(e){if(e.lengthComputable)fill.style.width=(e.loaded/e.total*100)+'%';};
  x.onload=function(){msg.textContent=x.status==200?'Upload complete!':'Error: '+x.responseText;load();};
  x.onerror=function(){msg.textContent='Network error';};
  x.open('POST','/upload');x.send(f);
};
load();
</script>
</body></html>
)rawliteral";

// ── E-ink display helpers ──────────────────────────────────────────────────

static void draw_upload_screen(const char *status_line = nullptr) {
  if (!_display_ptr) return;
  DISPLAY_TYPE &display = *_display_ptr;

  display.clearMemory();
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(BLACK);
  display.setTextWrap(false);

  display.setCursor(MARGIN_X, 16);
  display.print("Upload Mode");

  display.setFont(NULL);
  display.setTextSize(1);

  display.setCursor(MARGIN_X, 32);
  display.print("WiFi: ");
  display.print(AP_SSID);

  display.setCursor(MARGIN_X, 46);
  display.print("Open: http://");
  display.print(AP_IP.toString());

  display.setCursor(MARGIN_X, 62);
  display.print("Connect phone/laptop to");

  display.setCursor(MARGIN_X, 74);
  display.print("the WiFi above, then open");

  display.setCursor(MARGIN_X, 86);
  display.print("the URL in a browser.");

  if (status_line) {
    display.setCursor(MARGIN_X, 102);
    display.print(status_line);
  }

  display.setCursor(MARGIN_X, display.height() - 4);
  display.print("[Press button to exit]");

  display.update();
}

// ── Server handlers ────────────────────────────────────────────────────────

static void handle_root(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", UPLOAD_HTML);
}

static void handle_captive(AsyncWebServerRequest *request) {
  request->redirect("http://" + AP_IP.toString());
}

static void handle_file_list(AsyncWebServerRequest *request) {
  String json = "{\"files\":[";
  File root = LittleFS.open("/");
  bool first = true;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String name = f.name();
        if (name.endsWith(".txt")) {
          if (!first) json += ",";
          // Ensure name has leading /
          if (name[0] != '/') name = "/" + name;
          json += "{\"name\":\"" + name + "\",\"size\":" + String(f.size()) + "}";
          first = false;
        }
      }
      f = root.openNextFile();
    }
    root.close();
  }
  json += "],\"free\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes()) + "}";
  request->send(200, "application/json", json);
}

static void handle_file_delete(AsyncWebServerRequest *request) {
  if (!request->hasParam("name")) {
    request->send(400, "text/plain", "Missing name parameter");
    return;
  }
  String name = request->getParam("name")->value();
  if (LittleFS.exists(name)) {
    LittleFS.remove(name);
    request->send(200, "text/plain", "Deleted");
  } else {
    request->send(404, "text/plain", "File not found");
  }
}

static void handle_upload_complete(AsyncWebServerRequest *request) {
  if (_last_upload[0]) {
    char status[80];
    snprintf(status, sizeof(status), "Uploaded: %s (%u B)", _last_upload, (unsigned)_last_upload_size);
    draw_upload_screen(status);
  }
  request->send(200, "text/plain", "OK");
}

static void handle_upload_data(AsyncWebServerRequest *request, String filename,
                               size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    // Normalize filename
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.printf("Upload start: %s\n", filename.c_str());
    request->_tempFile = LittleFS.open(filename, "w");
    strncpy(_last_upload, filename.c_str(), sizeof(_last_upload) - 1);
    _last_upload_size = 0;
  }

  if (request->_tempFile) {
    request->_tempFile.write(data, len);
    _last_upload_size += len;
  }

  if (final) {
    if (request->_tempFile) {
      request->_tempFile.close();
    }
    Serial.printf("Upload end: %s (%u bytes)\n", _last_upload, (unsigned)_last_upload_size);
  }
}

// ── Public API ─────────────────────────────────────────────────────────────

void wifi_upload_start(DISPLAY_TYPE &display) {
  if (_active) return;
  _display_ptr = &display;

  // Start WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);
  delay(100);
  Serial.printf("AP started: %s at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // DNS for captive portal
  _dns = new DNSServer();
  _dns->start(53, "*", AP_IP);

  // Web server
  _server = new AsyncWebServer(80);
  _server->on("/", HTTP_GET, handle_root);
  _server->on("/files", HTTP_GET, handle_file_list);
  _server->on("/files", HTTP_DELETE, handle_file_delete);
  _server->on("/upload", HTTP_POST, handle_upload_complete, handle_upload_data);

  // Captive portal redirects
  _server->on("/generate_204", HTTP_GET, handle_captive);     // Android
  _server->on("/hotspot-detect.html", HTTP_GET, handle_captive); // Apple
  _server->on("/connecttest.txt", HTTP_GET, handle_captive);  // Windows
  _server->onNotFound(handle_captive);

  _server->begin();
  _active = true;

  draw_upload_screen();
}

void wifi_upload_tick() {
  if (_dns) _dns->processNextRequest();
}

void wifi_upload_stop() {
  if (!_active) return;

  if (_server) {
    _server->end();
    delete _server;
    _server = nullptr;
  }
  if (_dns) {
    _dns->stop();
    delete _dns;
    _dns = nullptr;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _active = false;
  _display_ptr = nullptr;
  _last_upload[0] = '\0';

  Serial.println("Upload mode stopped");
}

bool wifi_upload_active() {
  return _active;
}

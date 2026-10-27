#include <Arduino.h>
#include "serial_output.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <string.h>
#include "config.h"
#include "wifi_secrets.h"
#include "wifi_upload.h"
#include "reader.h"
#include "state.h"
#include "book_files.h"
#include "book_storage.h"
#include <new>
#include <Fonts/FreeSans9pt7b.h>

static const char *AP_SSID = WIFI_UPLOAD_AP_SSID;
static const IPAddress AP_IP(192, 168, 4, 1);

static AsyncWebServer *_server = nullptr;
static DNSServer *_dns = nullptr;
static bool _active = false;
static bool _using_ap_mode = true;
static EinkDisplay *_display_ptr = nullptr;
static IPAddress _host_ip(0, 0, 0, 0);
static char _active_ssid[33] = {};

// Upload status shown on e-ink after each file
static portMUX_TYPE _status_lock = portMUX_INITIALIZER_UNLOCKED;
static char _pending_status[80] = {};

// ── Embedded HTML ──────────────────────────────────────────────────────────

static const char UPLOAD_HTML[] = R"rawliteral(
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
select{width:100%;padding:6px 8px;margin:4px 0 10px;border:1px solid #ddd;border-radius:4px;font-size:14px}
input[type=number]{padding:6px 8px;border:1px solid #ddd;border-radius:4px;font-size:14px}
.ok{color:#16a34a;font-weight:600;margin-top:8px}
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
<div class="card">
<b>Saved Bookmark</b>
<div id="spos" style="margin:8px 0;color:#555">Loading...</div>
<label>Book<select id="bsel"></select></label>
<label>Page <input type="number" id="pnum" min="0" value="0" style="width:90px"></label>
&nbsp;<button onclick="setPage()">Set Bookmark</button>
<div id="smsg" class="ok"></div>
</div>
<script>
function esc(s){let d=document.createElement('div');d.textContent=s;return d.innerHTML}
function fmt(b){return b<1024?b+' B':(b/1024).toFixed(1)+' KB'}
function load(){
  fetch('/files').then(r=>r.json()).then(d=>{
    let h='';
    let sel=document.getElementById('bsel');
    let prev=sel.value;
    sel.innerHTML='';
    d.files.forEach(f=>{
      h+='<tr><td>'+esc(f.name)+'</td><td>'+fmt(f.size)+
      ' <button class="del" data-name="'+encodeURIComponent(f.name)+'">Delete</button></td></tr>';
      let o=document.createElement('option');
      o.value=f.name;o.textContent=f.name.replace(/^\//,'');
      sel.appendChild(o);
    });
    if(!d.files.length) h='<tr><td>No books yet</td></tr>';
    if(prev) sel.value=prev;
    document.getElementById('ft').innerHTML=h;
    document.querySelectorAll('#ft button.del').forEach(b=>{
      b.onclick=()=>del(decodeURIComponent(b.dataset.name));
    });
    document.getElementById('space').textContent=fmt(d.free)+' free';
  });
}
function del(n){
  if(!confirm('Delete '+n+'?'))return;
  fetch('/files?name='+encodeURIComponent(n),{method:'DELETE'}).then(async r=>{
    if(!r.ok)throw new Error(await r.text());
    load();
  }).catch(e=>alert(e.message));
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
function loadState(){
  fetch('/state').then(r=>r.json()).then(d=>{
    let el=document.getElementById('spos');
    if(d.valid){
      el.textContent='Current: '+d.filename.replace(/^\//,'')+' \u2014 page '+d.page;
      let sel=document.getElementById('bsel');
      if(Array.from(sel.options).some(o=>o.value===d.filename)) sel.value=d.filename;
      document.getElementById('pnum').value=d.page;
    } else {
      el.textContent='No bookmark saved';
    }
  });
}
function setPage(){
  let n=document.getElementById('bsel').value;
  let p=document.getElementById('pnum').value;
  let msg=document.getElementById('smsg');
  msg.textContent='';
  if(!n){msg.textContent='No book selected';return;}
  let fd=new URLSearchParams();
  fd.append('filename',n);fd.append('page',p);
  fetch('/state',{method:'POST',body:fd}).then(r=>{
    if(r.ok){msg.textContent='Bookmark saved!';loadState();}
    else r.text().then(t=>{msg.textContent='Error: '+t;});
  });
}
load();loadState();
</script>
</body></html>
)rawliteral";

// ── E-ink display helpers ──────────────────────────────────────────────────

static void draw_upload_screen(const char *status_line = nullptr) {
  if (!_display_ptr) return;
  EinkDisplay &display = *_display_ptr;

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
  display.print(_active_ssid);

  display.setCursor(MARGIN_X, 46);
  display.print("Open: http://");
  display.print(_host_ip.toString());

  if (_using_ap_mode) {
    display.setCursor(MARGIN_X, 62);
    display.print("Connect phone/laptop to");

    display.setCursor(MARGIN_X, 74);
    display.print("the WiFi above, then open");

    display.setCursor(MARGIN_X, 86);
    display.print("the URL in a browser.");
  } else {
    display.setCursor(MARGIN_X, 62);
    display.print("Use phone/laptop on same");

    display.setCursor(MARGIN_X, 74);
    display.print("router WiFi, then open");

    display.setCursor(MARGIN_X, 86);
    display.print("the URL in a browser.");
  }

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
  request->send(200, "text/html", UPLOAD_HTML);
}

static void handle_captive(AsyncWebServerRequest *request) {
  request->redirect("http://" + _host_ip.toString());
}

static void append_json_string(String &json, const char *text) {
  json += '"';
  for (; *text; ++text) {
    unsigned char c = (unsigned char)*text;
    if (c == '"' || c == '\\') {
      json += '\\';
      json += *text;
    } else if (c < 32) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)c);
      json += escaped;
    } else {
      json += *text;
    }
  }
  json += '"';
}

static void handle_file_list(AsyncWebServerRequest *request) {
  String json = "{\"files\":[";
  json.reserve(1536);
  File root = LittleFS.open("/");
  bool first = true;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        const char *raw_name = f.name();
        if (has_txt_extension(raw_name)) {
          String name = raw_name;
          if (!first) json += ",";
          // Ensure name has leading /
          if (name[0] != '/') name = "/" + name;
          json += "{\"name\":";
          append_json_string(json, name.c_str());
          json += ",\"size\":" + String(f.size()) + "}";
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
  BookStorageResult result = book_storage_delete(name.c_str());
  request->send(book_storage_http_status(result), "text/plain", book_storage_message(result));
}

static void cleanup_upload(AsyncWebServerRequest *request) {
  BookUpload *upload = static_cast<BookUpload *>(request->_tempObject);
  // The request destructor frees _tempObject with free(), not delete.
  request->_tempObject = nullptr;
  delete upload;
}

static void handle_upload_complete(AsyncWebServerRequest *request) {
  if (request->getResponse()) return;
  BookUpload *upload = static_cast<BookUpload *>(request->_tempObject);
  BookStorageResult result = upload ? upload->commit() : BookStorageResult::IncompleteUpload;
  if (result == BookStorageResult::Ok) {
    char status[80] = {};
    snprintf(status, sizeof(status), "Uploaded: %s (%u B)", upload->path(), (unsigned)upload->size());
    portENTER_CRITICAL(&_status_lock);
    memcpy(_pending_status, status, sizeof(status));
    portEXIT_CRITICAL(&_status_lock);
  }
  cleanup_upload(request);
  request->send(book_storage_http_status(result), "text/plain", book_storage_message(result));
}

static void handle_upload_data(AsyncWebServerRequest *request, String filename,
                               size_t index, uint8_t *data, size_t len, bool final) {
  if (request->getResponse()) return;
  BookUpload *upload = static_cast<BookUpload *>(request->_tempObject);
  if (!upload) {
    upload = new (std::nothrow) BookUpload();
    if (!upload) {
      request->send(book_storage_http_status(BookStorageResult::MemoryFailed), "text/plain",
                    book_storage_message(BookStorageResult::MemoryFailed));
      return;
    }
    request->_tempObject = upload;
    request->onDisconnect([request]() { cleanup_upload(request); });
    upload->begin(filename.c_str());
  } else if (index == 0) {
    upload->reject();
  }
  upload->append(index, data, len, final);
}

static void handle_state_get(AsyncWebServerRequest *request) {
  ReadingState s = state_load();
  String json;
  if (s.valid) {
    json = "{\"valid\":true,\"filename\":";
    append_json_string(json, s.filename);
    json += ",\"page\":";
    json += String(s.page);
    json += "}";
  } else {
    json = "{\"valid\":false}";
  }
  request->send(200, "application/json", json);
}

static void handle_state_set(AsyncWebServerRequest *request) {
  if (!request->hasParam("filename", true) || !request->hasParam("page", true)) {
    request->send(400, "text/plain", "Missing params");
    return;
  }
  String filename = request->getParam("filename", true)->value();
  int page;
  if (!book_storage_page(request->getParam("page", true)->value().c_str(), page)) {
    request->send(400, "text/plain", book_storage_message(BookStorageResult::InvalidPage));
    return;
  }
  BookStorageResult result = book_storage_set_page(filename.c_str(), page);
  request->send(book_storage_http_status(result), "text/plain", book_storage_message(result));
}

static bool start_wifi_sta() {
  if (!WIFI_UPLOAD_USE_STA)
    return false;
  if (strlen(WIFI_UPLOAD_STA_SSID) == 0)
    return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_UPLOAD_STA_SSID, WIFI_UPLOAD_STA_PASS);

  unsigned long start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < WIFI_UPLOAD_STA_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED)
    return false;

  _using_ap_mode = false;
  _host_ip = WiFi.localIP();
  strncpy(_active_ssid, WIFI_UPLOAD_STA_SSID, sizeof(_active_ssid) - 1);
  _active_ssid[sizeof(_active_ssid) - 1] = '\0';

  debug_serial.printf("WiFi STA connected: %s at %s\n", _active_ssid, _host_ip.toString().c_str());
  return true;
}

static void start_wifi_ap() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);
  delay(100);

  _using_ap_mode = true;
  _host_ip = WiFi.softAPIP();
  strncpy(_active_ssid, AP_SSID, sizeof(_active_ssid) - 1);
  _active_ssid[sizeof(_active_ssid) - 1] = '\0';

  debug_serial.printf("WiFi AP started: %s at %s\n", AP_SSID, _host_ip.toString().c_str());
}

// ── Public API ─────────────────────────────────────────────────────────────

void wifi_upload_start(EinkDisplay &display) {
  if (_active) return;
  _display_ptr = &display;

  if (!start_wifi_sta()) {
    start_wifi_ap();

    // DNS for captive portal (AP mode only)
    _dns = new DNSServer();
    _dns->start(53, "*", _host_ip);
  }

  WiFi.setSleep(true);

  // Web server
  _server = new AsyncWebServer(80);
  _server->on("/", HTTP_GET, handle_root);
  _server->on("/files", HTTP_GET, handle_file_list);
  _server->on("/files", HTTP_DELETE, handle_file_delete);
  _server->on("/upload", HTTP_POST, handle_upload_complete, handle_upload_data);
  _server->on("/state", HTTP_GET, handle_state_get);
  _server->on("/state", HTTP_POST, handle_state_set);

  if (_using_ap_mode) {
    // Captive portal redirects
    _server->on("/generate_204", HTTP_GET, handle_captive);     // Android
    _server->on("/hotspot-detect.html", HTTP_GET, handle_captive); // Apple
    _server->on("/connecttest.txt", HTTP_GET, handle_captive);  // Windows
    _server->onNotFound(handle_captive);
  } else {
    _server->onNotFound([](AsyncWebServerRequest *request) {
      request->send(404, "text/plain", "Not found");
    });
  }

  _server->begin();
  _active = true;

  draw_upload_screen();
}

void wifi_upload_tick() {
  if (_dns) _dns->processNextRequest();
  char status[sizeof(_pending_status)];
  portENTER_CRITICAL(&_status_lock);
  memcpy(status, _pending_status, sizeof(status));
  _pending_status[0] = '\0';
  portEXIT_CRITICAL(&_status_lock);
  if (status[0]) draw_upload_screen(status);
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

  if (_using_ap_mode)
    WiFi.softAPdisconnect(true);
  else
    WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  _active = false;
  _display_ptr = nullptr;
  portENTER_CRITICAL(&_status_lock);
  _pending_status[0] = '\0';
  portEXIT_CRITICAL(&_status_lock);
  _active_ssid[0] = '\0';

  debug_serial.println("Upload mode stopped");
}

bool wifi_upload_active() {
  return _active;
}

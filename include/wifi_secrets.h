#pragma once

// WiFi upload settings
//
// Optional local override:
//   1) Copy this file to `wifi_secrets.local.h`
//   2) Put your real credentials there
//   3) `wifi_secrets.local.h` is gitignored
#if defined(__has_include)
  #if __has_include("wifi_secrets.local.h")
    #include "wifi_secrets.local.h"
  #endif
#endif

// 0 = start local AP
// 1 = connect to router (STA), fallback to AP if connect fails
#ifndef WIFI_UPLOAD_USE_STA
#define WIFI_UPLOAD_USE_STA 0
#endif

#ifndef WIFI_UPLOAD_STA_SSID
#define WIFI_UPLOAD_STA_SSID ""
#endif

#ifndef WIFI_UPLOAD_STA_PASS
#define WIFI_UPLOAD_STA_PASS ""
#endif

#ifndef WIFI_UPLOAD_STA_TIMEOUT_MS
#define WIFI_UPLOAD_STA_TIMEOUT_MS 15000
#endif

#ifndef WIFI_UPLOAD_AP_SSID
#define WIFI_UPLOAD_AP_SSID "EinkReader"
#endif

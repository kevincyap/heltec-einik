#pragma once

#include "config.h"
#include <heltec-eink-modules.h>

// Start WiFi AP and web server for file uploads.
// Displays upload mode screen on the e-ink display.
void wifi_upload_start(DISPLAY_TYPE &display);

// Must be called periodically while upload mode is active (processes DNS).
void wifi_upload_tick();

// Stop WiFi AP and web server, free resources.
void wifi_upload_stop();

// Returns true if upload mode is currently active.
bool wifi_upload_active();

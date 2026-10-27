#pragma once

#include <stdint.h>

// Main application task only; never call from an ISR or async HTTP callback.
bool diagnostics_begin(uint32_t boot_id, int reset_reason, int wake_cause);
bool diagnostics_log(const char *format, ...);
bool diagnostics_log_at(unsigned long event_ms, const char *format, ...);
bool diagnostics_flush();
void diagnostics_poll();
bool diagnostics_export_active();
[[noreturn]] void diagnostics_display_timeout(uint8_t pin, unsigned long elapsed);

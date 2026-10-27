#pragma once

#include <stddef.h>

// Main task only. begin takes ownership of the serial export lock.
void diagnostics_export_begin(const char *previous, const char *current,
                              unsigned long started_at,
                              const char *error = nullptr);
void diagnostics_export_poll();
bool diagnostics_export_active();
void diagnostics_export_cancel(const char *reason);
// Consumes the terminal result; nullptr means no new result.
const char *diagnostics_export_result(size_t &bytes);

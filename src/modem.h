#pragma once
#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Early init: PMU rails for modem + modem UART + AT readiness.
// Returns true if modem is AT-ready.
bool modem_init_early();

// Get modem timestamp into out buffer.
// Returns true only if modem returned a "sane" network-synced time.
// If false, out will contain an uptime-based fallback like "UPT00012345".
bool modem_get_timestamp(char *out, size_t out_len);

// Optional: returns true if modem AT is responding right now.
bool modem_test_at(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

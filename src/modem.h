#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Returns true if PMU + modem AT are ready (or at least AT works).
bool modem_init_early();

// Tries to obtain a *plausible* modem timestamp via AT+CCLK?
// Output format: "YYYYMMDD_HHMMSS"
// Returns true if a plausible network time was obtained, false if fallback was used.
bool modem_get_timestamp(char *out, size_t out_len);

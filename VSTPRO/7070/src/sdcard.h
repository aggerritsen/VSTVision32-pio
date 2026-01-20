// src/sdcard.h (RFC) — API compatible with existing main.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Init SD (board-specific backend). Returns true if mounted.
bool sdcard_init();

// Existing code expects this name:
bool sdcard_available();

// Existing code expects this signature/order:
bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *jpg, size_t jpg_len);

// You already reference this from main.cpp (linker error earlier):
void sdcard_set_time_valid(bool valid);

// Optional helpers (safe to keep; not required by main.cpp)
void sdcard_set_time_prefix(const char *ts);
void sdcard_print_stats();

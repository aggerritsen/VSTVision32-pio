// src/sdcard.h (RFC)
#pragma once
#include <stddef.h>
#include <stdint.h>

bool sdcard_init();
bool sdcard_available();

// Set by main after modem time is applied
void sdcard_set_time_valid(bool valid);

// Save JPEG (frame_id used for suffix)
bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *data, size_t len);

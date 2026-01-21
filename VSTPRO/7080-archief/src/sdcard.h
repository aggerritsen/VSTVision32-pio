// sdcard.h (RFC)
#pragma once
#include <Arduino.h>

bool sdcard_init();
bool sdcard_available();

// Tell SD layer whether system time is valid (set after modem timestamp applied).
void sdcard_set_time_valid(bool valid);

bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *data, size_t len);

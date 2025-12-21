#pragma once
#include <Arduino.h>

bool     sdcard_init();
bool     sdcard_ready();
uint64_t sdcard_total_bytes();
uint64_t sdcard_used_bytes();

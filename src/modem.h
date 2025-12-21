#pragma once

#include <Arduino.h>
#include <time.h>

bool modemInit();
bool modemGetTimestamp(struct tm &out_tm);
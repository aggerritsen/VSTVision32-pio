//visionai.h
#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace VisionAI {

struct LoopResult
{
    bool ok = false;

    uint32_t frame_id = 0;

    // detections summary (targets only, to let main pulse actuators)
    size_t box_count = 0;
    uint8_t targets[16] = {0}; // cap to avoid dynamic alloc

    // JPEG buffer (malloc'd). Caller must free().
    uint8_t *jpeg = nullptr;
    size_t jpeg_len = 0;
};

// Init SSCMA on Wire1 (TwoWire(1)) to avoid conflict with PMU/Wire.
// Returns true on success.
bool begin();

// Runs one inference cycle and returns results.
// If ok==false, caller may call reinit().
LoopResult loop_once();

// Reinitialize SSCMA on Wire1 only.
bool reinit();

} // namespace VisionAI

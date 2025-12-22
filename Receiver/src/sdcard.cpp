//sdcard.cpp

#include "sdcard.h"
#include <SD_MMC.h>

/* =============================
   SD PIN CONFIG (T-SIM7080G-S3)
   ============================= */
#define SD_CMD  39
#define SD_CLK  38
#define SD_DATA 40

static bool sd_ok = false;

/* =============================
   INIT
   ============================= */
bool sdcard_init()
{
    Serial.println("📀 Initializing SD card (SD_MMC, custom pins)...");

    SD_MMC.setPins(SD_CLK, SD_CMD, SD_DATA);

    if (!SD_MMC.begin("/sdcard", true))
    {
        Serial.println("❌ SD_MMC mount failed");
        sd_ok = false;
        return false;
    }

    uint64_t size = SD_MMC.cardSize();
    uint64_t used = SD_MMC.usedBytes();

    Serial.printf("✅ SD card mounted\n");
    Serial.printf("📦 SD size : %llu MB\n", size / (1024 * 1024));
    Serial.printf("📊 SD usage: %llu / %llu bytes\n", used, size);

    sd_ok = true;
    return true;
}

bool sdcard_available()
{
    return sd_ok;
}

/* =============================
   SAVE JPEG
   ============================= */
bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *data, size_t len)
{
    if (!sd_ok)
        return false;

    char path[32];
    snprintf(path, sizeof(path), "/frame_%06lu.jpg", frame_id);

    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.printf("❌ Failed to open %s\n", path);
        return false;
    }

    size_t written = f.write(data, len);
    f.close();

    if (written != len)
    {
        Serial.printf("❌ SD write incomplete (%u / %u)\n", written, len);
        return false;
    }

    Serial.printf("💾 JPEG saved: %s (%u bytes)\n", path, len);
    return true;
}

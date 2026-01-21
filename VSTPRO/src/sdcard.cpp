// src/sdcard.cpp (RFC) — dual-board SD support
//  - 7070: SD over SPI (SD library)
//  - 7080: SD over SD_MMC (SD_MMC library)
//  - Filenames can use SYSTEM TIME once modem time is set

#include "sdcard.h"
#include "config.h"
#include <cstdio>
#include <time.h>
#include <Arduino.h>

static bool sd_ok = false;
static bool time_valid = false;

void sdcard_set_time_valid(bool valid)
{
    time_valid = valid;
    Serial.printf("🕒 SD time_valid=%s\n", time_valid ? "true" : "false");
}

static bool format_now_timestamp(char *out, size_t out_sz)
{
    if (!out || out_sz < 16) return false; // "YYYYMMDD_HHMMSS" + null

    time_t now = time(nullptr);
    if (now <= 0) return false;

    struct tm tm{};
    localtime_r(&now, &tm);

    int year = tm.tm_year + 1900;
    if (year < 2020) return false;

    snprintf(out, out_sz, "%04d%02d%02d_%02d%02d%02d",
             year,
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec);

    return true;
}

#if defined(VST_BOARD_7070)

// -----------------------------
// 7070: SD over SPI (SD library)
// -----------------------------
#include <SPI.h>
#include <SD.h>

static SPIClass g_sd_spi(VSPI);

bool sdcard_init()
{
    Serial.println("📀 Initializing SD card (SPI, custom pins)...");

    // config.h must define:
    // SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_SCK, SD_SPI_CS
    g_sd_spi.begin(SD_SPI_SCLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);

    if (!SD.begin(SD_SPI_CS, g_sd_spi))
    {
        Serial.println("❌ SD (SPI) mount failed");
        sd_ok = false;
        return false;
    }

    uint64_t size = SD.cardSize();
    Serial.printf("✅ SD card mounted (SPI)\n");
    Serial.printf("📦 SD size : %llu MB\n", size / (1024 * 1024));

    sd_ok = true;
    return true;
}

bool sdcard_available()
{
    return sd_ok;
}

bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *data, size_t len)
{
    if (!sd_ok) return false;

    char ts[20] = {0};
    char path[96];

    if (time_valid && format_now_timestamp(ts, sizeof(ts)))
        snprintf(path, sizeof(path), "/%s_frame_%06lu.jpg", ts, (unsigned long)frame_id);
    else
        snprintf(path, sizeof(path), "/frame_%06lu.jpg", (unsigned long)frame_id);

    File f = SD.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.printf("❌ Failed to open %s\n", path);
        return false;
    }

    size_t written = f.write(data, len);
    f.close();

    if (written != len)
    {
        Serial.printf("❌ SD write incomplete (%u / %u)\n", (unsigned)written, (unsigned)len);
        return false;
    }

    Serial.printf("💾 JPEG saved: %s (%u bytes)\n", path, (unsigned)len);
    return true;
}

#elif defined(VST_BOARD_7080)

// -----------------------------
// 7080: SD over SD_MMC
// -----------------------------
#include <SD_MMC.h>

bool sdcard_init()
{
    Serial.println("📀 Initializing SD card (SD_MMC, custom pins)...");

    // config.h must define: SD_CLK, SD_CMD, SD_DATA
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

bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *data, size_t len)
{
    if (!sd_ok) return false;

    char ts[20] = {0};
    char path[96];

    if (time_valid && format_now_timestamp(ts, sizeof(ts)))
        snprintf(path, sizeof(path), "/%s_frame_%06lu.jpg", ts, (unsigned long)frame_id);
    else
        snprintf(path, sizeof(path), "/frame_%06lu.jpg", (unsigned long)frame_id);

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
        Serial.printf("❌ SD write incomplete (%u / %u)\n", (unsigned)written, (unsigned)len);
        return false;
    }

    Serial.printf("💾 JPEG saved: %s (%u bytes)\n", path, (unsigned)len);
    return true;
}

#else
#error "Define VST_BOARD_7070 or VST_BOARD_7080"
#endif

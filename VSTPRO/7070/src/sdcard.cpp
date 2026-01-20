// src/sdcard.cpp (RFC)
//  - VST_BOARD_7070: SPI SD (SD.h) using pins from config.h: SD_SPI_MISO/MOSI/SCLK/CS
//  - VST_BOARD_7080: SD_MMC (optional path), pins from config.h: SD_CMD/SD_CLK/SD_DATA
//
// Keeps existing API used by main.cpp:
//   - sdcard_available()
//   - sdcard_save_jpeg(frame_id, jpg, jpg_len)
//   - sdcard_set_time_valid(bool)

#include "sdcard.h"
#include "config.h"

#include <Arduino.h>

static bool g_sd_ok = false;
static bool g_time_valid = false;
static char g_time_prefix[32] = {0};

// ----------------------------
// 7070: SPI SD
// ----------------------------
#if defined(VST_BOARD_7070)

#include <SPI.h>
#include <SD.h>

static SPIClass g_sd_spi(VSPI);

static bool sd_mount_spi()
{
    Serial.println("📀 Initializing SD card (SPI, custom pins)...");
    // Pins must come from config.h:
    //   SD_SPI_SCLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS
    g_sd_spi.begin(SD_SPI_SCLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);

    if (!SD.begin(SD_SPI_CS, g_sd_spi, 4000000))
    {
        Serial.println("❌ SD SPI mount failed");
        return false;
    }

    Serial.println("✅ SD card mounted (SPI)");
    return true;
}

static bool write_file_spi(const char *path, const uint8_t *data, size_t len)
{
    File f = SD.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.printf("❌ SD.open failed: %s\n", path);
        return false;
    }

    size_t w = f.write(data, len);
    f.close();

    if (w != len)
    {
        Serial.printf("❌ SD write short: %s (%u/%u)\n", path, (unsigned)w, (unsigned)len);
        return false;
    }
    return true;
}

static void sd_stats_spi()
{
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("⚠️ No SD card attached");
        return;
    }

    uint64_t cardSize = SD.cardSize();
    Serial.printf("📦 SD size : %llu MB\n", (unsigned long long)(cardSize / (1024ULL * 1024ULL)));
}

#endif // VST_BOARD_7070

// ----------------------------
// 7080: SD_MMC (kept for later; won’t affect 7070 build)
// ----------------------------
#if defined(VST_BOARD_7080)

#include <FS.h>
#include <SD_MMC.h>

static bool sd_mount_sdmmc()
{
    Serial.println("📀 Initializing SD card (SD_MMC, custom pins)...");

#if defined(ESP_ARDUINO_VERSION)
    // If available in your core:
    SD_MMC.setPins(SD_CMD, SD_CLK, SD_DATA);
#endif

    bool ok = SD_MMC.begin("/sdcard", true /*1-bit*/, false /*format*/, 20000000 /*freq*/);
    if (!ok)
    {
        Serial.println("❌ SD_MMC mount failed");
        return false;
    }

    Serial.println("✅ SD card mounted (SD_MMC)");
    return true;
}

static bool write_file_sdmmc(const char *path, const uint8_t *data, size_t len)
{
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.printf("❌ SD_MMC.open failed: %s\n", path);
        return false;
    }

    size_t w = f.write(data, len);
    f.close();

    if (w != len)
    {
        Serial.printf("❌ SD_MMC write short: %s (%u/%u)\n", path, (unsigned)w, (unsigned)len);
        return false;
    }
    return true;
}

static void sd_stats_sdmmc()
{
    uint64_t cardSize = SD_MMC.cardSize();
    Serial.printf("📦 SD size : %llu MB\n", (unsigned long long)(cardSize / (1024ULL * 1024ULL)));
}

#endif // VST_BOARD_7080

// ----------------------------
// Common helpers
// ----------------------------
void sdcard_set_time_valid(bool valid)
{
    g_time_valid = valid;
}

void sdcard_set_time_prefix(const char *ts)
{
    if (!ts || !*ts)
    {
        g_time_prefix[0] = 0;
        return;
    }
    strncpy(g_time_prefix, ts, sizeof(g_time_prefix) - 1);
    g_time_prefix[sizeof(g_time_prefix) - 1] = 0;
}

static void build_frame_path(char *out, size_t out_len, uint32_t frame_id)
{
    // Keep it simple and stable. If time prefix is set and valid, use it.
    if (g_time_valid && g_time_prefix[0])
    {
        snprintf(out, out_len, "/%s_frame_%06lu.jpg",
                 g_time_prefix, (unsigned long)frame_id);
    }
    else
    {
        // fallback
        snprintf(out, out_len, "/frame_%06lu_ms_%08lu.jpg",
                 (unsigned long)frame_id, (unsigned long)millis());
    }
}

// ----------------------------
// Public API (matches main.cpp)
// ----------------------------
bool sdcard_init()
{
    g_sd_ok = false;

#if defined(VST_BOARD_7070)
    g_sd_ok = sd_mount_spi();
    if (g_sd_ok) sd_stats_spi();
#elif defined(VST_BOARD_7080)
    g_sd_ok = sd_mount_sdmmc();
    if (g_sd_ok) sd_stats_sdmmc();
#else
#error "Define VST_BOARD_7070 or VST_BOARD_7080"
#endif

    return g_sd_ok;
}

bool sdcard_available()
{
    return g_sd_ok;
}

bool sdcard_save_jpeg(uint32_t frame_id, const uint8_t *jpg, size_t jpg_len)
{
    if (!g_sd_ok || !jpg || jpg_len == 0) return false;

    char path[96];
    build_frame_path(path, sizeof(path), frame_id);

    bool ok = false;

#if defined(VST_BOARD_7070)
    ok = write_file_spi(path, jpg, jpg_len);
#elif defined(VST_BOARD_7080)
    ok = write_file_sdmmc(path, jpg, jpg_len);
#endif

    if (ok)
    {
        Serial.printf("💾 JPEG saved: %s (%u bytes)\n", path, (unsigned)jpg_len);
    }

    return ok;
}

void sdcard_print_stats()
{
    if (!g_sd_ok)
    {
        Serial.println("⚠️ SD not mounted");
        return;
    }

#if defined(VST_BOARD_7070)
    sd_stats_spi();
#elif defined(VST_BOARD_7080)
    sd_stats_sdmmc();
#endif
}

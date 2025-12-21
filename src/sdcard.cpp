#include "sdcard.h"
#include "SD_MMC.h"

/*
 * T-SIM7080G-S3 SD card wiring (SDMMC 1-bit)
 * CLK = GPIO38
 * CMD = GPIO39
 * D0  = GPIO40
 */

static bool sd_ok = false;

bool sdcard_init()
{
    if (sd_ok)
        return true;

    Serial.println("📀 Initializing SD card (SD_MMC, custom pins)...");

    // Explicit SDMMC pin mapping for T-SIM7080G-S3
    SD_MMC.setPins(
        GPIO_NUM_38,  // CLK
        GPIO_NUM_39,  // CMD
        GPIO_NUM_40   // D0
    );

    // true = 1-bit mode (required for this board)
    if (!SD_MMC.begin("/sdcard", true))
    {
        Serial.println("❌ SD_MMC mount failed");
        sd_ok = false;
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("❌ No SD card detected");
        sd_ok = false;
        return false;
    }

    Serial.print("✅ SD card mounted: ");
    switch (cardType)
    {
        case CARD_MMC:  Serial.println("MMC"); break;
        case CARD_SD:   Serial.println("SDSC"); break;
        case CARD_SDHC: Serial.println("SDHC/SDXC"); break;
        default:        Serial.println("UNKNOWN"); break;
    }

    Serial.printf(
        "📦 SD size: %llu MB\n",
        SD_MMC.cardSize() / (1024ULL * 1024ULL)
    );

    sd_ok = true;
    return true;
}

bool sdcard_ready()
{
    return sd_ok;
}

uint64_t sdcard_total_bytes()
{
    return sd_ok ? SD_MMC.totalBytes() : 0;
}

uint64_t sdcard_used_bytes()
{
    return sd_ok ? SD_MMC.usedBytes() : 0;
}

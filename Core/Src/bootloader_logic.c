/**
  ******************************************************************************
  * @file    bootloader_logic.c
  * @brief   Second-stage bootloader: sector search, SHA256 verification, jump.
  ******************************************************************************
  */

#include "bootloader_logic.h"
#include "app_metadata.h"
#include "flash_ops.h"
#include "bootloader_download.h"
#include "sha256.h"
#include "main.h"
#include <string.h>

/* Metadata magic and expected values */
static const uint8_t MAGIC[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
static const uint8_t INV_MAGIC[8] = { 0x21, 0x52, 0x41, 0x10, 0x35, 0x01, 0x45, 0x42 };
static const uint8_t VALIDATION_DOWNLOAD[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t VALIDATION_READY[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t INVALIDATION[8] = { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };

/* Check if metadata at ptr has matching magic numbers. */
static bool check_magic(const uint8_t *ptr)
{
    return memcmp(ptr + APP_METADATA_OFFSET_MAGIC, MAGIC, 8) == 0 &&
           memcmp(ptr + APP_METADATA_OFFSET_INVERTED_MAGIC, INV_MAGIC, 8) == 0;
}

/* Get size from metadata (LE). */
static uint32_t get_metadata_size(const uint8_t *meta)
{
    return meta[APP_METADATA_OFFSET_SIZE] |
           (meta[APP_METADATA_OFFSET_SIZE + 1] << 8) |
           (meta[APP_METADATA_OFFSET_SIZE + 2] << 16) |
           (meta[APP_METADATA_OFFSET_SIZE + 3] << 24);
}

/* Verify validation array is all 0xFF (download state). */
static bool is_validation_download(const uint8_t *meta)
{
    return memcmp(meta + APP_METADATA_OFFSET_VALIDATION, VALIDATION_DOWNLOAD, 8) == 0;
}

/* Verify validation and invalidation for ready state. */
static bool is_validation_ready(const uint8_t *meta)
{
    return memcmp(meta + APP_METADATA_OFFSET_VALIDATION, VALIDATION_READY, 8) == 0 &&
           memcmp(meta + APP_METADATA_OFFSET_INVALIDATION, INVALIDATION, 8) == 0;
}

/* Verify SHA256 for sector 6 download state: hash from sector start up to validation,
   then substitute validation (0xFF*8) and invalidation (0x00*4,0xFF*4), exclude digest. */
static bool verify_sha256_sector6_download(uint32_t sector_addr, uint32_t size, const uint8_t *stored_digest)
{
    uint8_t computed[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;
    uint32_t bytes_before_validation = size - APP_METADATA_SIZE + APP_METADATA_OFFSET_VALIDATION;

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, (const uint8_t *)sector_addr, bytes_before_validation);
    SHA256_Update(&ctx, VALIDATION_DOWNLOAD, 8);
    SHA256_Update(&ctx, INVALIDATION, 8);
    SHA256_Final(&ctx, computed);

    return memcmp(computed, stored_digest, SHA256_DIGEST_SIZE) == 0;
}

/* Verify SHA256 for sector 7 ready state: hash from sector start to end, excluding digest. */
static bool verify_sha256_sector7_ready(uint32_t sector_addr, uint32_t size, const uint8_t *stored_digest)
{
    uint8_t computed[SHA256_DIGEST_SIZE];
    uint32_t bytes_to_hash = size - SHA256_DIGEST_SIZE;

    SHA256_Calculate((const uint8_t *)sector_addr, bytes_to_hash, computed);
    return memcmp(computed, stored_digest, SHA256_DIGEST_SIZE) == 0;
}

/* Search sector for metadata at 8-byte boundaries. Returns pointer to metadata or NULL. */
static const uint8_t *search_sector_metadata(uint32_t sector_addr, uint32_t sector_size)
{
    uint32_t addr;
    const uint8_t *ptr;

    for (addr = sector_addr; addr + APP_METADATA_SIZE <= sector_addr + sector_size; addr += 8) {
        ptr = (const uint8_t *)addr;
        if (check_magic(ptr))
            return ptr;
    }
    return NULL;
}

/* Jump to application: set MSP, disable interrupts, jump to reset handler. */
static void jump_to_application(uint32_t app_addr)
{
    uint32_t msp = *(volatile uint32_t *)app_addr;
    uint32_t reset_handler = *(volatile uint32_t *)(app_addr + 4);

    __disable_irq();
    __set_MSP(msp);
    ((void (*)(void))reset_handler)();
}

/* Only start the BLE download sequence once per boot; main loop then drives Bootloader_Download_Process(). */
static bool download_started;

void Bootloader_Run(void)
{
    const uint8_t *meta;
    uint32_t size;
    const uint8_t *digest_ptr;
    uint32_t sector6_addr = FLASH_SECTOR_6_ADDRESS;
    uint32_t sector7_addr = FLASH_SECTOR_7_ADDRESS;
    uint32_t sector_size = FLASH_SECTOR_SIZE_6_7;

    /* 1. Search sector 6 for app in download state (validation all 0xFF) */
    meta = search_sector_metadata(sector6_addr, sector_size);
    if (meta != NULL) {
        size = get_metadata_size(meta);
        if (size >= APP_METADATA_SIZE && size <= sector_size &&
            (uint32_t)meta == sector6_addr + size - APP_METADATA_SIZE) {
            if (is_validation_download(meta)) {
                digest_ptr = meta + APP_METADATA_OFFSET_SHA256;
                if (verify_sha256_sector6_download(sector6_addr, size, digest_ptr)) {
                    NVIC_SystemReset();
                    return;
                }
            }
        }
    }

    /* 2. Search sector 7 for app in ready state */
    meta = search_sector_metadata(sector7_addr, sector_size);
    if (meta != NULL) {
        size = get_metadata_size(meta);
        if (size >= APP_METADATA_SIZE && size <= sector_size &&
            (uint32_t)meta == sector7_addr + size - APP_METADATA_SIZE) {
            if (is_validation_ready(meta)) {
                digest_ptr = meta + APP_METADATA_OFFSET_SHA256;
                if (verify_sha256_sector7_ready(sector7_addr, size, digest_ptr)) {
                    uint32_t app_addr = sector7_addr;
                    jump_to_application(app_addr);
                    return;
                }
            }
        }
    }

    /* 3. No valid app found; start BLE download once, then rely on main loop calling Bootloader_Download_Process() */
    if (download_started) {
        return;
    }
    download_started = true;
    Bootloader_StartDownload();
}

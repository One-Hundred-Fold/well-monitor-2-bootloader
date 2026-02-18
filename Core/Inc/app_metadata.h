/**
  ******************************************************************************
  * @file    app_metadata.h
  * @brief   Application binary metadata for second-stage bootloader.
  *          Metadata is placed at the end of the flash image (sector 4),
  *          STM32F401RETX_FLASH.ld reserves .app_metadata and symbols
  *          _app_metadata_start, _app_metadata_end, _flash_image_end.
  ******************************************************************************
  */

#ifndef APP_METADATA_H
#define APP_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Extern symbols from linker script (addresses in FLASH) */
extern const unsigned char _app_metadata_start[];
extern const unsigned char _app_metadata_end[];
extern const unsigned char _flash_image_end[];

/* Layout: magic[8], inverted_magic[8], name[8], version[8],
   dest_address[4], size[4] (both LE), validation[8], invalidation[8], sha256[32] = 88 bytes */
#define APP_METADATA_OFFSET_MAGIC           0
#define APP_METADATA_OFFSET_INVERTED_MAGIC  8
#define APP_METADATA_OFFSET_NAME            16
#define APP_METADATA_OFFSET_VERSION         24
#define APP_METADATA_OFFSET_DEST_ADDRESS    32
#define APP_METADATA_OFFSET_SIZE            36
#define APP_METADATA_OFFSET_VALIDATION      40
#define APP_METADATA_OFFSET_INVALIDATION    48
#define APP_METADATA_OFFSET_SHA256          56
#define APP_METADATA_SIZE                   88

/* Magic numbers for metadata identification */
#define APP_METADATA_MAGIC          { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE }
#define APP_METADATA_INVERTED_MAGIC { 0x21, 0x52, 0x41, 0x10, 0x35, 0x01, 0x45, 0x42 }

/* Validation/invalidation arrays */
#define APP_METADATA_VALIDATION_DOWNLOAD    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#define APP_METADATA_VALIDATION_READY       { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 }
#define APP_METADATA_INVALIDATION           { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF }

#ifdef __cplusplus
}
#endif

#endif /* APP_METADATA_H */

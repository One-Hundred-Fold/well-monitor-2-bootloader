/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    flash_ops.h
  * @brief   Flash operations for firmware storage
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FLASH_OPS_H
#define __FLASH_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported constants --------------------------------------------------------*/
#define FLASH_SECTOR_DOWNLOAD        6
#define FLASH_SECTOR_CURRENT         7
#define FLASH_SECTOR_SIZE_6_7        0x20000  // 128KB
#define FLASH_SECTOR_6_ADDRESS       0x08040000  // Download sector (128KB)
#define FLASH_SECTOR_7_ADDRESS       0x08060000  // Current version sector (128KB)

/* Exported types ------------------------------------------------------------*/
typedef struct {
    char version[15];  // YYYYMMDDHHMMSS + null terminator
    bool valid;
} firmware_version_t;

/* Exported functions prototypes ---------------------------------------------*/
bool Flash_EraseSector(uint32_t sector);
bool Flash_WriteData(uint32_t address, const uint8_t* data, uint32_t length);
bool Flash_ReadData(uint32_t address, uint8_t* data, uint32_t length);
bool Flash_GetCurrentVersion(firmware_version_t* version);
bool Flash_CheckSpaceAvailable(uint32_t required_size);
bool Flash_ProgramFirmwareData(uint32_t offset, const uint8_t* data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_OPS_H */

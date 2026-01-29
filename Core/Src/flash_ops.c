/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    flash_ops.c
  * @brief   Flash operations for firmware storage
  ******************************************************************************
  */
/* USER CODE END Header */

#include "flash_ops.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include <string.h>

/* Flash sector addresses for STM32F401RE (512KB) */
/* Sectors 0-3: 16KB each, Sector 4: 64KB, Sectors 5-7: 128KB each */
/* Total: 4*16KB + 64KB + 3*128KB = 64KB + 64KB + 384KB = 512KB */
#define FLASH_SECTOR_0_ADDRESS       0x08000000
#define FLASH_SECTOR_1_ADDRESS       0x08004000
#define FLASH_SECTOR_2_ADDRESS       0x08008000
#define FLASH_SECTOR_3_ADDRESS       0x0800C000
#define FLASH_SECTOR_4_ADDRESS       0x08010000
#define FLASH_SECTOR_5_ADDRESS       0x08020000
/* FLASH_SECTOR_6_ADDRESS and FLASH_SECTOR_7_ADDRESS are defined in flash_ops.h */

/* Version storage offset in sector 7 (first 16 bytes reserved for version timestamp) */
#define VERSION_OFFSET_IN_SECTOR7    0
#define VERSION_MAGIC                0x56455253  // "VERS" in ASCII

typedef struct {
    uint32_t magic;
    char version[14];  // YYYYMMDDHHMMSS
    uint8_t reserved[2];
} version_header_t;

bool Flash_EraseSector(uint32_t sector)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    
    if (sector > 7) return false;
    
    // Unlock Flash
    HAL_FLASH_Unlock();
    
    // Configure erase
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = sector;
    EraseInitStruct.NbSectors = 1;
    
    // Erase sector
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }
    
    HAL_FLASH_Lock();
    return true;
}

bool Flash_WriteData(uint32_t address, const uint8_t* data, uint32_t length)
{
    uint32_t i;
    uint32_t write_address = address;
    const uint32_t* data_ptr = (const uint32_t*)data;
    uint32_t words = length / 4;
    uint32_t remainder = length % 4;
    
    // Address must be 4-byte aligned
    if (address % 4 != 0) return false;
    
    HAL_FLASH_Unlock();
    
    // Write 32-bit words
    for (i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_address, data_ptr[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        write_address += 4;
    }
    
    // Write remaining bytes if any
    if (remainder > 0) {
        uint32_t last_word = 0;
        uint8_t* last_word_ptr = (uint8_t*)&last_word;
        const uint8_t* remainder_data = data + (words * 4);
        
        // Read existing word
        last_word = *((uint32_t*)write_address);
        
        // Copy remainder bytes
        for (i = 0; i < remainder; i++) {
            last_word_ptr[i] = remainder_data[i];
        }
        
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_address, last_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }
    
    HAL_FLASH_Lock();
    return true;
}

bool Flash_ReadData(uint32_t address, uint8_t* data, uint32_t length)
{
    memcpy(data, (const void*)address, length);
    return true;
}

bool Flash_GetCurrentVersion(firmware_version_t* version)
{
    version_header_t header;
    
    if (version == NULL) return false;
    
    // Read version header from sector 7
    Flash_ReadData(FLASH_SECTOR_7_ADDRESS + VERSION_OFFSET_IN_SECTOR7, 
                   (uint8_t*)&header, sizeof(version_header_t));
    
    // Check magic number
    if (header.magic == VERSION_MAGIC) {
        memcpy(version->version, header.version, 14);
        version->version[14] = '\0';
        version->valid = true;
        return true;
    }
    
    // No valid version found
    strcpy(version->version, "00000000000000");
    version->version[14] = '\0';
    version->valid = false;
    return false;
}

bool Flash_CheckSpaceAvailable(uint32_t required_size)
{
    // Check if required size fits in sector 6 (128KB)
    return (required_size <= FLASH_SECTOR_SIZE_6_7);
}

bool Flash_ProgramFirmwareData(uint32_t offset, const uint8_t* data, uint32_t length)
{
    uint32_t address = FLASH_SECTOR_6_ADDRESS + offset;
    
    // Ensure address is 4-byte aligned
    if (address % 4 != 0) return false;
    
    // Ensure we don't exceed sector 6 boundaries
    if ((offset + length) > FLASH_SECTOR_SIZE_6_7) return false;
    
    return Flash_WriteData(address, data, length);
}

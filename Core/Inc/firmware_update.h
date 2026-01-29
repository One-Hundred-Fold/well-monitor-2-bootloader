/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    firmware_update.h
  * @brief   Firmware update over BLE using Stephano-I module
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FIRMWARE_UPDATE_H
#define __FIRMWARE_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported constants --------------------------------------------------------*/
// Protocol Constants
#define WE_SPP_HEADER                0x01
#define PACKET_SIZE_MAX              1024
#define FIRMWARE_DATA_ALIGNMENT      4
#define UART_BUFFER_SIZE             2048

// Token Constants (ASCII/UTF-8)
#define TOKEN_SERVER_ROLE            "SERVER"
#define TOKEN_CLIENT_ROLE            "CLIENT"
#define TOKEN_FIRMWARE_SIZE          "FW_SIZE"
#define TOKEN_SUCCESS                "SUCCESS"
#define TOKEN_FIRMWARE_DATA          "FW_DATA"
#define TOKEN_ERROR                  "ERROR"

// Flash Sector Information for STM32F401
// Sector 0-3: 16KB each
// Sector 4: 64KB
// Sector 5: 128KB
// Sector 6: 128KB (for downloaded firmware)
// Sector 7: 128KB (for current version)
#define FLASH_SECTOR_DOWNLOAD        6
#define FLASH_SECTOR_CURRENT         7
#define FLASH_SECTOR_SIZE_6_7        0x20000  // 128KB

// Flash base address
#define FLASH_BASE_ADDRESS           0x08000000
// FLASH_SECTOR_6_ADDRESS and FLASH_SECTOR_7_ADDRESS are defined in flash_ops.h

// Version timestamp length (YYYYMMDDHHMMSS = 14 bytes)
#define VERSION_TIMESTAMP_LEN        14

// SHA256 digest length (hex string = 64 bytes)
#define SHA256_DIGEST_HEX_LEN        64

/* Exported types ------------------------------------------------------------*/
typedef enum {
    FW_UPDATE_IDLE,
    FW_UPDATE_WAITING_CONNECTION,
    FW_UPDATE_EXCHANGING_TOKENS,
    FW_UPDATE_RECEIVING_SIZE,
    FW_UPDATE_RECEIVING_DATA,
    FW_UPDATE_COMPLETE,
    FW_UPDATE_ERROR
} fw_update_state_t;

typedef struct {
    uint8_t rx_buffer[UART_BUFFER_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t rx_count;
    
    uint8_t tx_buffer[UART_BUFFER_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t tx_count;
} uart_buffers_t;

/* Exported functions prototypes ---------------------------------------------*/
void FirmwareUpdate_Init(void);
void FirmwareUpdate_Process(void);
bool FirmwareUpdate_IsActive(void);
void FirmwareUpdate_Start(void);
void FirmwareUpdate_UART_IRQHandler(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_UPDATE_H */

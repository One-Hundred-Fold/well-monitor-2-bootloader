/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    at_command.h
  * @brief   AT command interface for Stephano-I module
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __AT_COMMAND_H
#define __AT_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported constants --------------------------------------------------------*/
#define AT_RESPONSE_TIMEOUT_MS       5000
#define AT_MAX_RESPONSE_LEN          256

/* Exported types ------------------------------------------------------------*/
typedef enum {
    AT_OK,
    AT_ERROR,
    AT_TIMEOUT,
    AT_BUSY
} at_status_t;

/* Exported functions prototypes ---------------------------------------------*/
at_status_t AT_SendCommand(const char* command, char* response, uint16_t response_len, uint32_t timeout_ms);
at_status_t AT_Test(void);
at_status_t AT_Reset(void);
at_status_t AT_ConfigureFlowControl(void);
at_status_t AT_EnableBLE(void);
at_status_t AT_ConnectBLE(const char* address);
at_status_t AT_DisconnectBLE(void);
at_status_t AT_FactoryReset(void);

#ifdef __cplusplus
}
#endif

#endif /* __AT_COMMAND_H */

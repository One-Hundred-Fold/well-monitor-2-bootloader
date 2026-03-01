/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    at_command.c
  * @brief   AT command interface for Stephano-I module
  ******************************************************************************
  */
/* USER CODE END Header */

#include "at_command.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define BOOTLOADER_DEBUG_ENABLE 1
#include "stm32f4xx_hal_uart.h"
extern UART_HandleTypeDef huart1;

/* STEPHANO_UART_PTR from main.h */

static char at_response_buffer[AT_MAX_RESPONSE_LEN];
static volatile uint16_t at_response_len = 0;

// Note: HAL_UART_RxCpltCallback is implemented in firmware_update.c
// AT commands use blocking receive to avoid conflicts

at_status_t AT_SendCommand(const char* command, char* response, uint16_t response_len, uint32_t timeout_ms)
{
    uint8_t cmd_with_crlf[256];
    uint16_t cmd_len;
    HAL_StatusTypeDef status;
    uint16_t request_size;
    uint16_t received_bytes;

    if (command == NULL) return AT_ERROR;

    cmd_len = strlen(command);
    if (cmd_len > 250) return AT_ERROR;

    memcpy(cmd_with_crlf, command, cmd_len);
    cmd_with_crlf[cmd_len++] = '\r';
    cmd_with_crlf[cmd_len++] = '\n';

    memset(at_response_buffer, 0, sizeof(at_response_buffer));
    at_response_len = 0;

    (void)HAL_UART_AbortReceive_IT(STEPHANO_UART_PTR);

    {
        uint8_t discard;
        uint32_t flush_end = HAL_GetTick() + 50;
        while (HAL_GetTick() < flush_end) {
            if (HAL_UART_Receive(STEPHANO_UART_PTR, &discard, 1, 5) != HAL_OK)
                break;
        }
    }

#if BOOTLOADER_DEBUG_ENABLE
  {
      char dbg_msg[128];
      int len = sprintf(dbg_msg, "%s send ", __FUNCTION__);
      HAL_UART_Transmit(&huart1, (uint8_t*) dbg_msg, len, 1000);
      HAL_UART_Transmit(&huart1, cmd_with_crlf, cmd_len, 1000);
  }
#endif

    status = HAL_UART_Transmit(STEPHANO_UART_PTR, cmd_with_crlf, cmd_len, timeout_ms);
    if (status != HAL_OK) {
        return AT_ERROR;
    }

    /* Allow timeout_ms for echo, processing, and response (all with line termination) */
    request_size = (uint16_t)(AT_MAX_RESPONSE_LEN - 1);
    status = HAL_UART_Receive(STEPHANO_UART_PTR, (uint8_t *)at_response_buffer, request_size, timeout_ms);
    received_bytes = request_size - STEPHANO_UART_PTR->RxXferCount;
    at_response_buffer[received_bytes] = '\0';
    at_response_len = received_bytes;
    
    // Note: Interrupt-based receive is stopped during AT commands to avoid conflicts
    // It will be restarted by firmware_update module after AT commands complete
#if BOOTLOADER_DEBUG_ENABLE
  {
      char dbg_msg[128];
      int len = sprintf(dbg_msg, "%s recv ", __FUNCTION__);
      HAL_UART_Transmit(&huart1, (uint8_t*) dbg_msg, len, 1000);
      HAL_UART_Transmit(&huart1, (uint8_t*) at_response_buffer, at_response_len, 1000);
  }
#endif
    
    // Copy response if buffer provided
    if (response != NULL && response_len > 0 && at_response_len > 0) {
        uint16_t copy_len = (at_response_len < response_len - 1) ? 
                            at_response_len : response_len - 1;
        memcpy(response, at_response_buffer, copy_len);
        response[copy_len] = '\0';
    }
    
    // Check for OK/ERROR
    if (strstr((char*)at_response_buffer, "OK") != NULL) {
        return AT_OK;
    } else if (strstr((char*)at_response_buffer, "ERROR") != NULL) {
        return AT_ERROR;
    }
    
    return (at_response_len > 0) ? AT_OK : AT_TIMEOUT;
}

at_status_t AT_Test(void)
{
    return AT_SendCommand("AT", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
}

at_status_t AT_Reset(void)
{
    return AT_SendCommand("AT+RST", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
}

at_status_t AT_ConfigureFlowControl(void)
{
    // Configure hardware flow control (RTS/CTS)
    // AT+UART_CUR command format: AT+UART_CUR=<baud>,<databits>,<stopbits>,<parity>,<flowcontrol>
    // Flow control: 0=None, 1=RTS/CTS
    at_status_t status;
    
    // Set UART with hardware flow control
    status = AT_SendCommand("AT+UART_CUR=115200,8,1,0,1", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
    if (status != AT_OK) {
        return status;
    }
    
    return AT_OK;
}

at_status_t AT_EnableBLE(void)
{
    // Enable BLE mode
    at_status_t status;
    
    // Set to BLE mode
    status = AT_SendCommand("AT+BLEMODE=1", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
    if (status != AT_OK) {
        return status;
    }
    
    return AT_OK;
}

at_status_t AT_ConnectBLE(const char* address)
{
    // Connect to BLE device (server will initiate connection, so this may not be needed)
    // For now, we'll configure the module to be discoverable and connectable
    at_status_t status;
    
    // Make device discoverable
    status = AT_SendCommand("AT+BLEADV=1", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
    if (status != AT_OK) {
        return status;
    }
    
    return AT_OK;
}

at_status_t AT_DisconnectBLE(void)
{
    // Disconnect BLE
    return AT_SendCommand("AT+BLEDISCON", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
}

at_status_t AT_FactoryReset(void)
{
    // Reset to factory settings
    return AT_SendCommand("AT+RESTORE", NULL, 0, AT_RESPONSE_TIMEOUT_MS);
}

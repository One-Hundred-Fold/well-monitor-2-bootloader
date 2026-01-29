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
    uint32_t start_tick;
    uint16_t received_bytes = 0;
    
    if (command == NULL) return AT_ERROR;
    
    // Prepare command with CRLF
    cmd_len = strlen(command);
    if (cmd_len > 250) return AT_ERROR;
    
    memcpy(cmd_with_crlf, command, cmd_len);
    cmd_with_crlf[cmd_len++] = '\r';
    cmd_with_crlf[cmd_len++] = '\n';
    
    // Clear response buffer
    memset(at_response_buffer, 0, sizeof(at_response_buffer));
    at_response_len = 0;
    
    // Note: Interrupt-based receive is active, but for AT commands we use blocking receive
    // This will temporarily override the interrupt receive
    
    // Send command
    status = HAL_UART_Transmit(STEPHANO_UART_PTR, cmd_with_crlf, cmd_len, timeout_ms);
    if (status != HAL_OK) {
        return AT_ERROR;
    }
    
    // Wait for response - read bytes until we get CRLF or timeout
    start_tick = HAL_GetTick();
    received_bytes = 0;
    
    while ((HAL_GetTick() - start_tick) < timeout_ms) {
        uint8_t byte;
        status = HAL_UART_Receive(STEPHANO_UART_PTR, &byte, 1, 100);
        
        if (status == HAL_OK) {
            if (received_bytes < (AT_MAX_RESPONSE_LEN - 1)) {
                at_response_buffer[received_bytes++] = byte;
                
                // Check for CRLF (end of response)
                if (received_bytes >= 2 && 
                    at_response_buffer[received_bytes - 2] == '\r' &&
                    at_response_buffer[received_bytes - 1] == '\n') {
                    break;
                }
            } else {
                break;  // Buffer full
            }
        }
    }
    
    at_response_buffer[received_bytes] = '\0';
    at_response_len = received_bytes;
    
    // Note: Interrupt-based receive is stopped during AT commands to avoid conflicts
    // It will be restarted by firmware_update module after AT commands complete
    
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

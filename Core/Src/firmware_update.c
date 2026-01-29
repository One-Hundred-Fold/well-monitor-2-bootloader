/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    firmware_update.c
  * @brief   Firmware update over BLE using Stephano-I module
  ******************************************************************************
  */
/* USER CODE END Header */

#include "firmware_update.h"
#include "at_command.h"
#include "flash_ops.h"
#include "sha256.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* STEPHANO_UART_PTR from main.h */

/* Private variables */
static fw_update_state_t update_state = FW_UPDATE_IDLE;
static uart_buffers_t uart_buffers;
static uint16_t expected_packet_num = 0;
static uint32_t firmware_size = 0;
static uint32_t firmware_received = 0;
static bool update_active = false;

/* Interrupt-based receive variables */
static uint8_t uart_rx_byte = 0;

/* Private function prototypes */
static void StephanoI_PowerOn(void);
static void StephanoI_Reset(void);
static bool ParseWESPPPacket(const uint8_t* data, uint16_t len, uint8_t** payload, uint16_t* payload_len);
static bool CreateWESPPPacket(const uint8_t* payload, uint16_t payload_len, uint8_t* packet, uint16_t* packet_len);
static void ProcessReceivedData(void);
static void HandleTokenExchange(void);
static void HandleFirmwareSize(void);
static void HandleFirmwarePacket(void);
static void GetVersionTimestamp(char* version, uint16_t len);

/* Initialize firmware update module */
void FirmwareUpdate_Init(void)
{
    memset(&uart_buffers, 0, sizeof(uart_buffers));
    update_state = FW_UPDATE_IDLE;
    update_active = false;
    expected_packet_num = 0;
    firmware_size = 0;
    firmware_received = 0;
    
    // Start UART receive in interrupt mode (receive one byte at a time)
    HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
}

/* Power on Stephano-I module */
static void StephanoI_PowerOn(void)
{
#if !STEPHANO_USE_UART1
    // Set n_STEPHANO_ON to LOW (active low)
    HAL_GPIO_WritePin(n_STEPHANO_ON_GPIO_Port, n_STEPHANO_ON_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);  // Wait 100ms
#endif
}

/* Reset Stephano-I module */
static void StephanoI_Reset(void)
{
#if !STEPHANO_USE_UART1
    // Set n_STEPHANO_RST to LOW for 100ms, then HIGH
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);  // Wait for module to stabilize
#endif
}

/* Start firmware update process */
void FirmwareUpdate_Start(void)
{
    at_status_t at_status;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    if (update_active) return;
    
    update_active = true;
    update_state = FW_UPDATE_IDLE;

#if STEPHANO_USE_UART1
    // When UART1 is selected, leave n_STEPHANO_ON and n_STEPHANO_RST in their default states,
    // and assert n_3GON low.
    HAL_GPIO_WritePin(n_3GON_GPIO_Port, n_3GON_Pin, GPIO_PIN_RESET);
#endif

    // Ensure selected UART CTS output is driven low before talking to the module by
    // temporarily switching CTS to GPIO output, then restoring Alternate Function.
#if STEPHANO_USE_UART1
    // USART1_CTS is on PA11 (EXT_MODEM_CTS)
    // First, configure as GPIO output and drive low
    GPIO_InitStruct.Pin = EXT_MODEM_CTS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(EXT_MODEM_CTS_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(EXT_MODEM_CTS_GPIO_Port, EXT_MODEM_CTS_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);  // Small delay to ensure signal is stable
    
    // Now restore to alternate function mode
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(EXT_MODEM_CTS_GPIO_Port, &GPIO_InitStruct);
#else
    // USART2_CTS is on PA0 (STEPHANO_CTS)
    // First, configure as GPIO output and drive low
    GPIO_InitStruct.Pin = STEPHANO_CTS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(STEPHANO_CTS_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(STEPHANO_CTS_GPIO_Port, STEPHANO_CTS_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);  // Small delay to ensure signal is stable
    
    // Now restore to alternate function mode
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(STEPHANO_CTS_GPIO_Port, &GPIO_InitStruct);
#endif
    
    // Power on Stephano-I module
    StephanoI_PowerOn();
    
    // Reset Stephano-I module
    StephanoI_Reset();
    
    // Wait a bit for module to initialize
    HAL_Delay(500);
    
    // Stop interrupt-based receive temporarily for AT commands
    HAL_UART_AbortReceive_IT(STEPHANO_UART_PTR);

    // Temporarily disable CTS hardware flow control for initial AT commands
    // The module may not be ready to assert CTS immediately after power-on/reset
    // We'll re-enable it after configuring the module
    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);

    // Reset module via AT command before initial test
    at_status = AT_Reset();
    if (at_status != AT_OK) {
        update_state = FW_UPDATE_ERROR;
        update_active = false;
        return;
    }
    
    // Test AT command
    at_status = AT_Test();
    if (at_status != AT_OK) {
        update_state = FW_UPDATE_ERROR;
        update_active = false;
        return;
    }
    
    // Configure hardware flow control on the module
    at_status = AT_ConfigureFlowControl();
    if (at_status != AT_OK) {
        update_state = FW_UPDATE_ERROR;
        update_active = false;
        return;
    }
    
    // Re-enable hardware flow control in UART now that module is configured
    // The module should now be ready to assert CTS properly
    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_ENABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);
    
    // Enable BLE mode
    at_status = AT_EnableBLE();
    if (at_status != AT_OK) {
        update_state = FW_UPDATE_ERROR;
        update_active = false;
        return;
    }
    
    // Make device discoverable and wait for connection
    at_status = AT_ConnectBLE(NULL);
    if (at_status != AT_OK) {
        update_state = FW_UPDATE_ERROR;
        update_active = false;
        return;
    }
    
    // Restart UART receive in interrupt mode for firmware update protocol
    HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
    
    update_state = FW_UPDATE_WAITING_CONNECTION;
}

/* Check if update is active */
bool FirmwareUpdate_IsActive(void)
{
    return update_active;
}

/* Parse WE SPP packet (header 0x01) */
static bool ParseWESPPPacket(const uint8_t* data, uint16_t len, uint8_t** payload, uint16_t* payload_len)
{
    if (data == NULL || len < 1) return false;
    
    // Check for WE SPP header
    if (data[0] != WE_SPP_HEADER) return false;
    
    if (payload != NULL) {
        *payload = (uint8_t*)(data + 1);
    }
    
    if (payload_len != NULL) {
        *payload_len = len - 1;
    }
    
    return true;
}

/* Create WE SPP packet */
static bool CreateWESPPPacket(const uint8_t* payload, uint16_t payload_len, uint8_t* packet, uint16_t* packet_len)
{
    if (packet == NULL || packet_len == NULL) return false;
    
    packet[0] = WE_SPP_HEADER;
    
    if (payload != NULL && payload_len > 0) {
        memcpy(packet + 1, payload, payload_len);
    }
    
    *packet_len = 1 + payload_len;
    return true;
}

/* Get version timestamp */
static void GetVersionTimestamp(char* version, uint16_t len)
{
    firmware_version_t fw_version;
    
    if (version == NULL || len < 15) return;
    
    if (Flash_GetCurrentVersion(&fw_version) && fw_version.valid) {
        // Copy version timestamp (14 characters: YYYYMMDDHHMMSS)
        memcpy(version, fw_version.version, 14);
        version[14] = '\0';
    } else {
        // No valid version - report as version 0 (all zeros)
        memcpy(version, "00000000000000", 14);
        version[14] = '\0';
    }
}

/* Process received data from UART */
static void ProcessReceivedData(void)
{
    // Process data if we have enough in the buffer
    if (uart_buffers.rx_count > 0) {
        // Process based on current state
        switch (update_state) {
            case FW_UPDATE_WAITING_CONNECTION:
            case FW_UPDATE_EXCHANGING_TOKENS:
                HandleTokenExchange();
                break;
                
            case FW_UPDATE_RECEIVING_SIZE:
                HandleFirmwareSize();
                break;
                
            case FW_UPDATE_RECEIVING_DATA:
                HandleFirmwarePacket();
                break;
                
            default:
                break;
        }
    }
}

/* UART interrupt handler - called from USART1 or USART2 IRQHandler (selected at compile time) */
void FirmwareUpdate_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    // This function is called before HAL_UART_IRQHandler
    // HAL will handle the interrupt and call HAL_UART_RxCpltCallback
    // No additional processing needed here
}

/* Handle token exchange */
static void HandleTokenExchange(void)
{
    uint8_t response[128];
    uint8_t packet[128];
    uint16_t response_len, packet_len;
    char version[15];
    char server_info[64];
    uint16_t bytes_to_process;
    
    // Check if we received SERVER token
    if (uart_buffers.rx_count > 0) {
        // Process data from circular buffer
        bytes_to_process = uart_buffers.rx_count;
        uint8_t* rx_data = uart_buffers.rx_buffer + uart_buffers.rx_head;
        
        // Handle wrap-around if needed
        uint8_t temp_buffer[128];
        if ((uart_buffers.rx_head + bytes_to_process) > UART_BUFFER_SIZE) {
            // Data wraps around
            uint16_t first_part = UART_BUFFER_SIZE - uart_buffers.rx_head;
            memcpy(temp_buffer, rx_data, first_part);
            memcpy(temp_buffer + first_part, uart_buffers.rx_buffer, bytes_to_process - first_part);
            rx_data = temp_buffer;
        }
        
        uint8_t* payload = NULL;
        uint16_t payload_len = 0;
        
        if (ParseWESPPPacket(rx_data, bytes_to_process, &payload, &payload_len)) {
            server_info[0] = '\0';
            if (payload_len < sizeof(server_info)) {
                memcpy(server_info, payload, payload_len);
                server_info[payload_len] = '\0';
            }
            
            // Check if it starts with SERVER
            if (strncmp((char*)payload, TOKEN_SERVER_ROLE, strlen(TOKEN_SERVER_ROLE)) == 0) {
                // Get our version
                GetVersionTimestamp(version, sizeof(version));
                
                // Send CLIENT token with our version
                snprintf((char*)response, sizeof(response), "%s:%s", TOKEN_CLIENT_ROLE, version);
                response_len = strlen((char*)response);
                
                CreateWESPPPacket(response, response_len, packet, &packet_len);
                HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
                
                // Clear processed data from buffer
                uart_buffers.rx_count = 0;
                uart_buffers.rx_head = 0;
                
                update_state = FW_UPDATE_RECEIVING_SIZE;
            }
        }
    }
}

/* Handle firmware size message */
static void HandleFirmwareSize(void)
{
    uint8_t response[64];
    uint8_t packet[64];
    uint16_t response_len, packet_len;
    char* size_token;
    uint16_t bytes_to_process;
    
    if (uart_buffers.rx_count > 0) {
        bytes_to_process = uart_buffers.rx_count;
        uint8_t* rx_data = uart_buffers.rx_buffer + uart_buffers.rx_head;
        
        // Handle wrap-around if needed
        uint8_t temp_buffer[128];
        if ((uart_buffers.rx_head + bytes_to_process) > UART_BUFFER_SIZE) {
            // Data wraps around
            uint16_t first_part = UART_BUFFER_SIZE - uart_buffers.rx_head;
            memcpy(temp_buffer, rx_data, first_part);
            memcpy(temp_buffer + first_part, uart_buffers.rx_buffer, bytes_to_process - first_part);
            rx_data = temp_buffer;
        }
        
        uint8_t* payload = NULL;
        uint16_t payload_len = 0;
        
        if (ParseWESPPPacket(rx_data, bytes_to_process, &payload, &payload_len)) {
            // Check for FW_SIZE token
            if (strncmp((char*)payload, TOKEN_FIRMWARE_SIZE, strlen(TOKEN_FIRMWARE_SIZE)) == 0) {
                // Extract size
                (void)strtok((char*)payload, ":");  // Skip token part
                size_token = strtok(NULL, ":");
                
                if (size_token != NULL) {
                    firmware_size = strtoul(size_token, NULL, 10);
                    
                    // Check if we have enough flash space
                    if (Flash_CheckSpaceAvailable(firmware_size)) {
                        // Erase sector 6
                        if (Flash_EraseSector(FLASH_SECTOR_DOWNLOAD)) {
                            firmware_received = 0;
                            expected_packet_num = 0;
                            
                            // Send SUCCESS
                            strcpy((char*)response, TOKEN_SUCCESS);
                            response_len = strlen((char*)response);
                            
                            CreateWESPPPacket(response, response_len, packet, &packet_len);
                            HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
                            
                            // Clear processed data from buffer
                            uart_buffers.rx_count = 0;
                            uart_buffers.rx_head = 0;
                            
                            update_state = FW_UPDATE_RECEIVING_DATA;
                        } else {
                            // Send ERROR
                            strcpy((char*)response, TOKEN_ERROR);
                            response_len = strlen((char*)response);
                            
                            CreateWESPPPacket(response, response_len, packet, &packet_len);
                            HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
                            
                            update_state = FW_UPDATE_ERROR;
                        }
                    } else {
                        // Send ERROR - insufficient space
                        strcpy((char*)response, TOKEN_ERROR);
                        response_len = strlen((char*)response);
                        
                        CreateWESPPPacket(response, response_len, packet, &packet_len);
                        HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
                        
                        update_state = FW_UPDATE_ERROR;
                    }
                }
            }
        }
    }
}

/* Handle firmware data packet */
static void HandleFirmwarePacket(void)
{
    uint8_t response[128];
    uint8_t packet[128];
    uint16_t response_len, packet_len;
    uint8_t* payload = NULL;
    uint16_t payload_len = 0;
    uint16_t packet_num, data_size, header_data_offset, flags;
    uint8_t* firmware_data;
    uint8_t sha256_hash[SHA256_DIGEST_SIZE];
    char sha256_hex[SHA256_DIGEST_HEX_LEN + 1];
    uint16_t bytes_to_process;
    uint8_t temp_buffer[PACKET_SIZE_MAX + 16];  // Max packet size + header
    
    if (uart_buffers.rx_count == 0) return;
    
    bytes_to_process = uart_buffers.rx_count;
    uint8_t* rx_data = uart_buffers.rx_buffer + uart_buffers.rx_head;
    
    // Handle wrap-around if needed
    if ((uart_buffers.rx_head + bytes_to_process) > UART_BUFFER_SIZE) {
        // Data wraps around
        uint16_t first_part = UART_BUFFER_SIZE - uart_buffers.rx_head;
        if (bytes_to_process <= sizeof(temp_buffer)) {
            memcpy(temp_buffer, rx_data, first_part);
            memcpy(temp_buffer + first_part, uart_buffers.rx_buffer, bytes_to_process - first_part);
            rx_data = temp_buffer;
        } else {
            return;  // Packet too large
        }
    }
    
    if (!ParseWESPPPacket(rx_data, bytes_to_process, &payload, &payload_len)) {
        return;
    }
    
    // Parse packet header
    // Header: token(2) + packet_num(2) + data_size(2) + offset(2) + flags(2) + padding(2) = 12 bytes
    if (payload_len < 12) return;
    
    // Check token (first 2 bytes should be "FW")
    if (payload[0] != 'F' || payload[1] != 'W') return;
    
    // Extract header fields (little-endian)
    packet_num = payload[2] | (payload[3] << 8);
    data_size = payload[4] | (payload[5] << 8);
    header_data_offset = payload[6] | (payload[7] << 8);  // Offset from header start to data
    flags = payload[8] | (payload[9] << 8);
    
    // Check if this is the expected packet
    if (packet_num != expected_packet_num) {
        // Send "0" to indicate unexpected packet number
        strcpy((char*)response, "0");
        response_len = 1;
        
        CreateWESPPPacket(response, response_len, packet, &packet_len);
        HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
        return;
    }
    
    // Extract firmware data (starts at header_data_offset from the start of the payload/header)
    // The header_data_offset is relative to the start of the header (which is at payload[0])
    if (header_data_offset > payload_len) return;
    firmware_data = payload + header_data_offset;
    
    // Adjust data_size if it exceeds available payload
    if ((header_data_offset + data_size) > payload_len) {
        data_size = payload_len - header_data_offset;
    }
    
    // Program firmware data to flash
    if (!Flash_ProgramFirmwareData(firmware_received, firmware_data, data_size)) {
        // Error programming flash
        strcpy((char*)response, TOKEN_ERROR);
        response_len = strlen((char*)response);
        
        CreateWESPPPacket(response, response_len, packet, &packet_len);
        HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
        
        update_state = FW_UPDATE_ERROR;
        return;
    }
    
    // Calculate SHA256 digest of received data
    SHA256_Calculate(firmware_data, data_size, sha256_hash);
    SHA256_HashToHex(sha256_hash, sha256_hex);
    
    // Send digest back
    strncpy((char*)response, sha256_hex, sizeof(response) - 1);
    response[sizeof(response) - 1] = '\0';
    response_len = strlen((char*)response);
    
    CreateWESPPPacket(response, response_len, packet, &packet_len);
    HAL_UART_Transmit(STEPHANO_UART_PTR, packet, packet_len, 1000);
    
    // Update state
    firmware_received += data_size;
    expected_packet_num++;
    
    // Clear processed data from buffer
    // For simplicity, clear all data after processing a complete packet
    // In a more sophisticated implementation, we'd track how much was consumed
    uart_buffers.rx_count = 0;
    uart_buffers.rx_head = 0;
    
    // Check if this was the last packet
    if (flags == 1 || firmware_received >= firmware_size) {
        update_state = FW_UPDATE_COMPLETE;
        
        // Reset Stephano-I to factory settings
        AT_FactoryReset();
        
        // Disconnect
        AT_DisconnectBLE();
        
        // Reboot (NVIC_SystemReset)
        HAL_Delay(100);
        NVIC_SystemReset();
    }
}

/* Main process function - call from main loop */
void FirmwareUpdate_Process(void)
{
    if (!update_active) return;
    
    // Process received UART data
    ProcessReceivedData();
    
    // Check for completion or error
    if (update_state == FW_UPDATE_COMPLETE || update_state == FW_UPDATE_ERROR) {
        update_active = false;
    }
}

/* UART RX complete callback - called by HAL after interrupt-based receive completes */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if STEPHANO_USE_UART1
    if (huart->Instance == USART1) {
#else
    if (huart->Instance == USART2) {
#endif
        // Add received byte to circular buffer
        if (uart_buffers.rx_count < UART_BUFFER_SIZE) {
            uint16_t write_pos = (uart_buffers.rx_head + uart_buffers.rx_count) % UART_BUFFER_SIZE;
            uart_buffers.rx_buffer[write_pos] = uart_rx_byte;
            uart_buffers.rx_count++;
        }
        // If buffer is full, data will be lost (could add error handling)
        
        // Restart receive for next byte
        HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
    }
}

/* UART error callback */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
#if STEPHANO_USE_UART1
    if (huart->Instance == USART1) {
#else
    if (huart->Instance == USART2) {
#endif
        // Restart receive after error
        HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
    }
}

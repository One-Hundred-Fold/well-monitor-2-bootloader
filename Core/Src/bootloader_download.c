/**
  ******************************************************************************
  * @file    bootloader_download.c
  * @brief   BLE download via Stephano-I: WSM↔PC protocol (plain ASCII).
  ******************************************************************************
  */

#include "bootloader_download.h"
#include "app_metadata.h"
#include "flash_ops.h"
#include "at_command.h"
#include "main.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BOOTLOADER_DEBUG_ENABLE 1

#define DOWNLOAD_BUFFER_SIZE  4096
#define LINE_BUFFER_SIZE      128
#define APP_VERSION_NONE      "0.0.0"

/* WELL_ID storage in sector 1 (0x08004000) */
#define STORED_PARAMS_FLASH_SECTOR FLASH_SECTOR_3
#define WELL_ID_STORAGE_ADDR  0x0800C000
#define WELL_ID_MAGIC         0x57454C4C  /* "WELL" */

#include "stm32f4xx_hal_uart.h"
extern UART_HandleTypeDef huart1;

typedef enum {
    DL_STATE_STEPHANO_POWER,
    DL_STATE_WAIT_READY,
    DL_STATE_AT_RESTORE,
    DL_STATE_AT_CFG,
    DL_STATE_WE_SPP_SETUP,
    DL_STATE_WAIT_CONNECT,
    DL_STATE_SEND_WSM_ID,
    DL_STATE_WAIT_ID_RESP,
    DL_STATE_SEND_WSM_MAC,
    DL_STATE_WAIT_WSM_ID,
    DL_STATE_SEND_WSM_BL,
    DL_STATE_WAIT_BL_RESP,
    DL_STATE_BL_DOWNLOAD,
    DL_STATE_SEND_WSM_APP,
    DL_STATE_WAIT_APP_RESP,
    DL_STATE_APP_DOWNLOAD,
    DL_STATE_ERROR
} dl_state_t;

static dl_state_t dl_state = DL_STATE_STEPHANO_POWER;
static uint8_t rx_buffer[DOWNLOAD_BUFFER_SIZE];
static uint16_t rx_head = 0;
static uint16_t rx_count = 0;
static uint8_t line_buffer[LINE_BUFFER_SIZE];
static uint16_t line_len = 0;
static uint32_t download_size = 0;
static uint32_t download_received = 0;
static uint16_t expected_packet = 0;
static bool downloading_bootloader = false;

static uint8_t uart_rx_byte;

#define MAC_BUF_SIZE 20
static char mac_buf[MAC_BUF_SIZE] = "00:00:00:00:00:00";
static uint16_t well_id = 0;
static bool have_stored_well_id = false;

static void dying_gasp(const char *msg);
static void Stephano_PowerOn(void);
static void Stephano_Reset(void);
static bool wait_for_ready(uint32_t timeout_ms);
static void get_bootloader_version(char *buf, size_t len);
static void get_app_version(char *buf, size_t len);
static void get_mac_from_module(void);
static void read_stored_well_id(void);
static void save_well_id(uint16_t id);
static void handle_id_response(const char *line);
static void handle_wsm_id_response(const char *line);
static bool parse_line(const char *line);
static void handle_bl_response(const char *line);
static void handle_app_response(const char *line);
static void process_rx_data(void);

static void dying_gasp(const char *msg)
{
    char buf[128];
    size_t n = snprintf(buf, sizeof(buf), "Bootloader Error! %s\r\n", msg ? msg : "Unknown");

#if BOOTLOADER_DEBUG_ENABLE
  {
	char dbg_msg[128];
	int len = sprintf(dbg_msg, "%s->%s\r\n", __FUNCTION__, msg);
    HAL_UART_Transmit(&huart1, (uint8_t*) dbg_msg, len, 100);
  }
#endif

    HAL_UART_Transmit(STEPHANO_UART_PTR, (uint8_t *)buf, (uint16_t)n, 1000);
    HAL_Delay(100);
    __disable_irq();
    NVIC_SystemReset();
}

static void Stephano_PowerOn(void)
{
    HAL_GPIO_WritePin(n_STEPHANO_ON_GPIO_Port, n_STEPHANO_ON_Pin, GPIO_PIN_RESET);
    HAL_Delay(500);
}

static void Stephano_PowerOff(void)
{
    HAL_GPIO_WritePin(n_STEPHANO_ON_GPIO_Port, n_STEPHANO_ON_Pin, GPIO_PIN_SET);
    HAL_Delay(500);
}

static void Stephano_Reset(void)
{
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(500);
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_SET);
}

static bool wait_for_ready(uint32_t timeout_ms)
{
    uint8_t buf[40];
    memset(buf, 0, 40);

    for (int attempt = 0; attempt < 10; ++ attempt)
    {
    	HAL_UART_Receive(STEPHANO_UART_PTR, buf, 7, timeout_ms);
#if BOOTLOADER_DEBUG_ENABLE
    	{
    		char dbg_msg[128];
    		int len = snprintf(dbg_msg, sizeof(dbg_msg), "%s -> '%s'\r\n", __FUNCTION__, (char*) buf);
    		HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
    	}
#endif
    	if (strstr((char*) buf, "ready") != NULL) return true;
    }

    return false;
}

static void get_bootloader_version(char *buf, size_t len)
{
    const uint8_t *meta = (const uint8_t *)_app_metadata_start;
    size_t i;
    if (len < 9) return;
    for (i = 0; i < 8 && meta[APP_METADATA_OFFSET_VERSION + i] != 0; i++)
        buf[i] = (char)meta[APP_METADATA_OFFSET_VERSION + i];
    buf[i] = '\0';
}

/* Search sector for metadata at 8-byte boundaries. */
static const uint8_t *find_metadata_in_sector(uint32_t sector_addr, uint32_t sector_size)
{
    static const uint8_t MAGIC[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
    uint32_t addr;
    for (addr = sector_addr; addr + APP_METADATA_SIZE <= sector_addr + sector_size; addr += 8) {
        const uint8_t *p = (const uint8_t *)addr;
        if (memcmp(p + APP_METADATA_OFFSET_MAGIC, MAGIC, 8) == 0)
            return p;
    }
    return NULL;
}

static void get_app_version(char *buf, size_t len)
{
    const uint8_t *meta = find_metadata_in_sector(FLASH_SECTOR_7_ADDRESS, FLASH_SECTOR_SIZE_6_7);
    if (meta != NULL) {
        size_t i;
        for (i = 0; i < 8 && meta[APP_METADATA_OFFSET_VERSION + i] != 0; i++)
            buf[i] = (char)meta[APP_METADATA_OFFSET_VERSION + i];
        buf[i] = '\0';
    } else {
        strncpy(buf, APP_VERSION_NONE, len - 1);
        buf[len - 1] = '\0';
    }
}

/* Extract MAC from AT+CIPSTAMAC? response.*/
static void get_mac_from_module(void)
{
    char resp[AT_MAX_RESPONSE_LEN];
    if (AT_SendCommand("AT+CIPSTAMAC?", resp, sizeof(resp), 3000) != AT_OK) {
        strncpy(mac_buf, "00:00:00:00:00:00", MAC_BUF_SIZE - 1);
        mac_buf[MAC_BUF_SIZE - 1] = '\0';
        return;
    }
    // Find first digit in response
    const char *p = resp;
    while (*p && !(*p >= '0' && *p <= '9')) p++;
    size_t i = 0;
    while (*p && ((*p >= '0' && *p <= '9') || (*p == ':')) && i < MAC_BUF_SIZE - 1) {
        mac_buf[i++] = *p++;
    }
    mac_buf[i] = '\0';
    if (i == 0) {
        strncpy(mac_buf, "00:00:00:00:00:00", MAC_BUF_SIZE - 1);
        mac_buf[MAC_BUF_SIZE - 1] = '\0';
    }
}

static void read_stored_well_id(void)
{
    uint32_t magic;
    uint8_t buf[6];
    Flash_ReadData(WELL_ID_STORAGE_ADDR, buf, 6);
    magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (magic == WELL_ID_MAGIC) {
        well_id = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
        have_stored_well_id = true;
    } else {
        well_id = 0;
        have_stored_well_id = false;
    }
}

static void save_well_id(uint16_t id)
{
    uint8_t buf[8];
    buf[0] = (uint8_t)(WELL_ID_MAGIC);
    buf[1] = (uint8_t)(WELL_ID_MAGIC >> 8);
    buf[2] = (uint8_t)(WELL_ID_MAGIC >> 16);
    buf[3] = (uint8_t)(WELL_ID_MAGIC >> 24);
    buf[4] = (uint8_t)(id);
    buf[5] = (uint8_t)(id >> 8);
    buf[6] = 0xFF;
    buf[7] = 0xFF;
    if (!Flash_EraseSector(STORED_PARAMS_FLASH_SECTOR))
        return;
    Flash_WriteData(WELL_ID_STORAGE_ADDR, buf, 8);
    well_id = id;
    have_stored_well_id = true;
}

/* Add byte to rx buffer (from UART callback). */
void Bootloader_RxByte(uint8_t b)
{
    if (rx_count < DOWNLOAD_BUFFER_SIZE) {
        uint16_t wi = (rx_head + rx_count) % DOWNLOAD_BUFFER_SIZE;
        rx_buffer[wi] = b;
        rx_count++;
    }
}

/* Extract a complete line (up to \r\n) into line_buffer. Returns true if line complete. */
static bool extract_line(void)
{
    while (rx_count > 0) {
        uint8_t b = rx_buffer[rx_head];
        rx_head = (rx_head + 1) % DOWNLOAD_BUFFER_SIZE;
        rx_count--;

        if (b == '\n') {
            line_buffer[line_len] = '\0';
            line_len = 0;
            return true;
        }
        if (b != '\r' && line_len < LINE_BUFFER_SIZE - 1)
            line_buffer[line_len++] = b;
    }
    return false;
}

/* Send string to PC. */
static void send_line(const char *s)
{
    size_t len = strlen(s);
    HAL_UART_Transmit(STEPHANO_UART_PTR, (uint8_t *)s, (uint16_t)len, 2000);
    HAL_UART_Transmit(STEPHANO_UART_PTR, (uint8_t *)"\r\n", 2, 500);
}

static void handle_id_response(const char *line)
{
    if (dl_state == DL_STATE_WAIT_ID_RESP) {
        if (strcmp(line, "OKAY") == 0) {
            dl_state = DL_STATE_SEND_WSM_BL;
        } else if (strcmp(line, "UNKNOWN") == 0) {
            dl_state = DL_STATE_SEND_WSM_MAC;
        }
    }
}

static void handle_wsm_id_response(const char *line)
{
    if (dl_state == DL_STATE_WAIT_WSM_ID && strncmp(line, "WSM ID ", 7) == 0) {
        unsigned int id_val;
        if (sscanf(line + 7, "%u", &id_val) == 1 && id_val <= 0xFFFF) {
            well_id = (uint16_t)id_val;
            save_well_id(well_id);
            dl_state = DL_STATE_SEND_WSM_BL;
        }
    }
}

static bool parse_line(const char *line)
{
    if (dl_state == DL_STATE_WAIT_ID_RESP)
        handle_id_response(line);
    else if (dl_state == DL_STATE_WAIT_WSM_ID)
        handle_wsm_id_response(line);
    else if (downloading_bootloader)
        handle_bl_response(line);
    else if (dl_state == DL_STATE_APP_DOWNLOAD)
        handle_app_response(line);
    else if (dl_state == DL_STATE_WAIT_BL_RESP)
        handle_bl_response(line);
    else if (dl_state == DL_STATE_WAIT_APP_RESP)
        handle_app_response(line);
    return true;
}

static void handle_bl_response(const char *line)
{
    if (dl_state == DL_STATE_WAIT_BL_RESP) {
        if (strncmp(line, "WSM BL OK", 9) == 0) {
            dl_state = DL_STATE_SEND_WSM_APP;
            return;
        }
        if (strncmp(line, "WSM BL ", 7) == 0) {
            unsigned int size_val = 0;
            char new_ver[16] = {0};
            int n = sscanf(line + 7, "%15s %u", new_ver, &size_val);
            if (n >= 2 && size_val > 0) {
                if (size_val > FLASH_SECTOR_SIZE_6_7) {
                    send_line("BL DL ERROR");
                    dying_gasp("New bootloader too large");
                }
                if (!Flash_EraseSector(FLASH_SECTOR_DOWNLOAD)) {
                    send_line("BL DL ERROR");
                    dying_gasp("Failed to erase sector 6");
                }
                send_line("BL DL READY");
                download_size = size_val;
                download_received = 0;
                expected_packet = 0;
                downloading_bootloader = true;
                dl_state = DL_STATE_BL_DOWNLOAD;
            }
            return;
        }
    }

}

static void handle_app_response(const char *line)
{
    if (dl_state == DL_STATE_WAIT_APP_RESP) {
        if (strncmp(line, "WSM APP OK", 10) == 0) {
            /* Done - reboot to let stage-1 run */
            HAL_Delay(100);
            NVIC_SystemReset();
            return;
        }
        if (strncmp(line, "WSM APP ", 8) == 0) {
            unsigned int size_val = 0;
            char new_ver[16] = {0};
            int n = sscanf(line + 8, "%15s %u", new_ver, &size_val);
            if (n >= 2 && size_val > 0) {
                if (size_val > FLASH_SECTOR_SIZE_6_7) {
                    send_line("APP DL ERROR");
                    dying_gasp("New application too large");
                }
                if (!Flash_EraseSector(FLASH_SECTOR_DOWNLOAD)) {
                    send_line("APP DL ERROR");
                    dying_gasp("Failed to erase sector 6");
                }
                send_line("APP DL READY");
                download_size = size_val;
                download_received = 0;
                expected_packet = 0;
                downloading_bootloader = false;
                dl_state = DL_STATE_APP_DOWNLOAD;
            }
            return;
        }
    }

}

/* Process BL DATA / APP DATA binary payload. The line "BL DATA N SIZE" has been
   consumed; remaining bytes in rx_buffer are the payload. We need to accumulate
   until we have the full payload for the current packet. For simplicity we handle
   one packet per line: the protocol sends "BL DATA N SIZE\r\n" then SIZE bytes.
   So we need a state: waiting for payload, payload_size, payload_received. */
static uint32_t pending_payload_size = 0;
static uint32_t pending_payload_received = 0;

#define FLASH_CHUNK 256
static uint8_t flash_chunk_buf[FLASH_CHUNK];
static uint16_t flash_chunk_len = 0;

static void flush_flash_chunk(void)
{
    if (flash_chunk_len == 0) return;
    if (!Flash_ProgramFirmwareData(download_received, flash_chunk_buf, flash_chunk_len)) {
        if (downloading_bootloader)
            send_line("BL DATA ERROR");
        else
            send_line("APP DATA ERROR");
        dying_gasp("Flash program failed");
    }
    download_received += flash_chunk_len;
    flash_chunk_len = 0;
}

static void process_binary_payload(void)
{
    if (pending_payload_size == 0) return;

    while (rx_count > 0 && pending_payload_received < pending_payload_size) {
        uint8_t b = rx_buffer[rx_head];
        rx_head = (rx_head + 1) % DOWNLOAD_BUFFER_SIZE;
        rx_count--;

        flash_chunk_buf[flash_chunk_len++] = b;
        pending_payload_received++;

        if (flash_chunk_len >= FLASH_CHUNK) {
            flush_flash_chunk();
        }

        if (pending_payload_received >= pending_payload_size) {
            flush_flash_chunk();
            if (downloading_bootloader)
                send_line("BL DATA OKAY");
            else
                send_line("APP DATA OKAY");
            expected_packet++;
            pending_payload_size = 0;
            pending_payload_received = 0;
            if (download_received >= download_size) {
                HAL_Delay(100);
                NVIC_SystemReset();
            }
            return;
        }
    }
}

/* Parse "BL DATA N SIZE" or "APP DATA N SIZE" and set pending_payload_size. */
static void parse_data_line(const char *line)
{
    if (strncmp(line, "BL DATA ", 8) == 0) {
        unsigned int n_val, size_val;
        if (sscanf(line + 8, "%u %u", &n_val, &size_val) == 2) {
            if (n_val != expected_packet) {
                send_line("BL DATA ERROR");
                dying_gasp("Unexpected packet number");
                return;
            }
            pending_payload_size = size_val;
            pending_payload_received = 0;
        }
    } else if (strncmp(line, "APP DATA ", 9) == 0) {
        unsigned int n_val, size_val;
        if (sscanf(line + 9, "%u %u", &n_val, &size_val) == 2) {
            if (n_val != expected_packet) {
                send_line("APP DATA ERROR");
                dying_gasp("Unexpected packet number");
                return;
            }
            pending_payload_size = size_val;
            pending_payload_received = 0;
        }
    }
}

static void process_rx_data(void)
{
    process_binary_payload();

    while (extract_line()) {
        const char *line = (const char *)line_buffer;
        if (dl_state == DL_STATE_BL_DOWNLOAD || dl_state == DL_STATE_APP_DOWNLOAD) {
            if ((strncmp(line, "BL DATA ", 8) == 0) || (strncmp(line, "APP DATA ", 9) == 0)) {
                parse_data_line(line);
                break;
            }
        }
        parse_line(line);
    }
}

void Bootloader_StartDownload(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    rx_head = 0;
    rx_count = 0;
    line_len = 0;
    pending_payload_size = 0;
    pending_payload_received = 0;
    dl_state = DL_STATE_STEPHANO_POWER;

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s begin\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif

    /* Drive CTS low before talking to Stephano */
    GPIO_InitStruct.Pin = STEPHANO_CTS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(STEPHANO_CTS_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(STEPHANO_CTS_GPIO_Port, STEPHANO_CTS_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(STEPHANO_CTS_GPIO_Port, &GPIO_InitStruct);

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s Stephano_PowerOn\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif
  Stephano_PowerOff();
  Stephano_PowerOn();

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s Stephano_Reset\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif
    Stephano_Reset();

    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s wait_for_ready\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif

    if (!wait_for_ready(10000))
        dying_gasp("Stephano Ready timeout");

    if (AT_SendCommand("AT+RESTORE", NULL, 0, 5000) != AT_OK)
        dying_gasp("AT+RESTORE failed");
    HAL_Delay(500);

    if (AT_SendCommand("AT+UART_CUR=115200,8,1,0,1", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+UART_CUR failed");

    get_mac_from_module();
    read_stored_well_id();

#if BOOTLOADER_USE_HARDWARE_FLOW_CONTROL
    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_ENABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);
#endif

    if (AT_SendCommand("AT+BLEINIT=2", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEINIT=2 failed");

    if (AT_SendCommand("AT+BLEGATTSSRVCRE", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEGATTSSRVCRE failed");

    if (AT_SendCommand("AT+BLEGATTSSRVSTART", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEGATTSSRVSTART failed");

    if (AT_SendCommand("AT+BLENAME=\"Stephano-I\"", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLENAME=\"Stephano-I\" failed");

    if (AT_SendCommand("AT+BLEADVDATA=\"0201060B095374657068616E6F2D49\"", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEADVDATA=\"0201060B095374657068616E6F2D49\" failed");

    if (AT_SendCommand("AT+BLEADVSTART", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEADVSTART failed");

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s Wait for BLE connection; then enter SPP mode\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif

    /* Wait for BLE connection; then enter SPP mode */
    dl_state = DL_STATE_WAIT_CONNECT;
    HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
}

/* Called from HAL when UART receive completes; add byte and restart receive. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if STEPHANO_USE_UART1
    if (huart->Instance == USART1) {
#else
    if (huart->Instance == USART2) {
#endif
        Bootloader_RxByte(uart_rx_byte);
        HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
    }
}

void Bootloader_Download_Process(void)
{

#if BOOTLOADER_DEBUG_ENABLE
  {
    char dbg_msg[128];
    int len = sprintf(dbg_msg, "%s begin\r\n", __FUNCTION__);
    HAL_UART_Transmit(&huart1, (uint8_t*)dbg_msg, len, 1000);
  }
#endif

    if (dl_state == DL_STATE_WAIT_CONNECT) {
        static uint32_t connect_start = 0;
        if (connect_start == 0) connect_start = HAL_GetTick();
        if ((HAL_GetTick() - connect_start) > 600000) {				// THIS IS THE CONNECTION WAIT TIME
            /* Proceed to SPP mode; user should be connected */
            HAL_UART_AbortReceive_IT(STEPHANO_UART_PTR);
            if (AT_SendCommand("AT+BLESPP", NULL, 0, 2000) != AT_OK)
                dying_gasp("AT+BLESPP failed");
            HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
            dl_state = have_stored_well_id ? DL_STATE_SEND_WSM_ID : DL_STATE_SEND_WSM_MAC;
            connect_start = 0;
        }
        process_rx_data();
        return;
    }

    if (dl_state == DL_STATE_SEND_WSM_ID) {
        char buf[64];
        snprintf(buf, sizeof(buf), "WSM ID %u", (unsigned int)well_id);
        send_line(buf);
        dl_state = DL_STATE_WAIT_ID_RESP;
        return;
    }

    if (dl_state == DL_STATE_SEND_WSM_MAC) {
        char buf[64];
        snprintf(buf, sizeof(buf), "WSM MAC %s", mac_buf);
        send_line(buf);
        dl_state = DL_STATE_WAIT_WSM_ID;
        return;
    }

    if (dl_state == DL_STATE_SEND_WSM_BL) {
        char ver[16];
        char buf[64];
        get_bootloader_version(ver, sizeof(ver));
        snprintf(buf, sizeof(buf), "WSM BL %s", ver);
        send_line(buf);
        dl_state = DL_STATE_WAIT_BL_RESP;
        return;
    }

    if (dl_state == DL_STATE_SEND_WSM_APP) {
        char ver[16];
        char buf[64];
        get_app_version(ver, sizeof(ver));
        snprintf(buf, sizeof(buf), "WSM APP %s", ver);
        send_line(buf);
        dl_state = DL_STATE_WAIT_APP_RESP;
        return;
    }

    process_rx_data();
}

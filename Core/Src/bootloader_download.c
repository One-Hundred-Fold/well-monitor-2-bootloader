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

#define DOWNLOAD_BUFFER_SIZE  4096
#define LINE_BUFFER_SIZE      128
#define APP_VERSION_NONE      "0.0.0"

typedef enum {
    DL_STATE_STEPHANO_POWER,
    DL_STATE_WAIT_READY,
    DL_STATE_AT_RESTORE,
    DL_STATE_AT_CFG,
    DL_STATE_WE_SPP_SETUP,
    DL_STATE_WAIT_CONNECT,
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

static void dying_gasp(const char *msg);
static void Stephano_PowerOn(void);
static void Stephano_Reset(void);
static bool wait_for_ready(uint32_t timeout_ms);
static void get_bootloader_version(char *buf, size_t len);
static void get_app_version(char *buf, size_t len);
static bool parse_line(const char *line);
static void handle_bl_response(const char *line);
static void handle_app_response(const char *line);
static void process_rx_data(void);

static void dying_gasp(const char *msg)
{
    char buf[128];
    size_t n = snprintf(buf, sizeof(buf), "Bootloader Error! %s\r\n", msg ? msg : "Unknown");
    HAL_UART_Transmit(STEPHANO_UART_PTR, (uint8_t *)buf, (uint16_t)n, 1000);
    HAL_Delay(100);
    __disable_irq();
    NVIC_SystemReset();
}

static void Stephano_PowerOn(void)
{
    HAL_GPIO_WritePin(n_STEPHANO_ON_GPIO_Port, n_STEPHANO_ON_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
}

static void Stephano_Reset(void)
{
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(n_STEPHANO_RST_GPIO_Port, n_STEPHANO_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
}

static bool wait_for_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    char buf[32];
    uint16_t bi = 0;

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t b;
        if (HAL_UART_Receive(STEPHANO_UART_PTR, &b, 1, 50) == HAL_OK) {
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = (char)b;
                buf[bi] = '\0';
                if (strstr(buf, "Ready") != NULL)
                    return true;
            }
        }
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

static bool parse_line(const char *line)
{
    if (downloading_bootloader)
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

    Stephano_PowerOn();
    Stephano_Reset();
    HAL_Delay(200);

    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);

    if (!wait_for_ready(3000))
        dying_gasp("Stephano Ready timeout");

    if (AT_SendCommand("AT+RESTORE", NULL, 0, 5000) != AT_OK)
        dying_gasp("AT+RESTORE failed");
    HAL_Delay(500);

    if (AT_SendCommand("AT+UART_CUR=115200,8,1,0,1", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+UART_CUR failed");

    __HAL_UART_DISABLE(STEPHANO_UART_PTR);
    __HAL_UART_HWCONTROL_CTS_ENABLE(STEPHANO_UART_PTR);
    __HAL_UART_ENABLE(STEPHANO_UART_PTR);

    if (AT_SendCommand("AT+BLEINIT=2", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEINIT=2 failed");
    if (AT_SendCommand("AT+BLENAME=\"Stephano_Device\"", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLENAME failed");
    if (AT_SendCommand("AT+BLEADVSTART", NULL, 0, 2000) != AT_OK)
        dying_gasp("AT+BLEADVSTART failed");

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
    if (dl_state == DL_STATE_WAIT_CONNECT) {
        static uint32_t connect_start = 0;
        if (connect_start == 0) connect_start = HAL_GetTick();
        if ((HAL_GetTick() - connect_start) > 10000) {
            /* Proceed to SPP mode; user should be connected */
            HAL_UART_AbortReceive_IT(STEPHANO_UART_PTR);
            if (AT_SendCommand("AT+BLESPP", NULL, 0, 2000) != AT_OK)
                dying_gasp("AT+BLESPP failed");
            HAL_UART_Receive_IT(STEPHANO_UART_PTR, &uart_rx_byte, 1);
            dl_state = DL_STATE_SEND_WSM_BL;
            connect_start = 0;
        }
        process_rx_data();
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

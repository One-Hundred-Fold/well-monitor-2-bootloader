/**
  ******************************************************************************
  * @file    bootloader_download.h
  * @brief   BLE download via Stephano-I: WSM↔PC protocol.
  ******************************************************************************
  */

#ifndef BOOTLOADER_DOWNLOAD_H
#define BOOTLOADER_DOWNLOAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start BLE download. Powers on Stephano, configures WE SPP-like, runs protocol.
   Never returns on success (reboots or jumps). On fatal error, sends dying gasp and reboots. */
void Bootloader_StartDownload(void);

/* Process received data (call from main loop). */
void Bootloader_Download_Process(void);

/* Add received byte (call from HAL_UART_RxCpltCallback). */
void Bootloader_RxByte(uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_DOWNLOAD_H */

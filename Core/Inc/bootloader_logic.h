/**
  ******************************************************************************
  * @file    bootloader_logic.h
  * @brief   Second-stage bootloader logic: sector search, verification, jump.
  ******************************************************************************
  */

#ifndef BOOTLOADER_LOGIC_H
#define BOOTLOADER_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Run the second-stage bootloader.
   - Search sector 6 for app in download state; if found and verified, reboot.
   - Search sector 7 for app in ready state; if found and verified, jump to it.
   - Otherwise, start BLE download (never returns on success). */
void Bootloader_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_LOGIC_H */

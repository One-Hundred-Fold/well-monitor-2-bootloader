/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define STEPHANO_CTS_Pin GPIO_PIN_0
#define STEPHANO_CTS_GPIO_Port GPIOA
#define STEPHANO_RTS_Pin GPIO_PIN_1
#define STEPHANO_RTS_GPIO_Port GPIOA
#define STEPHANO_TX_Pin GPIO_PIN_2
#define STEPHANO_TX_GPIO_Port GPIOA
#define STEPHANO_RX_Pin GPIO_PIN_3
#define STEPHANO_RX_GPIO_Port GPIOA
#define n_STEPHANO_ON_Pin GPIO_PIN_13
#define n_STEPHANO_ON_GPIO_Port GPIOB
#define n_STEPHANO_RST_Pin GPIO_PIN_14
#define n_STEPHANO_RST_GPIO_Port GPIOB
#define EXT_MODEM_TX_Pin GPIO_PIN_9
#define EXT_MODEM_TX_GPIO_Port GPIOA
#define EXT_MODEM_RX_Pin GPIO_PIN_10
#define EXT_MODEM_RX_GPIO_Port GPIOA
#define EXT_MODEM_CTS_Pin GPIO_PIN_11
#define EXT_MODEM_CTS_GPIO_Port GPIOA
#define EXT_MODEM_RTS_Pin GPIO_PIN_12
#define EXT_MODEM_RTS_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define n_3GON_Pin GPIO_PIN_12
#define n_3GON_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */
/* Stephano-I serial port selection: 1 = UART1 (default), 0 = UART2.
   Override from build: -DSTEPHANO_USE_UART1=0 to use UART2. */
#ifndef STEPHANO_USE_UART1
#define STEPHANO_USE_UART1 0
#endif

#if STEPHANO_USE_UART1
extern UART_HandleTypeDef huart1;
#define STEPHANO_UART_PTR  (&huart1)
#else
extern UART_HandleTypeDef huart2;
#define STEPHANO_UART_PTR  (&huart2)
#endif
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

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
#define LED1_Pin GPIO_PIN_9
#define LED1_GPIO_Port GPIOF
#define LED2_Pin GPIO_PIN_10
#define LED2_GPIO_Port GPIOF
#define T_CS_Pin GPIO_PIN_5
#define T_CS_GPIO_Port GPIOC
#define T_CLK_Pin GPIO_PIN_0
#define T_CLK_GPIO_Port GPIOB
#define T_IRQ_Pin GPIO_PIN_1
#define T_IRQ_GPIO_Port GPIOB
#define T_DO_Pin GPIO_PIN_2
#define T_DO_GPIO_Port GPIOB
#define T_DIN_Pin GPIO_PIN_11
#define T_DIN_GPIO_Port GPIOF
#define LCD_DC_Pin GPIO_PIN_11
#define LCD_DC_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_12
#define LCD_RST_GPIO_Port GPIOB
#define LCD_LED_Pin GPIO_PIN_13
#define LCD_LED_GPIO_Port GPIOB
#define W25Q138_CS_Pin GPIO_PIN_14
#define W25Q138_CS_GPIO_Port GPIOB
#define LCD_CS_Pin GPIO_PIN_15
#define LCD_CS_GPIO_Port GPIOB
#define ESP_RST_Pin GPIO_PIN_8
#define ESP_RST_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

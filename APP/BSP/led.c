/**
 * @file    led.c
 * @brief   LED 控制模块 — 实现
 *
 * @note    LED1 (PF9), LED2 (PF10), 低电平点亮
 */

#include "led.h"
#include "main.h"

/*==========================================================================
 * 初始化
 *==========================================================================*/

void LED_Init(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET); /* LED1 熄灭 */
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET); /* LED2 熄灭 */
}

/*==========================================================================
 * LED1 (PF9)
 *==========================================================================*/

void LED_Set(LED_State state)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin,
                      (state == LED_ON) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void LED_Toggle(void)
{
    HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
}

/*==========================================================================
 * LED2 (PF10)
 *==========================================================================*/

void LED2_Set(LED_State state)
{
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin,
                      (state == LED_ON) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void LED2_Toggle(void)
{
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
}

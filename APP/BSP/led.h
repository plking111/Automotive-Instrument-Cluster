/**
 * @file    led.h
 * @brief   LED 控制模块 — 对外接口
 *
 * @note    LED1 — PF9,  LED2 — PF10,  均为低电平点亮
 */

#ifndef __LED_H
#define __LED_H

#include <stdint.h>
#include "gpio.h"

typedef enum
{
    LED_OFF = 0,
    LED_ON = 1
} LED_State;

void LED_Init(void);
void LED_Set(LED_State state);  /* LED1 */
void LED_Toggle(void);          /* LED1 */
void LED2_Set(LED_State state); /* LED2 */
void LED2_Toggle(void);         /* LED2 */

#endif /* __LED_H */

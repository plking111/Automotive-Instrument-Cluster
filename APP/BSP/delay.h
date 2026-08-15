/**
 * @file    delay.h
 * @brief   微秒/毫秒级延时接口
 */

#ifndef __DELAY__
#define __DELAY__

#include <stdint.h>
#include "main.h"
#include "stm32f4xx_hal.h"

void delay_us(uint16_t us); /* 微秒级延时 */
void delay_ms(uint16_t ms); /* 毫秒级延时 */

#endif

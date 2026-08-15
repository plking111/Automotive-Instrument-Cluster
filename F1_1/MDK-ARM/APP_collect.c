#include "APP_collect.h"
#include "usart.h"
#include <stdio.h> /* sscanf */

extern vehicle_data_t g_vehicle;

/* USART1 逐字节中断接收 -> 行缓冲 -> 解析 "speed,rpm,fuel,temp" */
static uint8_t  rx_byte;
static char     line_buf[64];
static uint16_t line_len = 0;

void APP_COLLECT_Init(void)
{
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/**
 * @brief USART1 接收完成回调 (重写 HAL 弱函数)
 *
 * 逐字节累积到 line_buf, 遇到换行 '\n' 解析 "speed,rpm,fuel,temp" 写入 g_vehicle。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    int s, r, f, t;

    if (huart->Instance != USART1)
    {
        return;
    }

    if (rx_byte == '\n')
    {
        line_buf[line_len] = '\0';
        if (sscanf(line_buf, "%d,%d,%d,%d", &s, &r, &f, &t) == 4)
        {
            g_vehicle.speed = (uint16_t)s;
            g_vehicle.rpm   = (uint16_t)r;
            g_vehicle.fuel  = (uint8_t)f;
            g_vehicle.temp  = (uint8_t)t;
        }
        line_len = 0;
    }
    else if (rx_byte != '\r' && line_len < sizeof(line_buf) - 1)
    {
        line_buf[line_len++] = (char)rx_byte;
    }

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/**
 * @file    APP_can.c
 * @brief   CAN 总线接收与解析
 *
 * @note    轮询接收 0x200/0x201, 解析到 g_vehicle
 */

#include "APP_can.h"

#include "can.h"   /* hcan1, MX_CAN1_Init */
#include "usart.h" /* huart2 调试串口 */

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

// #define can_test /* 调试: 串口输入 -> CAN 发送, 接 F1 后屏蔽 */

/* 车辆数据共享实例 (CAN_Task 写, LVGL timer_cb 读) */
vehicle_data_t g_vehicle = {0};

#ifdef can_test /* 调试代码: 接 F1 后屏蔽 */
/* 调试: 串口输入行缓冲 + 单字节中断接收 */
static char dbg_line[32];                   /* 串口输入行缓冲 */
static uint8_t dbg_len = 0;                 /* 已缓冲字节数 */
static uint8_t dbg_rx_byte;                 /* 单字节中断接收缓冲 */
static volatile uint8_t dbg_line_ready = 0; /* 一行就绪 */
#endif

/**
 * @brief CAN 接收初始化 — 配置滤波器 + 启动 CAN1
 */
void APP_CAN_Init(void)
{
    CAN_FilterTypeDef filter;

    /* 正式 CAN 总线: 普通模式 */
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    if (HAL_CAN_Init(&hcan1) != HAL_OK)
    {
        Error_Handler();
    }

    /* ① 配置滤波器: 32位 ID-Mask, 掩码全 0 */
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = CAN_FILTER_ENABLE;
    filter.SlaveStartFilterBank = 14; /* 单 CAN 实例, 需填 */
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
    {
        Error_Handler();
    }

    /* ② 启动 CAN */
    if (HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        Error_Handler();
    }
    printf("[CAN] started, waiting for frames...\r\n");

#ifdef can_test
    /* 启动串口中断接收 */
    HAL_UART_Receive_IT(&huart2, &dbg_rx_byte, 1);
#endif
}

/**
 * @brief CAN 接收轮询 — 读空 FIFO 并解析到 g_vehicle
 */
void APP_CAN_Run(void)
{
    CAN_RxHeaderTypeDef rx;
    uint8_t data[8];

    /* 一次读空 FIFO 积压帧 */
    while (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx, data) == HAL_OK)
    {
        switch (rx.StdId)
        {
        case CAN_ID_VEHICLE: /* 仪表数据 */
            g_vehicle.speed = (uint16_t)(data[0] | (data[1] << 8));
            g_vehicle.rpm = (uint16_t)(data[2] | (data[3] << 8));
            g_vehicle.fuel = data[4];
            g_vehicle.temp = data[5];
            break;

        case CAN_ID_STATUS: /* 图标状态 */
            g_vehicle.icons = data[0];
            break;

        default:
            break;
        }
    }
}

#ifdef can_test /* 调试代码: 接 F1 后屏蔽 */
/*==========================================================================
 * 调试: 串口输入 -> CAN 发送 (回环自测)
 *==========================================================================*/

/**
 * @brief 解析一行输入并发送 0x200/0x201 两帧
 */
static void APP_CAN_DebugSend(const char *line)
{
    int speed, rpm, fuel, temp, icons;
    CAN_TxHeaderTypeDef tx;
    uint32_t mailbox;
    uint8_t data[8];

    if (sscanf(line, "%d,%d,%d,%d,%d", &speed, &rpm, &fuel, &temp, &icons) != 5)
    {
        printf("[CAN] bad input, expect: speed,rpm,fuel,temp,icons\r\n");
        return;
    }

    printf("[CAN] TX: %d,%d,%d,%d,%d\r\n", speed, rpm, fuel, temp, icons);

    /* 0x200 仪表数据帧 */
    tx.StdId = CAN_ID_VEHICLE;
    tx.ExtId = 0;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8;
    tx.TransmitGlobalTime = DISABLE;
    data[0] = (uint8_t)(speed & 0xFF);
    data[1] = (uint8_t)((speed >> 8) & 0xFF);
    data[2] = (uint8_t)(rpm & 0xFF);
    data[3] = (uint8_t)((rpm >> 8) & 0xFF);
    data[4] = (uint8_t)fuel;
    data[5] = (uint8_t)temp;
    data[6] = 0;
    data[7] = 0;
    HAL_CAN_AddTxMessage(&hcan1, &tx, data, &mailbox);

    /* 0x201 图标状态帧 */
    tx.StdId = CAN_ID_STATUS;
    tx.DLC = 1;
    data[0] = (uint8_t)icons;
    HAL_CAN_AddTxMessage(&hcan1, &tx, data, &mailbox);
}

/**
 * @brief UART 接收完成回调 (重写 HAL 弱函数)
 *
 * 逐字节接收 USART2 输入, 累积成行后置就绪标志。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2)
    {
        return;
    }

    if (dbg_rx_byte == '\n' || dbg_rx_byte == '\r')
    {
        if (dbg_len > 0)
        {
            dbg_line[dbg_len] = '\0';
            dbg_len = 0;
            dbg_line_ready = 1; /* 通知 CAN_Task 处理 */
        }
    }
    else if (dbg_len < sizeof(dbg_line) - 1)
    {
        dbg_line[dbg_len++] = (char)dbg_rx_byte;
    }

    /* 继续接收下一字节 */
    HAL_UART_Receive_IT(&huart2, &dbg_rx_byte, 1);
}

/**
 * @brief 调试轮询 — 消费就绪行并发送
 */
void APP_CAN_DebugPoll(void)
{
    if (dbg_line_ready)
    {
        dbg_line_ready = 0;
        APP_CAN_DebugSend(dbg_line);
    }
}
#endif /* 调试代码结束 */

/**
 * @file    APP_can.c
 * @brief   CAN 模块 (F103-A) — 实现
 *
 * 由 APP_can.h 的 CAN_TEST_FLAG 切换两种模式:
 *   [定义]   回环自测: 上位机 -> USART1(RX中断) -> CAN(TX) -> 回环静默 -> CAN(RX) -> USART1(TX)
 *   [未定义] 正常采集: USART1 串口接收 "speed,rpm,fuel,temp" -> g_vehicle -> 0x200 发送给 F407
 */

#include "APP_can.h"

#include "can.h"   /* hcan */
#include "usart.h" /* huart1 */
#include "main.h"  /* Error_Handler */
#include <stdio.h> /* sscanf */

vehicle_data_t g_vehicle = {0};

#ifdef CAN_TEST_FLAG

/* ================= 回环自测 ================= */

/* 串口接收环形缓冲 (中断写 head, 任务读 tail; 单生产者单消费者) */
#define RX_BUF_SIZE 64
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0; /* 中断写位置 */
static volatile uint16_t rx_tail = 0; /* 任务读位置 */

/* USART1 单字节中断接收缓冲 */
static uint8_t rx_byte;

void APP_CAN_Init(void)
{
    CAN_FilterTypeDef filter = {0};

    /* 回环静默: 自发自收, 不驱动总线, 无需收发器 */
    hcan.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
    if (HAL_CAN_Init(&hcan) != HAL_OK)
    {
        Error_Handler();
    }

    /* 接收过滤器: 掩码全 0, 接收所有标准帧 (回环自测) */
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }

    /* 启动 USART1 单字节中断接收 */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/**
 * @brief USART1 接收完成回调 (重写 HAL 弱函数)
 *
 * 每收到一个字节, 入队到环形缓冲; 缓冲满则丢弃该字节 (避免覆盖)。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;

    if (huart->Instance != USART1)
    {
        return;
    }

    next = (uint16_t)(rx_head + 1) % RX_BUF_SIZE;
    if (next != rx_tail) /* 未满才写 */
    {
        rx_buf[rx_head] = rx_byte;
        rx_head = next;
    }

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1); /* 重新武装 */
}

/**
 * @brief CAN 轮询 (由 Can_Task 周期调用)
 *
 * ① 串口收到的字节 -> CAN 发送 (逐个打包成 1 字节帧; 邮箱满则停下留到下周期)
 * ② CAN 回环收到的帧 -> 串口回传
 */
void APP_CAN_Run(void)
{
    CAN_TxHeaderTypeDef tx;
    CAN_RxHeaderTypeDef rxh;
    uint8_t data[8];
    uint32_t mailbox;

    /* ① 串口 -> CAN */
    tx.StdId = CAN_ID_VEHICLE;
    tx.ExtId = 0;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 1;
    tx.TransmitGlobalTime = DISABLE;

    while (rx_tail != rx_head)
    {
        data[0] = rx_buf[rx_tail];
        if (HAL_CAN_AddTxMessage(&hcan, &tx, data, &mailbox) != HAL_OK)
        {
            break; /* 3 个邮箱都满, 剩余字节下个周期再发 */
        }
        rx_tail = (uint16_t)(rx_tail + 1) % RX_BUF_SIZE;
    }

    /* ② CAN -> 串口 */
    while (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxh, data) == HAL_OK)
    {
        HAL_UART_Transmit(&huart1, data, rxh.DLC, 10);
    }
}

#else /* CAN_TEST_FLAG 未定义 */

/* ================= 正常采集: 串口 -> CAN ================= */

/* USART1 逐字节中断接收 -> 行缓冲 -> 解析 */
static uint8_t  rx_byte;
static char     line_buf[64];
static uint16_t line_len = 0;

void APP_CAN_Init(void)
{
    /* HAL_CAN_Init 已在 MX_CAN_Init 完成 (500kbps, NORMAL), 这里只需启动 */
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1); /* 启动串口接收 */
}

/**
 * @brief USART1 接收完成回调 (重写 HAL 弱函数)
 *
 * 逐字节累积到 line_buf, 遇到换行 '\n' 解析 "speed,rpm,fuel,temp" 写入 g_vehicle:
 *   "车速,转速,油箱量,水温\n"  例如 "100,3500,80,90\n"
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

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1); /* 重新武装 */
}

/**
 * @brief CAN 发送 — 打包 g_vehicle 发 0x200
 */
void APP_CAN_Send(void)
{
    CAN_TxHeaderTypeDef tx;
    uint32_t mailbox;
    uint8_t data[8];

    tx.StdId = CAN_ID_VEHICLE;
    tx.ExtId = 0;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8;
    tx.TransmitGlobalTime = DISABLE;
    data[0] = (uint8_t)(g_vehicle.speed & 0xFF);
    data[1] = (uint8_t)((g_vehicle.speed >> 8) & 0xFF);
    data[2] = (uint8_t)(g_vehicle.rpm & 0xFF);
    data[3] = (uint8_t)((g_vehicle.rpm >> 8) & 0xFF);
    data[4] = (uint8_t)g_vehicle.fuel;
    data[5] = (uint8_t)g_vehicle.temp;
    data[6] = 0;
    data[7] = 0;
    HAL_CAN_AddTxMessage(&hcan, &tx, data, &mailbox);
}

#endif /* CAN_TEST_FLAG */

/**
 * @file    APP_can.c
 * @brief   CAN 模块 (F103-B) - 实现
 *
 * 4 个按键 (PA15/PB4/PB6/PB8) 状态机消抖 -> 翻转对应图标 -> 发送 0x201。
 * 按键用轮询电平 + 状态机消抖 (20ms), 替代 EXTI 边沿检测, 更抗抖动。
 * 每键完整周期: 空闲 -> 按下消抖 -> 确认按下(动作一次) -> 释放消抖 -> 空闲。
 *
 * 回环自测 (CAN_TEST_FLAG 定义时):
 *   CAN 配 SILENT_LOOPBACK 自发自收, 串口打印回环接收 [RX ...],
 *   用于隔离问题 (按键 / CAN 收发 / 总线)。
 */

#include "APP_can.h"

#include "can.h"   /* hcan */
#include "main.h"  /* Key1_Pin ... Key4_Pin, Error_Handler */
#include "usart.h" /* huart1 调试串口 */
#include <stdio.h>  /* sprintf */
#include <string.h> /* strlen */

/* 当前图标状态位图 */
static volatile uint8_t g_icons = 0;

/* ------------------------------------------------------------------ */
/* 按键状态机消抖                                                     */
/* ------------------------------------------------------------------ */

/* 按键状态 */
typedef enum {
    KEY_IDLE = 0,     /* 空闲 */
    KEY_PRESS_DBNC,   /* 按下消抖中 */
    KEY_PRESSED,      /* 按下已确认, 等待释放 */
    KEY_RELEASE_DBNC  /* 释放消抖中 */
} key_state_t;

#define KEY_SCAN_MS  1                   /* 扫描周期 ms, 与 Can_Task 的 osDelay(1) 一致 */
#define KEY_DBNC_MS  20                  /* 消抖时长 ms */
#define KEY_DBNC_CNT (KEY_DBNC_MS / KEY_SCAN_MS)

/* 单个按键描述 */
typedef struct {
    GPIO_TypeDef *port;   /* GPIO 端口 */
    uint16_t      pin;    /* GPIO 引脚 */
    uint8_t       icon;   /* 图标位掩码 */
    key_state_t   state;  /* 当前状态 */
    uint8_t       cnt;    /* 消抖计数 */
} key_t;

/* 4 个按键: Key1=PA15(安全带) Key2=PB4(车辆) Key3=PB6(警告) Key4=PB8(制动) */
static key_t g_keys[4] = {
    { Key1_GPIO_Port, Key1_Pin, ICON_SEATBELT, KEY_IDLE, 0 },
    { Key2_GPIO_Port, Key2_Pin, ICON_CAR,      KEY_IDLE, 0 },
    { Key3_GPIO_Port, Key3_Pin, ICON_WARNING,  KEY_IDLE, 0 },
    { Key4_GPIO_Port, Key4_Pin, ICON_BRAKE,    KEY_IDLE, 0 },
};

#ifdef CAN_TEST_FLAG
static void dbg_send(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}
#endif

/**
 * @brief 启动 CAN
 * @note  HAL_CAN_Init 已在 MX_CAN_Init 完成; 回环测试时切换为 SILENT_LOOPBACK
 */
void APP_CAN_Init(void)
{
#ifdef CAN_TEST_FLAG
    CAN_FilterTypeDef filter = {0};

    /* 回环静默: 自发自收, 不驱动总线, 无需收发器 */
    hcan.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
    if (HAL_CAN_Init(&hcan) != HAL_OK)
    {
        Error_Handler();
    }

    /* 接收滤波器: 掩码全 0, 接收所有标准帧 */
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
    {
        Error_Handler();
    }
#endif

    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief 发送当前图标状态 -> 0x201 (DLC=1)
 */
void APP_CAN_SendStatus(void)
{
    CAN_TxHeaderTypeDef tx;
    uint32_t mailbox;
    uint8_t data[1];

    tx.StdId = CAN_ID_STATUS;
    tx.ExtId = 0;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 1;
    tx.TransmitGlobalTime = DISABLE;
    data[0] = g_icons;
    HAL_CAN_AddTxMessage(&hcan, &tx, data, &mailbox);
}

/**
 * @brief 按键处理: 翻转对应图标位 + 发送
 * @param icon_mask 图标位掩码 (ICON_SEATBELT / ICON_CAR / ICON_WARNING / ICON_BRAKE)
 */
void APP_CAN_HandleKey(uint8_t icon_mask)
{
    g_icons ^= icon_mask;   /* toggle 亮/灭 */
    APP_CAN_SendStatus();
}

/**
 * @brief 按键状态机扫描 (每 1ms 调用一次)
 *
 * 轮询 4 个按键电平, 每键独立状态机:
 *   IDLE -> (低) PRESS_DBNC -> (连续低 20ms) PRESSED(动作一次)
 *        -> (高) RELEASE_DBNC -> (连续高 20ms) IDLE
 * 中途电平跳变回退, 杜绝按下/释放抖动导致的误触发与重复触发。
 */
void APP_CAN_KeyScan(void)
{
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        key_t *k = &g_keys[i];
        uint8_t level = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_SET) ? 1u : 0u;

        switch (k->state)
        {
        case KEY_IDLE:
            if (level == 0u)   /* 检测到按下 (低电平) */
            {
                k->state = KEY_PRESS_DBNC;
                k->cnt = 0;
            }
            break;

        case KEY_PRESS_DBNC:
            if (level == 0u)
            {
                if (++k->cnt >= KEY_DBNC_CNT)
                {
                    k->state = KEY_PRESSED;
                    APP_CAN_HandleKey(k->icon);   /* 消抖通过, 确认按下, 动作一次 */
                }
            }
            else
            {
                k->state = KEY_IDLE;   /* 抖动, 回空闲 */
            }
            break;

        case KEY_PRESSED:
            if (level == 1u)   /* 检测到释放 (高电平) */
            {
                k->state = KEY_RELEASE_DBNC;
                k->cnt = 0;
            }
            break;

        case KEY_RELEASE_DBNC:
            if (level == 1u)
            {
                if (++k->cnt >= KEY_DBNC_CNT)
                {
                    k->state = KEY_IDLE;   /* 确认释放, 重新武装 */
                }
            }
            else
            {
                k->state = KEY_PRESSED;    /* 释放抖动, 回按下态 */
            }
            break;
        }
    }
}

#ifdef CAN_TEST_FLAG
/**
 * @brief 回环测试轮询: 读空 FIFO 并打印收到的帧
 */
void APP_CAN_Run(void)
{
    CAN_RxHeaderTypeDef rx;
    uint8_t data[8];
    char buf[48];

    while (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx, data) == HAL_OK)
    {
        sprintf(buf, "[RX id=0x%03X DLC=%d data0=0x%02X]\r\n",
                (unsigned)rx.StdId, (int)rx.DLC, (unsigned)data[0]);
        dbg_send(buf);
    }
}
#endif

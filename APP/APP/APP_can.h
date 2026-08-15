/**
 * @file    APP_can.h
 * @brief   CAN 总线接收模块 — 协议定义 + 共享数据 + 任务接口
 *
 * @note    0x200 (DLC=8): 车速/转速/油量/水温
 *          0x201 (DLC=1): 图标状态位图
 */

#ifndef __APP_CAN_H
#define __APP_CAN_H

#include <stdint.h>

/* CAN 报文 ID */
#define CAN_ID_VEHICLE 0x200 /**< 仪表数据帧 */
#define CAN_ID_STATUS 0x201  /**< 图标状态帧 */

/* 图标位图掩码 */
#define ICON_SEATBELT 0x01 /**< 安全带   */
#define ICON_CAR 0x02      /**< 车辆     */
#define ICON_WARNING 0x04  /**< 黄色警告 */
#define ICON_BRAKE 0x08    /**< 制动     */

/**
 * @brief 车辆数据共享结构体
 *
 * CAN_Task 写, LVGL 定时器读。
 */
typedef struct
{
    volatile uint16_t speed; /**< 车速 km/h        */
    volatile uint16_t rpm;   /**< 转速 rpm 0~8000  */
    volatile uint8_t fuel;   /**< 油量 0~100 %     */
    volatile uint8_t temp;   /**< 水温 0~120 ℃     */
    volatile uint8_t icons;  /**< 图标位图         */
} vehicle_data_t;

extern vehicle_data_t g_vehicle;

/**
 * @brief CAN 接收初始化 — 配置滤波器 + 启动 CAN1
 */
void APP_CAN_Init(void);

/**
 * @brief CAN 接收轮询 — 读空 FIFO 并解析到 g_vehicle
 */
void APP_CAN_Run(void);

// void APP_CAN_DebugPoll(void); /* 调试代码已屏蔽 (接 F1 从机) */

#endif /* __APP_CAN_H */

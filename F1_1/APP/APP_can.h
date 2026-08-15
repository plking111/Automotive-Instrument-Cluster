/**
 * @file    APP_can.h
 * @brief   CAN 模块 (F103-A) — 协议 + 接口
 *
 * 整车 CAN 网络: 2 × F103 + 1 × F407 (500kbps)
 *   F103-A (本节点): 采集转速/速度/油箱量 -> 发送 0x200 仪表数据帧
 *   F103-B (另一块): 采集图标状态     -> 发送 0x201 图标状态帧
 *   F407 (主控):     接收 0x200/0x201, 驱动 LVGL 仪表盘
 *
 * ID 0x200 (标准帧, DLC=8):
 *   Byte0-1: 车速 uint16 km/h (小端, 低字节在前)
 *   Byte2-3: 转速 uint16 rpm 0~8000 (小端)
 *   Byte4:   油箱量 uint8 0~100 %
 *   Byte5:   水温 uint8 0~120 ℃
 *   Byte6-7: 保留 (填 0)
 */

#ifndef __APP_CAN_H
#define __APP_CAN_H

#include <stdint.h>

/* 回环自测开关: 定义 = 启用自测(串口 -> CAN -> 回传); 注释掉 = 正常采集发送 0x200 */
//#define CAN_TEST_FLAG

/* CAN 报文 ID */
#define CAN_ID_VEHICLE 0x200 /**< 仪表数据帧 (本节点发送) */

/* 车辆数据共享结构体 (采集代码写, CAN_Task 读) */
typedef struct
{
    volatile uint16_t speed; /**< 车速 km/h        */
    volatile uint16_t rpm;   /**< 转速 rpm 0~8000  */
    volatile uint8_t  fuel;  /**< 油箱量 0~100 %   */
    volatile uint8_t  temp;  /**< 水温 0~120 ℃     */
} vehicle_data_t;

extern vehicle_data_t g_vehicle;

void APP_CAN_Init(void);

#ifdef CAN_TEST_FLAG
void APP_CAN_Run(void);   /**< 自测: 串口字节 -> CAN -> 回环 -> 串口回传 */
#else
void APP_CAN_Send(void);  /**< 正常: 打包 g_vehicle -> 0x200 发出 */
#endif

#endif /* __APP_CAN_H */

/**
 * @file    APP_can.h
 * @brief   CAN 模块 (F103-B) - 协议 + 接口
 *
 * 整车 CAN 网络: 2 x F103 + 1 x F407 (500kbps)
 *   F103-A (F1_1):  采集转速/速度/油箱量 -> 发送 0x200 仪表数据帧
 *   F103-B (本节点 F1_2): 4 按键状态机消抖 -> 切换图标 -> 发送 0x201 图标状态帧
 *   F407 (主控):    接收 0x200/0x201, 驱动 LVGL 仪表盘
 *
 * ID 0x201 (标准帧, DLC=1):
 *   Byte0: 图标位图
 *     bit0 = 安全带, bit1 = 车辆, bit2 = 黄色警告, bit3 = 制动
 *     1 = 亮起 / 0 = 熄灭
 *
 * 按键: 轮询电平 + 状态机消抖 (20ms), 每键完整 按下->动作->释放 周期, 抗抖动。
 *
 * 回环自测开关 CAN_TEST_FLAG:
 *   定义   = CAN 自发自收回环测试 (串口打印 [RX] 调试信息, 不驱动总线)
 *   注释掉 = 正常发送 0x201 给 F407
 */

#ifndef __APP_CAN_H
#define __APP_CAN_H

#include <stdint.h>

/* 回环自测开关: 调试时定义, 正式联调时注释掉 */
//#define CAN_TEST_FLAG

/* CAN 报文 ID */
#define CAN_ID_STATUS 0x201 /**< 图标状态帧 (本节点发送) */

/* 图标位图掩码 (与 F407 APP_can.h 保持一致) */
#define ICON_SEATBELT 0x01 /**< 安全带   */
#define ICON_CAR      0x02 /**< 车辆     */
#define ICON_WARNING  0x04 /**< 黄色警告 */
#define ICON_BRAKE    0x08 /**< 制动     */

void APP_CAN_Init(void);                   /**< 启动 CAN (回环测试时配 SILENT_LOOPBACK) */
void APP_CAN_SendStatus(void);             /**< 发送当前图标状态 -> 0x201 */
void APP_CAN_HandleKey(uint8_t icon_mask); /**< 按键处理: 翻转图标位 + 发送 */
void APP_CAN_KeyScan(void);                /**< 按键状态机扫描 (1ms 轮询消抖) */

#ifdef CAN_TEST_FLAG
void APP_CAN_Run(void); /**< 回环测试: 轮询接收 + 串口打印 */
#endif

#endif /* __APP_CAN_H */

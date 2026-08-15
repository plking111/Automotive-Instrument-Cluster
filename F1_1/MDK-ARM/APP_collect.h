#ifndef __APP_COLLECT_H
#define __APP_COLLECT_H

#include "APP_can.h" /* vehicle_data_t */

/* 启动 USART1 串口接收 (由 APP_CAN_Init 调用) */
void APP_COLLECT_Init(void);

#endif

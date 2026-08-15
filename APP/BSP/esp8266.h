/**
 * @file    esp8266.h
 * @brief   ESP8266 WiFi模块驱动 (AT指令 + UART6 DMA空闲中断)
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f4xx_hal.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 用户配置 */
#define BUFF_SIZE 4096 /* UART6 DMA接收缓冲区 (需>1460的TCP MSS) */

/* ESP8266 AT指令返回状态 */
typedef enum
{
    ESP_OK = 0,     /* 成功，收到预期应答 */
    ESP_ERROR = 1,  /* 失败，硬件错误 */
    ESP_TIMEOUT = 2 /* 超时，未收到应答 */
} ESP_ACK;

/* 全局标志：TCP数据到达 (ISR置位，任务轮询) */
extern volatile uint32_t idle_isr_cnt;   /* 调试: IDLE ISR触发次数 */
extern volatile uint16_t esp_rx_len;     /* 最新一帧字节数 (ISR填充) */
extern volatile uint8_t esp_rx_complete; /* DMA IDLE帧完成标志 */
extern volatile uint8_t tcp_rx_flag;
extern volatile uint16_t TCP_DataLength; /* +IPD数据载荷长度 (ISR填充) */
extern char TCPdata[4096];               /* +IPD数据载荷内容 (需匹配BUFF_SIZE) */
extern uint8_t ESP_Buff[4096];           /* DMA接收原始缓冲区 (需匹配BUFF_SIZE) */

/* 函数声明 */
void Start_Recv(void);
void ESP_REST(void);
ESP_ACK ESP_SendCmd(const char *cmd, const char *ack, uint32_t timeout);

/*==========================================================================
 * TCP 应用层 (WiFi连接 / TCP连接 / 数据收发)
 *==========================================================================*/
ESP_ACK ESP_Connect_Wifi(const char *ssid, const char *pass);
ESP_ACK ESP_Connect_TCP(const char *ServerIP, uint16_t ServerPort, uint32_t timeout);
ESP_ACK ESP_SendToTCPServer(const char *txData, uint32_t timeout);
void ESP_ReveToTVPServer(void);

#endif

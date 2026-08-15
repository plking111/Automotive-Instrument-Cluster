/**
 * @file    bootloader.h
 * @brief   BootLoader 固件升级模块 — 对外接口
 *
 * ===========================================================================
 * 职责
 * ===========================================================================
 *   读取 W25Q128 OTA 标志区, 判断是否需要搬运固件:
 *     - upgrade_flag == READY     → 将 Zone1 固件写入 STM32 APP Flash
 *     - upgrade_flag == COMMITTED → 跳过, 直接跳转 APP
 *     - 其他                      → 直接跳转 APP
 *
 * ===========================================================================
 * 使用方式
 * ===========================================================================
 *   在 Boot 项目的 main.c 中, 外设初始化完成后调用:
 *
 *     int main(void)
 *     {
 *         HAL_Init();
 *         SystemClock_Config();
 *         MX_GPIO_Init();
 *         MX_DMA_Init();
 *         MX_USART2_UART_Init();
 *         MX_SPI1_Init();
 *
 *         BootLoader_Run();   // 阻塞, 不会返回
 *
 *         while (1) {}
 *     }
 */

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include <stdint.h>

/**
 * @brief  执行 BootLoader 主流程 (阻塞, 不会返回)
 *
 * 内部流程:
 *   [1] 初始化 W25Q128
 *   [2] 读取并打印全部 OTA 标志区内容
 *   [3] 根据 upgrade_flag 分流:
 *        READY → 验证固件头 → 擦除 APP Flash → 搬运 Zone1 → 验证 → 设 COMMITTED → 重启
 *        其他  → Jump_to_APP()
 */
void BootLoader_Run(void);

/**
 * @brief  跳转到 APP 固件 (0x08010000)
 * @param  app_addr APP 起始地址 (通常为 0x08010000)
 * @note   复位外设, 设 MSP/PC, 不返回
 */
void Jump_to_APP(uint32_t app_addr);

#endif /* __BOOTLOADER_H */

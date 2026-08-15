/**
 * @file    APP_ota.h
 * @brief   OTA 固件升级应用层 — 对外接口
 *
 * @note    供 freertos.c 的 OTA_Task 调用
 */

#ifndef __APP_OTA_H
#define __APP_OTA_H

#include <stdint.h>

/**
 * @brief 执行完整 OTA 升级流程 (阻塞, 一次性)
 *
 * 等待网络 -> 接管 ESP8266 -> 版本同步 -> 标志自检 ->
 * 状态机分流 -> 恢复网络。
 */
void APP_OTA_Run(void);

/**
 * @brief 由下载按钮触发, 执行固件下载
 *
 * 下载到 Zone1, 成功重启进 BootLoader, 失败恢复 MQTT。
 */
void APP_OTA_StartDownload(void);

extern volatile uint8_t ota_busy;               /**< 下载忙标志 */
extern volatile uint8_t ota_download_requested; /**< 用户下载请求标志 */
extern volatile uint8_t g_ota_stop_feed_dog;    /**< 下载完成停止喂狗标志 */

/**
 * @brief 请求开始下载 (非阻塞, 立即返回)
 */
void APP_OTA_RequestDownload(void);

#endif /* __APP_OTA_H */

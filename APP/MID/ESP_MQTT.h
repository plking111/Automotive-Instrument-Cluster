/**
 * @file    ESP_MQTT.h
 * @brief   OneNET MQTT 客户端 (ESP8266 AT 指令)
 *
 * @note    提供连接/上报/订阅/断开等 MQTT 接口
 */

#ifndef __ESP_MQTT_H
#define __ESP_MQTT_H

#include "esp8266.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*=== OneNET MQTT 设备配置区（可外部修改） ===*/
#define MQTT_BROKER_URL "mqtts.heclouds.com" /**< MQTT 服务器域名 */
#define MQTT_BROKER_PORT 1883                /**< MQTT 服务器端口 */
#define MQTT_PRODUCT_ID "YourProductID"      /**< 你的 OneNET 产品 ID */
#define MQTT_DEVICE_NAME "YourDeviceName"    /**< 你的 OneNET 设备名称 */
#define MQTT_AUTH_PARAM "YourAuthParam"      /**< 你的 OneNET 鉴权串 */

/*=== OneNET 标准主题宏 ===*/
#define MQTT_TOPIC_POST "$sys/%s/%s/thing/property/post"             /**< 设备属性上报 */
#define MQTT_TOPIC_POST_REPLY "$sys/%s/%s/thing/property/post/reply" /**< 平台回复上报结果 */
#define MQTT_TOPIC_PROP_SET "$sys/%s/%s/thing/property/set"          /**< 平台下发控制指令 */

#define USER_SSID "YourSSID"     /**< 你的WiFi名称 */
#define USER_PASS "YourPassword" /**< 你的WiFi密码 */

/*=== MQTT 状态枚举 ===*/
typedef enum
{
    MQTT_STATE_IDLE = 0,      /**< 空闲未连接 */
    MQTT_STATE_CFG_READY = 1, /**< MQTT参数配置完成 */
    MQTT_STATE_CONNECTED = 2, /**< MQTT服务器连接成功 */
    MQTT_STATE_SUB_OK = 3,    /**< 主题订阅完成 */
    MQTT_STATE_ERR = 4        /**< MQTT异常断开 */
} MQTT_CONN_STATE;

/*=== MQTT 句柄结构体 ===*/
typedef struct
{
    char product_id[64];
    char dev_name[64];
    char auth_param[256]; /**< OneNET MQTT 完整鉴权串 (含 sign token) */
    char broker[64];
    uint16_t port;
    MQTT_CONN_STATE state;
} ESP_MQTT_HandleTypeDef;

// 全局MQTT实例
extern ESP_MQTT_HandleTypeDef hmqtt;

/*=== 对外API函数声明 ===*/
/**
 * @brief 初始化MQTT句柄，填入OneNET设备参数
 */
void ESP_MQTT_InitParam(void);

/**
 * @brief 完整流程：MQTT参数配置 + 连接服务器 + 订阅双主题
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_Connect(uint32_t timeout);

/**
 * @brief 上报设备属性到OneNET平台
 * @param json_data 待发送JSON字符串
 * @param timeout 超时时间
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_PublishProperty(const char *json_data, uint32_t timeout);

/**
 * @brief MQTT消息解析处理函数（任务循环调用）
 * @note 从TCPdata缓冲区解析平台下发的MQTT指令
 */
void ESP_MQTT_MsgProcess(void);

/**
 * @brief 断开MQTT连接
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_Disconnect(uint32_t timeout);

#endif /* __ESP_MQTT_H */

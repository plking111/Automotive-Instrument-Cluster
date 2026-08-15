/**
 * @file    ota_http.h
 * @brief   OneNET OTA HTTP 通信模块 — 基于新 API (iot-api.heclouds.com)
 *
 * ===========================================================================
 * 工作流程 (3 步)
 * ===========================================================================
 *   Step 1: OTA_ReportVersion()  — POST 上报当前版本号
 *   Step 2: OTA_CheckTask()      — GET  查询是否有升级任务
 *   Step 3: OTA_DownloadFirmware() — GET  分片下载固件 -> W25Q128 Zone1
 *
 * ===========================================================================
 * 依赖
 * ===========================================================================
 *   通信:  ESP8266 AT 指令 (esp8266.h)
 *   存储:  W25Q128 外部 Flash (w25q128.h)
 *   标志:  OTA 标志区管理 (ota_flags.h)
 *   校验:  MD5 摘要算法    (md5.h)
 *   系统:  FreeRTOS + CMSIS-RTOS v2
 */

#ifndef __OTA_HTTP_H
#define __OTA_HTTP_H

#include "esp8266.h"
#include "ota_flags.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*==========================================================================
 * 1. OneNET 平台配置
 *==========================================================================*/

/** @brief OTA 服务器地址 (新 API, HTTP 明文) */
#define OTA_SERVER_IP "iot-api.heclouds.com"
#define OTA_SERVER_PORT 80

/** @brief OneNET 产品/设备/用户标识 */
#define OTA_PRODUCT_ID "YourProductID"   /**< 你的 OneNET 产品 ID */
#define OTA_DEVICE_NAME "YourDeviceName" /**< 你的 OneNET 设备名称 */
#define OTA_USER_ID "YourUserID"         /**< 你的 OneNET 用户 ID (用于鉴权 token) */

/**
 * @brief 固定鉴权 token
 * @note  格式: version=2022-05-01&res=userid%2F{uid}&et={expire}&method=sha1&sign={sig}
 *        至于token的生成, 参考 OneNET OTA 文档, 或者b站上面也有教学
 *        我不使用动态 HMAC-SHA1, 直接硬编码固定值
 */
#define OTA_AUTHORIZATION "YourAuthorizationToken" /**< 你的 OneNET 固定鉴权 token (含 sign) */

/*==========================================================================
 * 2. 当前固件版本号
 *    (每次发布新固件时必须同步修改)
 *==========================================================================*/

/**
 * @brief  版本格式: V<主版本>.<次版本>.<修订号>
 *
 * 每次新固件的版本在这里修改
 *
 * 使用示例:
 *   OTA_VERSION_STR = "V1.0.0"   -> 上报给服务器 (带 V 前缀)
 *   OTA_VERSION_NUM = "1.0.0"    -> 用于 check API 查询参数 (无 V 前缀)
 *
 * 版本比较规则 (OTA_CompareVersion):
 *   逐段比较 major -> minor -> patch
 *   V2.0.0 > V1.9.9  -> 升级
 *   V1.0.0 > V0.9.9  -> 升级
 */

#define OTA_VERSION_MAJOR 1 /**< 主版本号 (不兼容的大改动)   */
#define OTA_VERSION_MINOR 0 /**< 次版本号 (兼容的功能新增)   */
#define OTA_VERSION_PATCH 0 /**< 修订号   (Bug 修复)       */

/** @brief 带 V 前缀的完整版本字符串, POST /version 接口使用 */
#define OTA_VERSION_STR "V1.0.0"

/** @brief 纯数字版本号, GET /check 接口查询参数使用 */
#define OTA_VERSION_NUM "1.0.0"

/*==========================================================================
 * 3. OTA 操作结果枚举
 *==========================================================================*/

typedef enum
{
    OTA_OK = 0,           /**< 操作成功                  */
    OTA_ERR_TCP = 1,      /**< TCP 连接失败 (服务器/网络)  */
    OTA_ERR_SEND = 2,     /**< HTTP 请求发送失败          */
    OTA_ERR_TIMEOUT = 3,  /**< 等待应答超时               */
    OTA_ERR_NO_TASK = 4,  /**< 无升级任务 (正常情况)       */
    OTA_ERR_PARSE = 5,    /**< 应答 JSON 解析失败          */
    OTA_ERR_DOWNLOAD = 6, /**< 固件下载失败 (分片/校验)    */
    OTA_ERR_FLASH = 7     /**< Flash 写入/读取失败         */
} OTA_Result;

/** @brief 将 OTA_Result 枚举转为可读字符串 */
static inline const char *OTA_ResultStr(OTA_Result r)
{
    switch (r)
    {
    case OTA_OK:
        return "OK";
    case OTA_ERR_TCP:
        return "TCP_FAIL";
    case OTA_ERR_SEND:
        return "SEND_FAIL";
    case OTA_ERR_TIMEOUT:
        return "TIMEOUT";
    case OTA_ERR_NO_TASK:
        return "NO_TASK";
    case OTA_ERR_PARSE:
        return "PARSE_FAIL";
    case OTA_ERR_DOWNLOAD:
        return "DOWNLOAD_FAIL";
    case OTA_ERR_FLASH:
        return "FLASH_FAIL";
    default:
        return "UNKNOWN";
    }
}

/*==========================================================================
 * 4. 升级任务信息结构体 (从 check 接口 JSON 解析)
 *==========================================================================*/

typedef struct
{
    char target_ver[24]; /**< 目标固件版本号, 如 "V2.0.0"             */
    uint32_t tid;        /**< 升级任务 ID (下载时必须提供)             */
    uint32_t size;       /**< 固件文件大小 (字节)                     */
    char md5[33];        /**< 固件 MD5 校验值 (32 字符 hex + '\0')    */
    uint8_t type;        /**< 升级类型: 1=全量升级, 2=差分升级         */
    uint8_t valid;       /**< 数据有效性标志: 1=解析成功, 0=无效       */
} OTA_TaskInfo;

/** @brief 全局任务信息实例, OTA_CheckTask() 填充, OTA_DownloadFirmware() 使用 */
extern OTA_TaskInfo ota_task;

/**
 * @brief OTA 下载进度 (0~100), OTA_DownloadFirmware() 更新, LVGL 进度条读取
 *
 * 在分片下载循环中每完成一片就更新一次, 值域 0~100。
 * 无需同步锁 — OTA_Task 写, LVGL 定时器读, 单字节读写是原子的。
 */
extern volatile uint8_t ota_download_progress;

/*==========================================================================
 * 5. API 函数声明
 *==========================================================================*/

/**
 * @brief  Step 1+2 合并: 单次TCP连接完成版本上报 + 任务查询
 *
 * 建立一次 TCP 连接, 依次发送:
 *   1. POST /version   -> 上报版本
 *   2. GET  /check     -> 查询升级任务
 *
 * 比分别调用 OTA_ReportVersion + OTA_CheckTask 少一次 TCP 握手,
 * 大幅降低因频繁连接/断开导致的失败概率。
 *
 * @return OTA_OK           有升级任务, ota_task.valid=1
 *         OTA_ERR_NO_TASK  无升级任务
 *         OTA_ERR_TCP      连接失败
 *         OTA_ERR_TIMEOUT  应答超时
 *         其他             网络/解析错误
 */
OTA_Result OTA_ReportAndCheck(void);

/**
 * @brief  Step 1: 上报设备版本号到 OneNET OTA 服务器
 *
 * HTTP 请求:
 *   POST /fuse-ota/{pid}/{dev_name}/version
 *   Body: {"s_version":"V1.0.0", "f_version":"V1.0.0"}
 *
 * @return OTA_OK / OTA_ERR_TCP / OTA_ERR_TIMEOUT / OTA_ERR_PARSE
 */
OTA_Result OTA_ReportVersion(void);

/**
 * @brief  Step 2: 检查 OneNET 平台是否有待执行的升级任务
 *
 * HTTP 请求:
 *   GET /fuse-ota/{pid}/{dev_name}/check?type=2&version=1.0.0
 *
 * 解析返回的 JSON 到全局 ota_task:
 *   data.target -> target_ver  (目标版本号)
 *   data.tid    -> tid         (任务 ID)
 *   data.size   -> size        (固件大小)
 *   data.md5    -> md5         (MD5 校验值)
 *   data.type   -> type        (1=全量 2=差分)
 *
 * @return OTA_OK           — 有任务, ota_task.valid=1
 *         OTA_ERR_NO_TASK  — 无任务 (code:12012 或 msg:"not exist")
 *         OTA_ERR_PARSE    — 应答格式异常
 *         其他              — 网络/连接错误
 */
OTA_Result OTA_CheckTask(void);

/**
 * @brief  Step 3: 分片下载固件并写入 W25Q128 Zone1 (0x000000~0x5FFFFF)
 *
 * HTTP 请求 (Keep-Alive 复用 TCP 连接):
 *   GET /fuse-ota/{pid}/{dev_name}/{tid}/download
 *   Header: Range:bytes={offset}-{offset+range_size-1}
 *   Header: Connection:keep-alive
 *
 * 特性:
 *   - Keep-Alive:  整个下载过程仅建立 1 次 TCP 连接
 *   - 断点续传:    通过 Flash 标志区保存进度, 重启后自动续传
 *   - 分片重试:    每个分片最多重试 3 次, 失败自动重连
 *   - MD5 校验:    下载完成后整体校验, 不匹配则清除记录
 *
 * @param  range_size 每片字节数 (推荐 3584, HTTP头+数据 < ESP_Buff 4096)
 * @return OTA_OK            — 下载+校验成功, 已设 UPGRADE_MAGIC_READY
 *         OTA_ERR_DOWNLOAD  — 下载/校验失败
 *         其他               — 网络/连接错误
 */
OTA_Result OTA_DownloadFirmware(uint16_t range_size);

/**
 * @brief  退出数据模式并进入命令模式 (UART 刷新 + +++ + AT)
 *
 * 仅确保 ESP8266 处于命令模式, 不操作 TCP/MQTT。
 * 用于需要在命令模式执行其他指令 (如 MQTTCLEAN) 的场景。
 */
void OTA_ESP_EnterCmdMode(void);

/**
 * @brief  关闭所有 TCP 连接并设单连接模式
 *
 * 调用前必须已释放 MQTT 会话 (ESP_MQTT_Disconnect),
 * 否则 MQTT 持有的 TCP 可能拒绝关闭。
 */
void OTA_ESP_CloseAllTCP(void);

/**
 * @brief  彻底重置 ESP8266 为 OTA 原始 TCP 做准备
 *
 * 等同于 OTA_ESP_EnterCmdMode() + OTA_ESP_CloseAllTCP()，
 * 用于无需在两步之间插入 MQTT 释放的场景 (如周期性检查中
 * 已经调用了 ESP_MQTT_Disconnect 之后)。
 */
void OTA_ESP_Cleanup(void);

/**
 * @brief  断开 OTA TCP 连接 (发送 AT+CIPCLOSE)
 */
void OTA_Disconnect(void);

/**
 * @brief  从 W25Q128 Zone1 回读固件并计算 MD5, 与预期值对比
 *
 * 使用场景:
 *   1. 下载完成后自动调用, 确保固件完整
 *   2. 重启时如果 upgrade_flag==READY, 本地校验避免无效搬运
 *
 * @param  total_size   固件总大小 (字节), 通常来自 ota_task.size
 * @param  expected_md5 预期 MD5 (32 字符 hex 字符串)
 * @return 1 = MD5 匹配, 固件完整
 *         0 = MD5 不匹配, 固件可能损坏
 */
int OTA_VerifyFirmware(uint32_t total_size, const char *expected_md5);

/**
 * @brief  版本号比较 — 判断是否需要升级
 *
 * 支持的格式:
 *   "V1.0.0"  "v2.3.1"  "1.0.0"  "0.9.5"
 *
 * @param  new_ver 服务器下发的目标版本
 * @param  cur_ver 设备当前运行的版本
 * @return  1 = new > cur  -> 需要升级
 *          0 = 版本相同   -> 无需操作
 *         -1 = new < cur  -> 目标比当前还旧 (异常)
 */
int OTA_CompareVersion(const char *new_ver, const char *cur_ver);

#endif /* __OTA_HTTP_H */

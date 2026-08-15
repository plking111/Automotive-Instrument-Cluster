/**
 * @file    ota_http.c
 * @brief   OneNET OTA HTTP 通信模块 — 完整实现
 *
 * ===========================================================================
 * 通信原理
 * ===========================================================================
 *   本模块通过 ESP8266 AT 指令发起原始 TCP 连接, 手动构造 HTTP/1.1 请求,
 *   不使用任何第三方 HTTP 库。ESP8266 工作在透传模式下的单连接模式 (CIPMUX=0)。
 *
 *   数据流:
 *     STM32 UART6 TX -> ESP8266 -> WiFi -> OneNET 服务器
 *     OneNET 服务器 -> WiFi -> ESP8266 -> STM32 UART6 RX (DMA IDLE 中断)
 *
 *   接收机制:
 *     UART6 的 DMA IDLE 中断在检测到帧间隔时触发,
 *     HAL_UARTEx_RxEventCallback() 调用 parse_ipd() 从 ESP_Buff 中
 *     提取 +IPD,<len>:<data> 格式的数据到 TCPdata[], 并置位 tcp_rx_flag。
 *     本模块通过轮询 tcp_rx_flag 等待应答。
 *
 *   关键全局变量 (定义在 esp8266.c):
 *     - TCPdata[4096]   : 存放解析后的 TCP 载荷数据
 *     - TCP_DataLength  : 载荷数据的实际字节数 (ISR 填充)
 *     - tcp_rx_flag     : 数据到达标志 (ISR 置 1, 任务清 0)
 *     - ESP_Buff[4096]  : DMA 原始接收缓冲区 (含 +IPD 头)
 *
 * ===========================================================================
 * OTA 三步流程
 * ===========================================================================
 *   [1] OTA_ReportVersion()     POST 上报版本, 服务器确认设备在线
 *   [2] OTA_CheckTask()         GET  查询任务, 解析 target/tid/size/md5
 *   [3] OTA_DownloadFirmware()  GET  分片下载 + MD5 校验 + 设 READY 标志
 *
 * ===========================================================================
 * W25Q128 分区布局 (16MB)
 * ===========================================================================
 *   Zone1 (下载区): 0x000000 ~ 0x5FFFFF (6MB) — OTA 固件下载到这里
 *   Zone2 (备份区): 0x600000 ~ 0xBFFFFF (6MB) — BootLoader 备份原固件
 *   Zone3 (标志区): 0xC00000 ~ 0xC0FFFF (64KB) — OTA 标志/版本/进度
 */

#include "ota_http.h"
#include "md5.h"

/* ESP8266 通信串口, 用于 +++ 转义序列等底层操作 */
extern UART_HandleTypeDef huart6;

/*==========================================================================
 * 全局变量
 *==========================================================================*/

/** @brief OTA 任务信息, OTA_CheckTask() 填充, OTA_DownloadFirmware() 使用 */
OTA_TaskInfo ota_task = {0};

/**
 * @brief OTA 下载进度 (0~100), OTA_DownloadFirmware() 更新, LVGL 定时器读取
 *
 * 在 ota_http.h 中声明为 extern, 供 APP_lv_gui.c 的进度条定时器使用。
 * 不需要 FreeRTOS 同步 — 单写入者 (OTA_Task) + 单读取者 (LVGL 定时器)。
 */
volatile uint8_t ota_download_progress = 0;

/*==========================================================================
 * 内部常量 (不对外暴露)
 *==========================================================================*/

/** @brief TCP 连接重试次数 */
#define TCP_RETRY_MAX 10

/** @brief 分片下载重试次数 */
#define CHUNK_RETRY_MAX 10

/** @brief HTTP 请求默认超时 (ms) */
#define HTTP_TIMEOUT_DEF 10000

/** @brief HTTP 请求下载超时 (ms) — 含 3.5KB 数据传输, 需更长 */
#define HTTP_TIMEOUT_DOWN 15000

/** @brief W25Q128 单扇区大小 */
#define FLASH_SECTOR_SIZE 4096

/*==========================================================================
 * 内部函数声明
 *==========================================================================*/

static OTA_Result OTA_TCP_Connect(void);
static OTA_Result OTA_SendRequest(const char *request, uint32_t timeout_ms);

/*==========================================================================
 * TCP 连接管理
 *==========================================================================*/

/**
 * @brief  退出数据模式并进入 AT 命令模式
 *
 * 步骤:
 *   [0] UART 排空: 停止 DMA -> 清除帧错误标志 -> 排空 RX FIFO
 *   [1] 发送 +++ 转义序列 (前后各 1 秒静默, 满足 ESP8266 时序要求)
 *   [2] 发送 AT 确认已回到命令模式
 *
 * @note  与 OTA_ESP_Cleanup() 的区别: 不关闭 TCP, 不设置 CIPMUX。
 *        用于需要先确保命令模式再执行其他 AT 指令 (如 MQTTCLEAN) 的场景。
 */
void OTA_ESP_EnterCmdMode(void)
{
    /* 彻底清空 UART: 停止DMA -> 清标志 -> 排空RX FIFO */
    HAL_UART_AbortReceive(&huart6);
    if (huart6.Instance)
    {
        __HAL_UART_CLEAR_PEFLAG(&huart6);
    }
    while (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_RXNE))
    {
        (void)READ_REG(huart6.Instance->DR);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    HAL_UART_Transmit(&huart6, (uint8_t *)"+++", 3, 100);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_SendCmd("AT\r\n", "OK", 2000);
    vTaskDelay(pdMS_TO_TICKS(200));
}

/**
 * @brief  关闭所有 TCP 连接并设单连接模式
 *
 * 必须在 MQTT 会话已释放、ESP 处于命令模式时调用。
 * 使用多连接格式 AT+CIPCLOSE=<id> 逐个关闭,
 * 因为 MQTT 内部使用 CIPMUX=1。
 */
void OTA_ESP_CloseAllTCP(void)
{
    for (int id = 0; id < 5; id++)
    {
        char cmd[16];
        sprintf(cmd, "AT+CIPCLOSE=%d\r\n", id);
        ESP_ACK ack = ESP_SendCmd(cmd, "OK", 1000);
        if (ack != ESP_OK)
        {
            // printf("[OTA] CIPCLOSE=%d fail (ack=%d)\r\n", id, (int)ack);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* CIPMUX=0 重试3次 */
    ESP_ACK ack;
    for (int i = 0; i < 3; i++)
    {
        ack = ESP_SendCmd("AT+CIPMUX=0\r\n", "OK", 2000);
        if (ack == ESP_OK)
            break;
        // printf("[OTA] CIPMUX=0 retry %d/3 (ack=%d)\r\n", i + 1, (int)ack);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief  彻底重置 ESP8266 为 OTA 原始 TCP 做准备
 *
 * 完整清理流程:
 *   [0] 刷新 UART6 FE/NE/ORE 标志
 *   [1] +++ 退出数据透传模式
 *   [2] AT 确认命令模式
 *   [3] AT+CIPCLOSE=<id> 逐个关闭所有 TCP (多连接格式, 兼容 MQTT 的 CIPMUX=1)
 *   [4] AT+CIPMUX=0 切换单连接模式
 *
 * @note  调用方应在本函数之前先调用 ESP_MQTT_Disconnect() 释放 MQTT 会话,
 *        否则 MQTT 持有的 TCP 连接可能拒绝关闭, 导致 CIPMUX 切换失败。
 *        推荐调用顺序:
 *          OTA_ESP_EnterCmdMode()     -> 确保命令模式
 *          ESP_MQTT_Disconnect()      -> 释放 MQTT 会话
 *          OTA_ESP_CloseAllTCP()      -> 关闭 TCP + 设 CIPMUX=0
 */
void OTA_ESP_Cleanup(void)
{
    OTA_ESP_EnterCmdMode();
    OTA_ESP_CloseAllTCP();
}

/**
 * @brief  通过 ESP8266 连接 OTA 服务器
 *
 * 执行步骤:
 *   1. OTA_ESP_Cleanup()  -> 退出数据模式 + UART 刷新 + 关闭旧连接 + 设置 CIPMUX=0
 *   2. AT+CIPSTART -> 发起 TCP 连接 (最多重试 3 次)
 *
 * @return OTA_OK        连接成功
 *         OTA_ERR_TCP   3 次重试均失败
 */
static OTA_Result OTA_TCP_Connect(void)
{
    /*
     * 先强制关闭旧 TCP 连接, 处理三种场景:
     *   ① 上次连接还活着但服务端已关闭 -> ESP8266 不知道, AT+CIPCLOSE 通知它
     *   ② 上次连接已死 (zombie)      -> AT+CIPCLOSE 清理状态
     *   ③ 没有连接                   -> AT+CIPCLOSE 返回 ERROR, 无害
     * ack="O" 同时匹配 OK 和 ERROR, 确保不超时等待。
     */
    ESP_SendCmd("AT+CIPCLOSE\r\n", "O", 1000);
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int i = 0; i < TCP_RETRY_MAX; i++)
    {
        ESP_ACK ack = ESP_Connect_TCP(OTA_SERVER_IP, OTA_SERVER_PORT, 10000);
        if (ack == ESP_OK)
        {
            // printf("[OTA] TCP connected OK\r\n");
            return OTA_OK;
        }
        // printf("[OTA] TCP retry %d/%d fail (ack=%d)\r\n", i + 1, TCP_RETRY_MAX, (int)ack);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("[OTA] TCP connect FAILED\r\n");
    return OTA_ERR_TCP;
}

/**
 * @brief  断开 OTA TCP 连接
 *
 * @note   在整个下载过程中, 正常分片不调用此函数 (Keep-Alive 复用连接),
 *         仅在最终完成或发生不可恢复错误时调用。
 */
void OTA_Disconnect(void)
{
    /* ack="O" 同时匹配 OK 和 ERROR, 无论连接是否存活都能快速返回 */
    ESP_SendCmd("AT+CIPCLOSE\r\n", "O", 1000);
}

/*==========================================================================
 * HTTP 请求发送 & 应答接收 (核心引擎)
 *==========================================================================*/

/**
 * @brief  通过 ESP8266 发送 HTTP 请求, 并等待应答
 *
 * 工作流程:
 *   [1] 清空 TCPdata / tcp_rx_flag, 启动 DMA 接收
 *   [2] 调用 ESP_SendToTCPServer() 发送 AT+CIPSEND + 数据
 *   [3] 重新启动 DMA (ESP_SendToTCPServer 内部会干扰 DMA 状态)
 *   [4] 轮询 tcp_rx_flag, 等待 ISR 解析出 +IPD 数据
 *   [5] 超时则打印 ESP_Buff 原始内容辅助调试
 *
 * @param  request    完整的 HTTP 请求报文 (含所有头部 + 正文)
 * @param  timeout_ms 应答等待超时 (ms), 超时返回 OTA_ERR_TIMEOUT
 *
 * @note   本函数依赖全局变量:
 *         tcp_rx_flag — ISR 在解析到 +IPD 后置 1
 *         TCPdata     — ISR 将 +IPD 载荷复制到这里
 *         TCP_DataLength — ISR 记录的实际载荷长度
 *
 * @return OTA_OK           成功收到应答
 *         OTA_ERR_SEND     发送失败
 *         OTA_ERR_TIMEOUT  等待应答超时
 */
static OTA_Result OTA_SendRequest(const char *request, uint32_t timeout_ms)
{
    /* ── [1] 发送前: 清接收缓冲, 启动干净 DMA ── */
    tcp_rx_flag = 0;
    memset(TCPdata, 0, sizeof(TCPdata));
    Start_Recv();
    vTaskDelay(pdMS_TO_TICKS(20));

    /* ── [2] 发送 HTTP 请求 ──
     *      ESP_SendToTCPServer 内部:
     *        AT+CIPSEND=<n>  -> wait ">"
     *        <data>           -> wait "SEND OK"
     */
    if (ESP_SendToTCPServer(request, 5000) != ESP_OK)
    {
        printf("[OTA] HTTP send failed\r\n");
        return OTA_ERR_SEND;
    }

    /* ── [3] 先检查: 应答是否在发送期间已到达 ──
     *      ESP_SendToTCPServer 内部等待 "SEND OK" 时,
     *      HTTP 应答可能已经通过 +IPD 到达, ISR 中的 parse_ipd()
     *      已将其捕获到 TCPdata 并置位 tcp_rx_flag。
     *      此时直接消费标志即可, 无需重新启 DMA 和轮询。
     */
    if (tcp_rx_flag)
    {
        tcp_rx_flag = 0;
        return OTA_OK;
    }

    /* ── [4] 应答尚未到达: 重装 DMA, 轮询等待 ──
     *      Start_Recv 会清 ESP_Buff 并重启 HAL_UARTEx_ReceiveToIdle_DMA
     */
    Start_Recv();
    tcp_rx_flag = 0;
    vTaskDelay(pdMS_TO_TICKS(50));

    uint32_t t_start = HAL_GetTick();
    while ((HAL_GetTick() - t_start) < timeout_ms)
    {
        if (tcp_rx_flag)
        {
            tcp_rx_flag = 0; /* 消费标志, 为下一次请求做准备 */
            return OTA_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); /* 10ms 轮询间隔 */
    }

    /* ── [5] 超时: 打印诊断信息 (精简 — 避免阻塞加剧) ── */
    printf("[OTA] Timeout %lums, rx=%d\r\n",
           (unsigned long)timeout_ms, esp_rx_complete);
    return OTA_ERR_TIMEOUT;
}

/*==========================================================================
 * Step 1: 上报设备版本号
 *==========================================================================*/

/**
 * @brief  POST /fuse-ota/{pid}/{dev_name}/version
 *
 * 将当前固件版本上报给 OneNET OTA 服务器, 使其知道设备在线。
 * POST body 格式:
 *   {"s_version":"V1.0.0", "f_version":"V1.0.0"}
 *
 * 成功应答:
 *   HTTP/1.1 200 OK
 *   {"code":0,"msg":"succ"}
 */
/*==========================================================================
 * Step 1 & Step 2 核心逻辑 (不管理TCP连接)
 *==========================================================================*/

/**
 * @brief  Step 1 核心: 构造并发送版本上报请求 (假设TCP已连接)
 * @return OTA_OK / OTA_ERR_SEND / OTA_ERR_TIMEOUT / OTA_ERR_PARSE
 */
static OTA_Result OTA_DoReportVersion(void)
{
    OTA_Result ret;
    char request[512];
    char json_body[64];

    /* 构造 JSON 正文 */
    snprintf(json_body, sizeof(json_body),
             "{\"s_version\":\"%s\", \"f_version\": \"%s\"}",
             OTA_VERSION_STR, OTA_VERSION_STR);

    /* 构造 HTTP POST */
    snprintf(request, sizeof(request),
             "POST /fuse-ota/%s/%s/version HTTP/1.1\r\n"
             "Content-Type: application/json\r\n"
             "Authorization: %s\r\n"
             "host:%s\r\n"
             "Content-Length:%d\r\n"
             "\r\n"
             "%s",
             OTA_PRODUCT_ID, OTA_DEVICE_NAME,
             OTA_AUTHORIZATION,
             OTA_SERVER_IP,
             (int)strlen(json_body),
             json_body);

    ret = OTA_SendRequest(request, HTTP_TIMEOUT_DEF);
    if (ret != OTA_OK)
        return ret;

    if (strstr(TCPdata, "\"msg\":\"succ\""))
        return OTA_OK;

    printf("[OTA] Version report FAIL\r\n");
    return OTA_ERR_PARSE;
}

/**
 * @brief  Step 2 核心: 构造并发送任务查询请求 (假设TCP已连接)
 * @return OTA_OK / OTA_ERR_NO_TASK / OTA_ERR_PARSE
 */
static OTA_Result OTA_DoCheckTask(void)
{
    OTA_Result ret;
    char request[384];
    char *p;

    snprintf(request, sizeof(request),
             "GET /fuse-ota/%s/%s/check?type=2&version=%s HTTP/1.1\r\n"
             "Authorization: %s\r\n"
             "host:%s\r\n"
             "\r\n",
             OTA_PRODUCT_ID, OTA_DEVICE_NAME, OTA_VERSION_NUM,
             OTA_AUTHORIZATION,
             OTA_SERVER_IP);

    ret = OTA_SendRequest(request, HTTP_TIMEOUT_DEF);
    if (ret != OTA_OK)
        return ret;

    /* 无任务 / 任务过期 */
    if (strstr(TCPdata, "\"msg\":\"not exist\"") ||
        strstr(TCPdata, "\"code\":12012") ||
        strstr(TCPdata, "\"code\":12014"))
    {
        return OTA_ERR_NO_TASK;
    }

    /* 验证格式 */
    if (!strstr(TCPdata, "\"msg\":\"succ\"") ||
        !strstr(TCPdata, "\"data\":{"))
    {
        printf("[OTA] Check: unexpected response\r\n");
        return OTA_ERR_PARSE;
    }

    /* 解析 JSON 字段 */
    memset(&ota_task, 0, sizeof(ota_task));

    p = strstr(TCPdata, "\"target\":\"");
    if (p)
    {
        p += 10;
        for (int i = 0; i < (int)sizeof(ota_task.target_ver) - 1 && *p && *p != '\"'; i++)
            ota_task.target_ver[i] = *p++;
    }

    p = strstr(TCPdata, "\"tid\":");
    if (p)
        ota_task.tid = strtoul(p + 6, NULL, 10);

    p = strstr(TCPdata, "\"size\":");
    if (p)
        ota_task.size = strtoul(p + 7, NULL, 10);

    p = strstr(TCPdata, "\"md5\":\"");
    if (p)
    {
        p += 7;
        for (int i = 0; i < 32 && *p && *p != '\"'; i++)
            ota_task.md5[i] = *p++;
    }

    p = strstr(TCPdata, "\"type\":");
    if (p)
        ota_task.type = (uint8_t)(p[7] - '0');

    ota_task.valid = 1;
    return OTA_OK;
}

/*==========================================================================
 * 公开API: Step 1 & Step 2 (独立使用 — 各自管理TCP连接)
 *==========================================================================*/

/**
 * @brief  Step 1: 上报设备版本号 (独立使用, 含TCP连接管理)
 */
OTA_Result OTA_ReportVersion(void)
{
    OTA_Result ret;
    ret = OTA_TCP_Connect();
    if (ret != OTA_OK)
        return ret;
    ret = OTA_DoReportVersion();
    OTA_Disconnect();
    return ret;
}

/**
 * @brief  Step 2: 检查升级任务 (独立使用, 含TCP连接管理)
 */
OTA_Result OTA_CheckTask(void)
{
    OTA_Result ret;
    ret = OTA_TCP_Connect();
    if (ret != OTA_OK)
        return ret;
    ret = OTA_DoCheckTask();
    OTA_Disconnect();
    return ret;
}

/**
 * @brief  Step 1+2 合并: 单次TCP连接完成版本上报 + 任务查询
 *
 * 比分别调用 OTA_ReportVersion + OTA_CheckTask 少一次 TCP 握手,
 * 大幅降低连接失败概率。
 *
 * @return OTA_OK           有升级任务, ota_task 已填充
 *         OTA_ERR_NO_TASK  无升级任务
 *         其他              TCP/HTTP 错误
 */
OTA_Result OTA_ReportAndCheck(void)
{
    OTA_Result ret;

    /* ── 建立TCP连接 (仅一次) ── */
    ret = OTA_TCP_Connect();
    if (ret != OTA_OK)
        return ret;

    /* ── Step 1: 上报版本 ── */
    ret = OTA_DoReportVersion();
    if (ret != OTA_OK)
    {
        OTA_Disconnect();
        return ret;
    }

    /* ── Step 2: 查询任务 (复用同一连接) ── */
    ret = OTA_DoCheckTask();

    OTA_Disconnect();
    return ret;
}

/*==========================================================================
 * 版本号比较
 *==========================================================================*/

/**
 * @brief  将版本字符串解析为三段整数
 *
 * 输入示例:
 *   "V1.0.0" -> {1, 0, 0}
 *   "v2.3.1" -> {2, 3, 1}
 *   "1.0.0"  -> {1, 0, 0}
 *
 * @param ver  版本号字符串 (可带或不带 V/v 前缀)
 * @param v    输出数组 {major, minor, patch}
 */
static void OTA_ParseVersion(const char *ver, int *v)
{
    v[0] = v[1] = v[2] = 0;
    if (!ver)
        return;

    /* 跳过可选前导 V/v */
    if (*ver == 'V' || *ver == 'v')
        ver++;

    sscanf(ver, "%d.%d.%d", &v[0], &v[1], &v[2]);
}

/**
 * @brief  比较两个版本号, 判断是否需要升级
 *
 * 比较策略: 逐段比较 major -> minor -> patch
 *
 * @param new_ver 服务器目标版本 (如 "V2.0.0")
 * @param cur_ver 设备当前版本   (如 "V1.0.0")
 * @return  1 = new > cur, 需要升级
 *          0 = 版本相同
 *         -1 = new < cur, 目标版本比当前还旧 (异常)
 */
int OTA_CompareVersion(const char *new_ver, const char *cur_ver)
{
    int vn[3], vc[3];
    OTA_ParseVersion(new_ver, vn);
    OTA_ParseVersion(cur_ver, vc);

    for (int i = 0; i < 3; i++)
    {
        if (vn[i] > vc[i])
            return 1; /* 目标更新 -> 升级   */
        if (vn[i] < vc[i])
            return -1; /* 目标更旧 -> 跳过   */
    }
    return 0; /* 版本完全一致 */
}

/*==========================================================================
 * 二进制安全工具函数
 *
 * 固件数据是二进制格式, 可能包含 0x00 字节。
 * 标准 C 库的 strstr() 遇到 0x00 会截断, 因此需要 mem_find() 替代。
 *==========================================================================*/

/**
 * @brief  在缓冲区中查找子串 (二进制安全, 不依赖 strlen)
 *
 * @param  haystack     源缓冲区
 * @param  haystack_len 源缓冲区有效字节数
 * @param  needle       要查找的字节模式
 * @param  needle_len   模式字节数
 * @return 找到则返回指向匹配位置的指针, 否则 NULL
 */
static char *mem_find(const char *haystack, int haystack_len,
                      const char *needle, int needle_len)
{
    if (needle_len > haystack_len)
        return NULL;
    for (int i = 0; i <= haystack_len - needle_len; i++)
    {
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return (char *)(haystack + i);
    }
    return NULL;
}

/**
 * @brief  在 HTTP 应答中定位 \r\n\r\n (头部/正文分隔符)
 *
 * HTTP/1.1 协议规定: 头部和正文之间以空行分隔。
 * 空行 = "\r\n\r\n" (CRLF + CRLF)。
 *
 * 本函数找到分隔符位置后, 返回指向正文第一个字节的指针,
 * 即跳过 \r\n\r\n 这 4 个字节。
 *
 * @param  buf     HTTP 应答完整数据
 * @param  buf_len 数据字节数 (使用 TCP_DataLength, 而非 strlen)
 * @return 正文起始指针 (已跳过 \r\n\r\n), 找不到返回 NULL
 */
static char *http_find_body(char *buf, int buf_len)
{
    char *sep = mem_find(buf, buf_len, "\r\n\r\n", 4);
    if (sep)
        return sep + 4; /* 跳过分隔符, 指向正文第一个字节 */
    return NULL;
}

/*==========================================================================
 * W25Q128 Zone1 擦除
 *==========================================================================*/

/**
 * @brief  擦除 Zone1 中存放固件所需的全部扇区
 *
 * W25Q128 擦除特性:
 *   - 最小擦除单位: 4KB 扇区 (Sector Erase, ~50ms)
 *   - 大块擦除单位: 64KB 块  (Block Erase,  ~200ms)
 *
 * 优化策略:
 *   先用 64KB 块擦除 (速度更快), 剩余不足 64KB 的部分用 4KB 扇区擦除。
 *
 * 示例: total_size = 219564 bytes
 *   -> 3 个 64KB 块 (=192KB)
 *   -> 剩余 ~28KB 用 7 个 4KB 扇区
 *
 * @param total_size 固件总大小 (字节)
 */
static void OTA_EraseZone1(uint32_t total_size)
{
    uint32_t num_sectors = (total_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t remain = total_size;

    printf("[OTA] Erasing Zone1: %lu sectors (~%lu KB) for %lu bytes firmware...\r\n",
           (unsigned long)num_sectors,
           (unsigned long)(num_sectors * FLASH_SECTOR_SIZE / 1024),
           (unsigned long)total_size);

    /* ── 第 1 轮: 64KB 块擦除 (速度优先) ── */
    while (remain >= 65536)
    {
        uint32_t addr = total_size - remain;
        W25Q128_BlockErase_64K(addr);
        remain -= 65536;
    }

    while (remain > 0)
    {
        uint32_t addr = total_size - remain;
        W25Q128_SectorErase(addr);
        uint32_t step = (remain >= FLASH_SECTOR_SIZE) ? FLASH_SECTOR_SIZE : remain;
        remain -= step;
    }

    printf("[OTA] Erase done\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
}

/*==========================================================================
 * MD5 校验辅助函数
 *==========================================================================*/

/**
 * @brief  将 hex 字符转为 4-bit 数值
 * @return 0~15, 无效字符返回 0
 */
static inline uint8_t hex2nibble(char c)
{
    if (c >= '0' && c <= '9')
        return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (uint8_t)(c - 'A' + 10);
    return 0;
}

/**
 * @brief  读取 W25Q128 Zone1 固件并增量计算 MD5, 与期望值对比
 *
 * 数据流:
 *   W25Q128 Zone1 -> 4KB 栈缓冲 -> MD5_Update() × N -> MD5_Final()
 *                                                      ↓
 *                                                  16 字节 digest
 *                                                      ↓
 *                                                MD5_HexStr() -> 32 字符 hex
 *                                                      ↓
 *                                                memcmp() 对比
 *
 * @param  total_size   固件总大小 (字节)
 * @param  expected_md5 期望的 MD5 值 (32 字符 hex 字符串, 来自 OTA 服务器)
 * @return 1=MD5 匹配 (固件完整)
 *         0=MD5 不匹配 (固件损坏)
 */
int OTA_VerifyFirmware(uint32_t total_size, const char *expected_md5)
{
    uint8_t digest[MD5_HASH_SIZE];         /* 16 字节原始 digest       */
    uint8_t expected_bytes[MD5_HASH_SIZE]; /* 期望值 -> 字节数组       */
    char actual_hex[33];                   /* 实际值 -> hex 字符串      */
    MD5_CTX ctx;                           /* MD5 计算上下文          */
    uint8_t read_buf[4096];                /* Flash 读取缓冲区 (栈上)  */

    /* ── 无 MD5 可对比时跳过 ── */
    if (expected_md5 == NULL || expected_md5[0] == '\0')
        return 1; /* 无期望值则默认通过 */

    /* 期望 hex -> 字节数组 */
    for (int i = 0; i < MD5_HASH_SIZE; i++)
    {
        expected_bytes[i] = (uint8_t)((hex2nibble(expected_md5[i * 2]) << 4) |
                                      hex2nibble(expected_md5[i * 2 + 1]));
    }

    /* 从 W25Q128 分块读取, 增量计算 MD5 */
    MD5_Init(&ctx);
    uint32_t remain = total_size;
    uint32_t addr = 0;

    while (remain > 0)
    {
        uint32_t chunk = (remain > sizeof(read_buf)) ? (uint32_t)sizeof(read_buf) : remain;
        W25Q128_Read(addr, read_buf, chunk);
        MD5_Update(&ctx, read_buf, chunk);
        addr += chunk;
        remain -= chunk;
    }

    MD5_Final(digest, &ctx);

    /* ── 对比 ── */
    if (memcmp(digest, expected_bytes, MD5_HASH_SIZE) == 0)
        return 1;

    printf("[OTA] MD5 MISMATCH!\r\n");
    return 0;
}

/*==========================================================================
 * Step 3: 分片下载固件 -> 写入 W25Q128 Zone1
 *==========================================================================*/

/**
 * @brief  从 OneNET OTA 服务器分片下载固件, 写入 W25Q128 Zone1
 *
 * ========== 下载策略 ==========
 *
 * 连接策略 (Keep-Alive):
 *   整个下载过程只建立 1 次 TCP 连接, 所有分片共用。
 *   如果某个分片失败, 断开重连后从断点继续。
 *   这在 ESP8266 慢速 AT 连接场景下可节省大量时间。
 *
 * 分片大小:
 *   默认 3584 字节 (3.5KB)。
 *   ESP_Buff 是 4096 字节, HTTP 响应头约 200~300 字节,
 *   3584 + 300 ≈ 3884 < 4096, 确保 DMA 缓冲区安全。
 *
 * 断点续传:
 *   - 每个分片写入后立即保存 offset 到 FLAGS_DOWNLOADED_SIZE
 *   - 如果下载中断, 下次重启会读取进度, 匹配版本后从断点继续
 *   - 版本不匹配 -> 重新下载 (清空进度 + 擦除 Zone1)
 *
 * 分片重试:
 *   - 每个分片最多重试 3 次
 *   - 重试时断开旧连接 -> 300ms 等待 -> 重新 TCP 连接 -> 再发请求
 *
 * ========== HTTP 请求格式 ==========
 *
 *   GET /fuse-ota/{pid}/{dev_name}/{tid}/download HTTP/1.1
 *   Authorization: {token}
 *   host:iot-api.heclouds.com
 *   Connection:keep-alive
 *   Range:bytes={offset}-{range_end}
 *
 * ========== 数据写入路径 ==========
 *
 *   服务器 TCP 包
 *     -> ESP8266 WiFi 接收
 *     -> ESP8266 UART 发送 (含 +IPD 头)
 *     -> STM32 UART6 DMA
 *     -> DMA IDLE ISR -> parse_ipd() -> TCPdata[] / TCP_DataLength
 *     -> http_find_body() 定位正文
 *     -> W25Q128_Write() 写入 Flash
 *
 * @param  range_size 每片请求的字节数 (推荐 3584, 上限取决于 ESP_Buff 大小)
 * @return OTA_OK           下载 + MD5 校验均成功
 *         OTA_ERR_DOWNLOAD 下载失败 (已保存断点)
 *         其他              连接/发送错误
 */
OTA_Result OTA_DownloadFirmware(uint16_t range_size)
{
    OTA_Result ret;
    char request[384];   /* HTTP 请求报文缓冲区          */
    uint32_t offset = 0; /* 已下载字节数 (写指针)        */
    uint32_t total_size; /* 固件总大小 (来自 ota_task)   */

    /* ── 前置检查 ── */
    if (!ota_task.valid || ota_task.size == 0)
    {
        printf("[OTA] No valid task info, call OTA_CheckTask first\r\n");
        return OTA_ERR_DOWNLOAD;
    }

    total_size = ota_task.size;
    ota_download_progress = 0; /* 复位进度条 */

    if (range_size == 0)
        range_size = 3584; /* 默认每片 3.5KB */

    /* ══════════════════════════════════════════════════════════════
     * 阶段 1: 断点续传判断
     * ══════════════════════════════════════════════════════════════
     *
     * 读取 Flash 标志区:
     *   - saved_ver    : 上次下载的固件版本
     *   - saved_offset : 上次下载的进度 (字节数)
     *
     * 续传条件 is_resume (全部满足):  
     *   ① saved_ver == ota_task.target_ver  (同一版本)
     *   ② saved_offset > 0                  (有有效进度)
     *   ③ saved_offset < total_size         (还没下完)
     */
    char saved_ver[24];
    Flags_ReadNewVer(saved_ver);
    uint32_t saved_offset = Flags_ReadDownloadedSize();

    int is_resume = (strcmp(saved_ver, ota_task.target_ver) == 0 && saved_offset > 0 && saved_offset < total_size);

    if (is_resume)
    {
        printf("[OTA] >>> Resume from offset %lu / %lu <<<\r\n",
               (unsigned long)saved_offset, (unsigned long)total_size);
        offset = saved_offset;
        /* 注意: 续传不需要重新擦除, 直接接续写入 */
    }
    else
    {
        printf("[OTA] >>> Fresh download, erasing Zone1... <<<\r\n");
        Flags_SaveDownloadedSize(0); /* 清零进度       */
        OTA_EraseZone1(total_size);  /* 擦除 6MB Zone1 */
    }

    /* ══════════════════════════════════════════════════════════════
     * 阶段 2: 保存任务信息到 Flash 标志区
     *
     * 必须在擦除之后、下载之前写入, 确保:
     *   - 版本号记录正确 (用于重启后续传判断)
     *   - MD5 记录正确   (用于下载完成后的校验)
     * ══════════════════════════════════════════════════════════════ */
    Flags_WriteNewVer(ota_task.target_ver);
    Flags_WriteFwSize(total_size);
    Flags_SaveMd5(ota_task.md5);

    printf("[OTA] Start download: %lu bytes, chunk=%u bytes (keep-alive)\r\n",
           (unsigned long)total_size, range_size);

    /* ══════════════════════════════════════════════════════════════
     * 阶段 3: 建立 TCP 连接 (整个下载过程只此一次)
     * ══════════════════════════════════════════════════════════════ */
    ret = OTA_TCP_Connect();
    if (ret != OTA_OK)
    {
        Flags_SaveDownloadedSize(offset); /* 保存断点, 下次再试 */
        return ret;
    }

    /* ══════════════════════════════════════════════════════════════
     * 阶段 4: 循环下载 (Keep-Alive 复用 TCP 连接)
     *
     * 每次循环:
     *   [a] 计算 Range 头: bytes={offset}-{offset+range_size-1}
     *   [b] 构造 HTTP GET 请求
     *   [c] 发送请求 + 接收应答
     *   [d] 二进制安全地提取 HTTP 正文
     *   [e] 写入 W25Q128
     *   [f] 更新断点 (Flags_SaveDownloadedSize)
     *   [g] 短延时 100ms
     *
     * 如果某片失败 -> 断开 + 重连 + 重试 (最多 10 次)
     * 如果 10 次都失败 -> 保存断点 + 返回错误 + 下次重启续传
     * ══════════════════════════════════════════════════════════════ */
    while (offset < total_size)
    {
        int chunk_ok = 0; /* 当前分片是否成功 */

        /* ── 分片重试循环 (最多 10 次) ── */
        for (int retry = 0; retry < CHUNK_RETRY_MAX && !chunk_ok; retry++)
        {
            /* 第一次尝试用现有连接, 重试时断开重连 */
            if (retry > 0)
            {
                printf("[OTA] Chunk retry %d/%d at offset %lu...\r\n",
                       retry, CHUNK_RETRY_MAX, (unsigned long)offset);

                OTA_Disconnect();               /* 断开旧连接     */
                vTaskDelay(pdMS_TO_TICKS(300)); /* 等 ESP8266 恢复 */
                ret = OTA_TCP_Connect();        /* 重新连接       */
                if (ret != OTA_OK)
                    continue; /* 重连失败 -> 下一轮重试 */
            }

            /* ── [a] 计算 Range ──
             *      Range 是闭区间: bytes={first}-{last}
             *      例: 第一片 offset=0, range_size=3584
             *          -> Range:bytes=0-3583
             *      最后一片 offset+range_size 可能超出, 修正为 total_size-1
             */
            uint32_t range_end = offset + range_size - 1;
            if (range_end >= total_size)
                range_end = total_size - 1;

            /* ── [b] 构造 GET 请求 ── */
            snprintf(request, sizeof(request),
                     "GET /fuse-ota/%s/%s/%lu/download HTTP/1.1\r\n"
                     "Authorization: %s\r\n"
                     "host:%s\r\n"
                     "Connection:keep-alive\r\n"
                     "Range:bytes=%lu-%lu\r\n"
                     "\r\n",
                     OTA_PRODUCT_ID, OTA_DEVICE_NAME,
                     (unsigned long)ota_task.tid,
                     OTA_AUTHORIZATION,
                     OTA_SERVER_IP,
                     (unsigned long)offset, (unsigned long)range_end);

            /* ── [c] 发送 + 接收应答 ── */
            ret = OTA_SendRequest(request, HTTP_TIMEOUT_DOWN);
            if (ret != OTA_OK)
                continue; /* 发送/超时失败 -> 重试 */

            /* ── [d] 定位 HTTP 正文 ──
             *      http_find_body 在 TCPdata 中搜索 \r\n\r\n
             *      返回指向正文第一个字节的指针 (已跳过 4 字节分隔符)
             *
             *      使用 TCP_DataLength (实际的 IPD 长度), 而非 strlen,
             *      因为固件数据是二进制的, 可能包含 0x00 字节。
             */
            char *body = http_find_body(TCPdata, (int)TCP_DataLength);
            if (!body)
                continue; /* 找不到分隔符 -> 重试 */

            /* ── [e] 计算要写入的数据长度 ──
             *      header_end   = body 指针 - TCPdata 起始 = 头部+分隔符长度
             *      chunk_bytes  = TCP_DataLength - header_end = 纯正文长度
             */
            int header_end = (int)(body - TCPdata);
            int chunk_bytes = (int)TCP_DataLength - header_end;

            if (chunk_bytes <= 0)
                continue; /* 空正文 -> 重试 */

            /* ── [f] 写入 W25Q128 Zone1 ──
             *      Flash 地址 = offset (Zone1 从 0x000000 开始)
             */
            W25Q128_Write(offset, (uint8_t *)body, chunk_bytes);

            /* ── [g] 更新断点 ── */
            offset += chunk_bytes;
            Flags_SaveDownloadedSize(offset);

            /* ── [h] 进度输出 ── */
            ota_download_progress = (uint8_t)(offset * 100 / total_size);
            printf("[OTA] %lu / %lu  (%lu%%)\r\n",
                   (unsigned long)offset, (unsigned long)total_size,
                   (unsigned long)ota_download_progress);

            chunk_ok = 1; /* 本片成功 */
        }

        /* ── 10 次重试全部失败 ── */
        if (!chunk_ok)
        {
            printf("[OTA] Chunk failed after %d retries at offset %lu\r\n",
                   CHUNK_RETRY_MAX, (unsigned long)offset);
            OTA_Disconnect();
            Flags_SaveDownloadedSize(offset); /* 保存断点, 下次续传 */
            return OTA_ERR_DOWNLOAD;
        }

        /* 片间延时: Keep-Alive 模式下不需要长时间等待,
         * 100ms 足够 ESP8266 处理完上一帧的 ACK */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* ── 下载完成, 断开 TCP ── */
    OTA_Disconnect();

    /* ══════════════════════════════════════════════════════════════
     * 阶段 5: 下载完成
     * ══════════════════════════════════════════════════════════════ */
    printf("[OTA] Download done: %lu bytes\r\n", (unsigned long)offset);

    /* ══════════════════════════════════════════════════════════════
     * 阶段 6: MD5 校验
     *
     * 将刚写入 W25Q128 的固件全部回读, 计算 MD5,
     * 与 OTA 服务器下发的 MD5 对比。
     *
     * 不匹配 -> 清空进度和版本 -> 下次重启重新下载
     * 匹配   -> 清除断点 + 设 READY 标志 -> 等待 BootLoader
     * ══════════════════════════════════════════════════════════════ */
    if (!OTA_VerifyFirmware(total_size, ota_task.md5))
    {
        printf("[OTA] MD5 verify FAILED, will retry on next boot\r\n");
        Flags_SaveDownloadedSize(0);
        Flags_WriteNewVer("");
        return OTA_ERR_DOWNLOAD;
    }

    /* ── 校验通过: 清理断点并设置升级就绪标志 ── */
    Flags_SaveDownloadedSize(0);
    /* 关键: 清除 protect_flag, 迫使 BootLoader 在刷入新固件后检查 APP 是否确认。
     * 旧版 BootLoader 的 SetCommittedFlag 不会清 protect, 如果这里不清,
     * protect=OK 会残留, 导致 BootLoader 误判 COMMITTED+OK, 跳过回滚。 */
    Flags_SetProtectFlag(PROTECT_MAGIC_NONE);
    Flags_SetUpgradeFlag(UPGRADE_MAGIC_READY);
    printf("[OTA] Upgrade flag = READY, protect = NONE, waiting for bootloader\r\n");

    return OTA_OK;
}

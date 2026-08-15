/**
 * @file    esp8266.c
 * @brief   ESP8266 AT指令驱动 (UART6 + DMA2_Stream1 空闲中断接收)
 * @note    硬件: USART6_TX=PC6, USART6_RX=PC7, RST=PC8
 *          优先: USART6_IRQn=6, DMA2_Stream1_IRQn=7
 */

#include "esp8266.h"

/*==========================================================================
 * 全局变量
 *==========================================================================*/

uint8_t ESP_Buff[BUFF_SIZE] = {0}; /* DMA接收缓冲区，硬件DMA直接写入 */

/* 以下变量由ISR写入、任务读取，必须加volatile */
volatile uint16_t esp_rx_len = 0;     /* 最新一帧的字节数 */
volatile uint8_t esp_rx_complete = 0; /* 一帧接收完成标志：0=等待中 1=刚完成 */
volatile uint8_t tcp_rx_flag = 0;     /* TCP数据到达标志 (+IPD报文) */
volatile uint16_t TCP_DataLength = 0; /* +IPD数据载荷长度 */
volatile uint32_t idle_isr_cnt = 0;   /* 调试: IDLE ISR 触发次数 */
char TCPdata[4096] = {0};             /* +IPD数据载荷内容 (需匹配BUFF_SIZE) */

extern UART_HandleTypeDef huart2; /* 调试串口 */
extern UART_HandleTypeDef huart6; /* ESP8266通信串口 */

// #define ESP_DBG_ENABLE   /* ★ 取消注释开启ESP收发调试打印 */

/**
 * @brief  解析+IPD报文: +IPD,<len>:<data>
 */
static ESP_ACK parse_ipd(char *recvdata, uint16_t *data_length, char *data, uint16_t data_size)
{
    const char *start = strstr(recvdata, "+IPD");
    if (!start)
        return ESP_ERROR;
    start += 4;
    const char *p_comma = strchr(start, ',');
    const char *p_colon = strchr(start, ':');
    if (!p_comma || !p_colon)
        return ESP_ERROR;

    *data_length = atoi(p_comma + 1);
    uint16_t len = *data_length;
    if (len > data_size)
        return ESP_ERROR;

    memcpy(data, p_colon + 1, len);
    data[len] = '\0';
    return ESP_OK;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART6)
        return;

    /* 过滤：只要IDLE事件，HT(半传输)和TC(传输完成)忽略 */
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_IDLE)
        return;

    idle_isr_cnt++; /* 调试: 记录ISR触发次数 */
    /* 尝试解析TCP数据 (+IPD报文) */
    if (parse_ipd((char *)ESP_Buff, (uint16_t *)&TCP_DataLength, TCPdata, 4096) == 0)
        tcp_rx_flag = 1;

    esp_rx_len = size;
    esp_rx_complete = 1; /* 通知任务层：有一帧新数据 */
}

/*==========================================================================
 * 硬件控制
 *==========================================================================*/

/** @brief 硬件复位 ESP8266: 拉低500ms -> 拉高500ms */
void ESP_REST(void)
{
    HAL_GPIO_WritePin(ESP_RST_GPIO_Port, ESP_RST_Pin, GPIO_PIN_RESET);
    vTaskDelay(500);
    HAL_GPIO_WritePin(ESP_RST_GPIO_Port, ESP_RST_Pin, GPIO_PIN_SET);
    vTaskDelay(500);
}

/*==========================================================================
 * DMA接收管理
 *==========================================================================*/

/**
 * @brief  启动DMA空闲接收 (持续监听ESP8266发来的数据)
 * @note   每次调用会清空ESP_Buff并复位完成标志
 */
void Start_Recv(void)
{
    esp_rx_complete = 0;
    memset(ESP_Buff, 0, BUFF_SIZE);

    /* 如果上次DMA没停就先强制中止 */
    if (huart6.RxState != HAL_UART_STATE_READY)
        HAL_UART_AbortReceive(&huart6);

    HAL_UARTEx_ReceiveToIdle_DMA(&huart6, ESP_Buff, BUFF_SIZE);
}

/*==========================================================================
 * 核心：AT指令发送+等待应答
 *==========================================================================*/

/**
 * @brief  发送AT指令并等待期望应答 (核心引擎)
 *
 * @param  cmd      AT指令，需带\r\n，如 "AT\r\n"
 * @param  ack      期望应答中的关键字，如 "OK" ">" "SEND OK"
 * @param  timeout  超时时间(ms)
 * @return ESP_OK / ESP_ERROR / ESP_TIMEOUT
 *
 * @note   自动处理多帧拼接：ESP8266先echo指令再返回应答，
 *         可能分多帧到达（每帧结束触发IDLE），本函数通过偏移量
 *         续写DMA缓冲区来拼接，然后统一搜索ack。
 *         每轮循环vTaskDelay(1)让出CPU。
 */
ESP_ACK ESP_SendCmd(const char *cmd, const char *ack, uint32_t timeout)
{
    HAL_StatusTypeDef hal_ret;

    /* --- 0. 排空UART硬件RX FIFO，避免旧数据玷污新缓冲区 --- */
    HAL_UART_AbortReceive(&huart6);
    if (huart6.Instance)
    {
        __HAL_UART_CLEAR_PEFLAG(&huart6);
    }
    while (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_RXNE))
    {
        (void)READ_REG(huart6.Instance->DR);
    }

    /* --- 1. 清缓冲、启动DMA --- */
    memset(ESP_Buff, 0, BUFF_SIZE);
    esp_rx_complete = 0;

    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, ESP_Buff, BUFF_SIZE) != HAL_OK)
    {
        HAL_UART_AbortReceive(&huart6);
        vTaskDelay(pdMS_TO_TICKS(10));
        if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, ESP_Buff, BUFF_SIZE) != HAL_OK)
            return ESP_ERROR;
    }

    /* --- 2. 发送指令 --- */
    HAL_UART_Transmit(&huart6, (uint8_t *)cmd, strlen(cmd), 1000);

#ifdef ESP_DBG_ENABLE
    /* 调试：串口2打印发送的数据包（去掉末尾\r\n避免换行混乱） */
    {
        char tx_log[256] = {0};
        int len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n')
            len--;
        if (len > 0 && cmd[len - 1] == '\r')
            len--;
        snprintf(tx_log, sizeof(tx_log), "[ESP TX] %.*s\r\n", len, cmd);
        HAL_UART_Transmit(&huart2, (uint8_t *)tx_log, strlen(tx_log), 0xfff);
    }
#endif

    /* --- 3. 轮询等待应答 --- */
    uint32_t t_start = HAL_GetTick();
    uint16_t acc_len = 0;  /* 多帧累计已收字节数 */
    uint8_t rearm_err = 0; /* DMA重装失败标志 */

    while (HAL_GetTick() - t_start < timeout)
    {
        if (esp_rx_complete)
        {
            esp_rx_complete = 0;
            acc_len += esp_rx_len;

#ifdef ESP_DBG_ENABLE
            /* 调试：串口2打印接收的数据帧（累积内容） */
            {
                char rx_label[] = "[ESP RX] ";
                HAL_UART_Transmit(&huart2, (uint8_t *)rx_label, strlen(rx_label), 0xfff);
                HAL_UART_Transmit(&huart2, ESP_Buff, strlen((char *)ESP_Buff), 0xfff);
                HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, 0xfff);
            }
#endif

            /* 检查是否有期望应答 */
            if (strstr((const char *)ESP_Buff, ack))
                return ESP_OK; /* 找到了 */

            /* 还没找到 -> 从缓冲区尾部继续DMA接收下一帧 */
            if (acc_len < BUFF_SIZE)
            {
                hal_ret = HAL_UARTEx_ReceiveToIdle_DMA(&huart6, ESP_Buff + acc_len, BUFF_SIZE - acc_len);
                if (hal_ret != HAL_OK)
                    rearm_err = 1; /* 重装失败 */
            }
            else
            {
                acc_len = 0; /* 缓冲满，从头来 */
                memset(ESP_Buff, 0, BUFF_SIZE);
                HAL_UARTEx_ReceiveToIdle_DMA(&huart6, ESP_Buff, BUFF_SIZE);
            }
        }
        if (rearm_err)
            break;                    /* 重装失败，不用等了 */
        vTaskDelay(pdMS_TO_TICKS(1)); /* 让出CPU */
    }

    /* --- 4. 超时 --- */
#ifdef ESP_DBG_ENABLE
    {
        char to_log[256] = {0};
        int len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n')
            len--;
        if (len > 0 && cmd[len - 1] == '\r')
            len--;
        snprintf(to_log, sizeof(to_log), "[ESP TIMEOUT] cmd=\"%.*s\" ack=\"%s\" buf=[%s]\r\n", len, cmd, ack, ESP_Buff);
        HAL_UART_Transmit(&huart2, (uint8_t *)to_log, strlen(to_log), 0xfff);
    }
#endif
    HAL_UART_AbortReceive(&huart6);
    return ESP_TIMEOUT;
}

/*==========================================================================
 * TCP 应用层 (WiFi连接 / TCP连接 / 数据收发)
 *==========================================================================*/

/** @brief 连接WiFi: 先设Station模式，再连AP (timeout=10s) */
ESP_ACK ESP_Connect_Wifi(const char *ssid, const char *pass)
{
    ESP_ACK ret;

    /* 1. 等待ESP8266就绪：发AT测试，最多重试5次 */
    printf("[WiFi] 等待ESP8266就绪...\r\n");
    for (int i = 0; i < 5; i++)
    {
        ret = ESP_SendCmd("AT\r\n", "OK", 2000);
        if (ret == ESP_OK)
        {
            printf("[WiFi] ESP8266 AT就绪\r\n");
            break;
        }
        if (ret == ESP_ERROR)
            printf("[WiFi] DMA硬件错误，重试%d/5...\r\n", i + 1);
        else
            printf("[WiFi] AT无响应(超时)，重试%d/5...\r\n", i + 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (ret != ESP_OK)
    {
        printf("[WiFi] ESP8266无响应，放弃\r\n");
        return ret;
    }

    /* 2. 设Station模式 */
    printf("[WiFi] 设置Station模式...\r\n");
    ret = ESP_SendCmd("AT+CWMODE=1\r\n", "OK", 2000);
    if (ret != ESP_OK)
    {
        printf("[WiFi] CWMODE设置失败\r\n");
        return ret;
    }
    printf("[WiFi] Station模式OK\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 3. 连接AP */
    printf("[WiFi] 连接AP: %s...\r\n", ssid);
    char cmd[64] = {0};
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);
    ret = ESP_SendCmd(cmd, "OK", 10000);
    if (ret != ESP_OK)
    {
        printf("[WiFi] AP连接失败\r\n");
        return ret;
    }
    printf("[WiFi] AP连接成功\r\n");
    return ESP_OK;
}

/** @brief 连接TCP服务器: AT+CIPSTART */
ESP_ACK ESP_Connect_TCP(const char *ServerIP, uint16_t ServerPort, uint32_t timeout)
{
    char cmd[100] = {0};
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ServerIP, ServerPort);

    /*
     * 用 "CONNECT" 而非 "OK" 匹配:
     *   AT+CIPSTART 响应 = "...\r\nCONNECT\r\n\r\nOK\r\n"
     *   CONNECT 和 OK 常分两帧到达, DMA 重装可能漏掉 OK 帧,
     *   导致明明连上了却报 TIMEOUT。
     *   "CONNECT" 同时匹配首次连接和 "ALREADY CONNECTED"。
     */
    ESP_ACK ret = ESP_SendCmd(cmd, "CONNECT", timeout);
    if (ret == ESP_OK)
        return ESP_OK;
    /* 打印 ESP8266 原始响应辅助调试 */
    printf("TCP FAIL: %s (buf=%.60s)\r\n",
           ret == ESP_TIMEOUT ? "TIMEOUT" : "ERROR",
           ESP_Buff);
    return ret;
}

/**
 * @brief  向TCP服务器发送数据
 * @note   两步: AT+CIPSEND=<len>等">" -> 发数据等"SEND OK"
 */
ESP_ACK ESP_SendToTCPServer(const char *txData, uint32_t timeout)
{
    char cmd[100] = {0};
    uint16_t len = strlen(txData);

    /* 步骤1: 告知ESP8266要发多少字节，等待">"提示符 */
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);
    if (ESP_OK != ESP_SendCmd(cmd, ">", timeout))
    {
        printf("CIPSEND wait '>' Timeout\r\n");
        return ESP_TIMEOUT;
    }

    /* 步骤2: 发送原始数据，等待"SEND OK"确认 */
    if (ESP_OK == ESP_SendCmd(txData, "SEND OK", timeout))
        return ESP_OK;
    printf("Send TIMEOUT\r\n");
    return ESP_TIMEOUT;
}

/** @brief 处理收到的TCP数据 (在ESP_RxTask主循环中调用) */
void ESP_ReveToTVPServer(void)
{
    /* 由 ESP_MQTT_MsgProcess 统一处理 */
}

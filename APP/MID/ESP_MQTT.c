/**
 * @file    ESP_MQTT.c
 * @brief   OneNET MQTT 客户端实现 (ESP8266 AT 指令)
 *
 * @note    通过 AT+MQTT* 指令连接 OneNET 并收发物模型数据
 */

#include "ESP_MQTT.h"

// 全局MQTT管理实例
ESP_MQTT_HandleTypeDef hmqtt = {0};

/**
 * @brief 初始化MQTT固定参数（产品/设备/密钥/服务器）
 */
void ESP_MQTT_InitParam(void)
{
    memset(&hmqtt, 0, sizeof(ESP_MQTT_HandleTypeDef));
    strcpy(hmqtt.product_id, MQTT_PRODUCT_ID);
    strcpy(hmqtt.dev_name, MQTT_DEVICE_NAME);
    strcpy(hmqtt.auth_param, MQTT_AUTH_PARAM);
    strcpy(hmqtt.broker, MQTT_BROKER_URL);
    hmqtt.port = MQTT_BROKER_PORT;
    hmqtt.state = MQTT_STATE_IDLE;
}

/**
 * @brief 配置MQTT鉴权信息 AT+MQTTUSERCFG
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 * @note  AT格式：AT+MQTTUSERCFG=0,1,"devname","productid","authinfo",0,0,""
 */
static ESP_ACK ESP_MQTT_SetUserCfg(uint32_t timeout)
{
    char cmd_buf[512] = {0};
    // 直接使用完整鉴权串(MQTT_AUTH_PARAM)，不再拼接
    sprintf(cmd_buf, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
            hmqtt.dev_name, hmqtt.product_id, hmqtt.auth_param);

    ESP_ACK ret = ESP_SendCmd(cmd_buf, "OK", timeout);
    if (ret == ESP_OK)
    {
        hmqtt.state = MQTT_STATE_CFG_READY;
    }
    return ret;
}

/**
 * @brief 连接MQTT服务器 AT+MQTTCONN
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
static ESP_ACK ESP_MQTT_ConnectBroker(uint32_t timeout)
{
    char cmd_buf[128] = {0};
    sprintf(cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", hmqtt.broker, hmqtt.port);
    ESP_ACK ret = ESP_SendCmd(cmd_buf, "OK", timeout);

    if (ret == ESP_OK)
    {
        hmqtt.state = MQTT_STATE_CONNECTED;
    }
    return ret;
}

/**
 * @brief 订阅OneNET两个必需主题
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
static ESP_ACK ESP_MQTT_SubscribeTopic(uint32_t timeout)
{
    char sub_cmd[128] = {0};
    char topic_buf[96] = {0};
    ESP_ACK ret;

    // 1. 订阅属性上报回复主题
    sprintf(topic_buf, MQTT_TOPIC_POST_REPLY, hmqtt.product_id, hmqtt.dev_name);
    sprintf(sub_cmd, "AT+MQTTSUB=0,\"%s\",0\r\n", topic_buf);
    ret = ESP_SendCmd(sub_cmd, "OK", timeout);
    if (ret != ESP_OK)
        return ret;

    // 2. 订阅平台下发控制指令主题
    memset(topic_buf, 0, sizeof(topic_buf));
    sprintf(topic_buf, MQTT_TOPIC_PROP_SET, hmqtt.product_id, hmqtt.dev_name);
    memset(sub_cmd, 0, sizeof(sub_cmd));
    sprintf(sub_cmd, "AT+MQTTSUB=0,\"%s\",0\r\n", topic_buf);
    ret = ESP_SendCmd(sub_cmd, "OK", timeout);
    if (ret == ESP_OK)
    {
        hmqtt.state = MQTT_STATE_SUB_OK;
    }
    return ret;
}

/**
 * @brief MQTT完整连接流程：配置鉴权->连服务器->订阅主题
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_Connect(uint32_t timeout)
{
    ESP_ACK ret;

    // 步骤1：配置MQTT用户鉴权信息
    ret = ESP_MQTT_SetUserCfg(timeout);
    if (ret != ESP_OK)
    {
        printf("[MQTT] 鉴权失败\r\n");
        hmqtt.state = MQTT_STATE_ERR;
        return ret;
    }
    /* OK */
    vTaskDelay(pdMS_TO_TICKS(300));

    // 步骤2：连接OneNET MQTT服务器
    ret = ESP_MQTT_ConnectBroker(timeout);
    if (ret != ESP_OK)
    {
        printf("[MQTT] 连接失败\r\n");
        hmqtt.state = MQTT_STATE_ERR;
        return ret;
    }
    /* OK */
    vTaskDelay(pdMS_TO_TICKS(300));

    // 步骤3：订阅平台上下行主题
    ret = ESP_MQTT_SubscribeTopic(timeout);
    if (ret != ESP_OK)
    {
        printf("[MQTT] 订阅失败\r\n");
        hmqtt.state = MQTT_STATE_ERR;
        return ret;
    }
    /* OK */
    return ESP_OK;
}

/**
 * @brief 上报JSON格式设备属性至OneNET
 * @param json_data 标准物模型JSON字符串
 * @param timeout   单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_PublishProperty(const char *json_data, uint32_t timeout)
{
    if (hmqtt.state != MQTT_STATE_SUB_OK)
    {
        printf("[MQTT] 禁止发布\r\n");
        return ESP_ERROR;
    }

    char pub_cmd[1024] = {0};
    char topic_buf[96] = {0};
    char escaped_json[512] = {0}; /* 转义后JSON（"->\"  ,->\,） */

    /* JSON中的 " 和 , 需要转义，否则ESP8266 AT解析器会误当成分隔符 */
    int j = 0;
    for (int i = 0; json_data[i] != '\0' && j < (int)sizeof(escaped_json) - 2; i++)
    {
        if (json_data[i] == '"' || json_data[i] == ',')
            escaped_json[j++] = '\\';
        escaped_json[j++] = json_data[i];
    }
    escaped_json[j] = '\0';

    sprintf(topic_buf, MQTT_TOPIC_POST, hmqtt.product_id, hmqtt.dev_name);

    // AT+MQTTPUB=0,"主题","数据",0,0
    sprintf(pub_cmd, "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n", topic_buf, escaped_json);

    ESP_ACK ret = ESP_SendCmd(pub_cmd, "OK", timeout);
    if (ret == ESP_OK)
    {
        /* OK */
    }
    else
    {
        printf("[MQTT] 属性上报超时/失败\r\n");
    }
    return ret;
}

/**
 * @brief MQTT消息处理入口，配合底层tcp_rx_flag使用
 * @note  放在FreeRTOS ESP接收任务循环中轮询调用
 */
void ESP_MQTT_MsgProcess(void)
{
    if (tcp_rx_flag == 0)
        return;

    // 清除接收标志，防止重复解析
    tcp_rx_flag = 0;

    /* MQTT data received, processed silently */

    // 此处可扩展JSON解析逻辑，解析平台下发的控制指令
    // 示例：解析{"params":{"开关":{"value":1}}} 做IO控制
}

/**
 * @brief 断开MQTT连接
 * @param timeout 单条AT指令超时ms
 * @return ESP_OK/ESP_TIMEOUT/ESP_ERROR
 */
ESP_ACK ESP_MQTT_Disconnect(uint32_t timeout)
{
    ESP_ACK ret = ESP_SendCmd("AT+MQTTCLEAN=0\r\n", "OK", timeout);
    hmqtt.state = MQTT_STATE_IDLE;
    return ret;
}

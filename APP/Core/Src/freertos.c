/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include <stdio.h>

// 底层代码
#include "ili9341.h"
#include "touch.h"
#include "w25q128.h"
#include "esp8266.h"
#include "led.h"

// 中间层代码
#include "ESP_MQTT.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "APP_lv_gui.h"
#include "APP_ota.h"
#include "ota_http.h"
#include "iwdg.h"
#include "APP_can.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for LVGLTask */
// LVGL 界面刷新任务：获取 LVGL_READY 信号量后周期性调用 lv_timer_handler() 刷新界面
osThreadId_t LVGLTaskHandle;
const osThreadAttr_t LVGLTask_attributes = {
  .name = "LVGLTask",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Lv_page_show */
// LVGL 初始化任务：初始化 LCD/触摸/Flash/LED 与 LVGL，完成后释放 LVGL_READY
osThreadId_t Lv_page_showHandle;
const osThreadAttr_t Lv_page_show_attributes = {
  .name = "Lv_page_show",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for espRx */
// ESP8266 通信任务：连接 WiFi/MQTT 并循环处理消息，初始化完成后释放 OTA_READY
osThreadId_t espRxHandle;
const osThreadAttr_t espRx_attributes = {
  .name = "espRx",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for OTA */
// OTA 固件升级任务：等待 OTA_READY 后执行升级流程，并周期性检查/下载新版本
osThreadId_t OTAHandle;
const osThreadAttr_t OTA_attributes = {
  .name = "OTA",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for IWDG */
// 独立看门狗喂狗任务：周期性刷新 IWDG，OTA 下载期间停止喂狗以触发复位
osThreadId_t IWDGHandle;
const osThreadAttr_t IWDG_attributes = {
  .name = "IWDG",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for CANHandle */
// CAN 总线通信任务：初始化 CAN 后周期性轮询收发
osThreadId_t CANHandleHandle;
const osThreadAttr_t CANHandle_attributes = {
  .name = "CANHandle",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LVGL_READY */
// LVGL 初始化完成信号量：由 Lv_show 释放，LVGL_Task 获取
osSemaphoreId_t LVGL_READYHandle;
const osSemaphoreAttr_t LVGL_READY_attributes = {
  .name = "LVGL_READY"
};
/* Definitions for OTA_READY */
// 初始化完成信号量：由 ESP_RxTask 释放，OTA_Task 获取后开始升级
osSemaphoreId_t OTA_READYHandle;
const osSemaphoreAttr_t OTA_READY_attributes = {
  .name = "OTA_READY"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void LVGL_Task(void *argument);
void Lv_show(void *argument);
void ESP_RxTask(void *argument);
void OTA_Task(void *argument);
void IWDG_Task(void *argument);
void CAN_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationTickHook(void);

/* USER CODE BEGIN 3 */
__weak void vApplicationTickHook(void)
{
  /* This function will be called by each tick interrupt if
  configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
  added here, but the tick hook is called from an interrupt context, so
  code must not attempt to block, and only the interrupt safe FreeRTOS API
  functions can be used (those that end in FromISR()). */
}
/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of LVGL_READY */
  LVGL_READYHandle = osSemaphoreNew(1, 0, &LVGL_READY_attributes);

  /* creation of OTA_READY */
  OTA_READYHandle = osSemaphoreNew(1, 0, &OTA_READY_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of LVGLTask */
  LVGLTaskHandle = osThreadNew(LVGL_Task, NULL, &LVGLTask_attributes);

  /* creation of Lv_page_show */
  Lv_page_showHandle = osThreadNew(Lv_show, NULL, &Lv_page_show_attributes);

  /* creation of espRx */
  espRxHandle = osThreadNew(ESP_RxTask, NULL, &espRx_attributes);

  /* creation of OTA */
  OTAHandle = osThreadNew(OTA_Task, NULL, &OTA_attributes);

  /* creation of IWDG */
  IWDGHandle = osThreadNew(IWDG_Task, NULL, &IWDG_attributes);

  /* creation of CANHandle */
  CANHandleHandle = osThreadNew(CAN_Task, NULL, &CANHandle_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_LVGL_Task */
/**
 * @brief  Function implementing the LVGLTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_LVGL_Task */
void LVGL_Task(void *argument)
{
  /* USER CODE BEGIN LVGL_Task */
  /* 等待 LVGL 初始化完成（Lv_show 释放 LVGL_READY），随后周期性刷新界面 */
  osSemaphoreAcquire(LVGL_READYHandle, osWaitForever);
//	LCD_Clear(BLACK);
  /* Infinite loop */
  for (;;)
  {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  /* USER CODE END LVGL_Task */
}

/* USER CODE BEGIN Header_Lv_show */
/**
 * @brief Function implementing the Lv_page_show thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_Lv_show */
void Lv_show(void *argument)
{
  /* USER CODE BEGIN Lv_show */
  /* 初始化底层硬件与 LVGL，构建界面后释放 LVGL_READY 通知刷新任务 */
  LCD_Init();
  TP_Init();
  W25Q128_Init();
  LED_Init();
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();
  //  lv_demo_widgets();
  lv_gui();
  gui_set_version(OTA_VERSION_STR);
  osSemaphoreRelease(LVGL_READYHandle);
  /* Infinite loop */
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  /* USER CODE END Lv_show */
}

/* USER CODE BEGIN Header_ESP_RxTask */
/**
 * @brief  Function implementing the ESP_RxTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_ESP_RxTask */
void ESP_RxTask(void *argument)
{
  /* USER CODE BEGIN ESP_RxTask */
  /* 连接 WiFi/MQTT 并循环处理消息；初始化完成后释放 OTA_READY 通知 OTA 任务 */
  ESP_ACK ret;
  // 1. 硬件初始化
  ESP_REST();
  ESP_MQTT_InitParam(); /* 先初始化MQTT参数，不要先调Start_Recv */

  // 2. 连接WiFi
  ret = ESP_Connect_Wifi(USER_SSID, USER_PASS);
  if (ret != ESP_OK)
  {
    printf("WiFi连接失败,任务挂起\r\n");
    vTaskDelete(NULL);
  }
  printf("WiFi连接成功\r\n");
  gui_set_wifi(1); /* LVGL 显示 WiFi 已连接图标 */
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 3. 连接OneNET MQTT
  ret = ESP_MQTT_Connect(5000);
  if (ret != ESP_OK)
  {
    printf("MQTT连接失败,等待重连\r\n");
  }

  /* 初始化完成，通知 OTA 任务可以开始检查升级 */
  osSemaphoreRelease(OTA_READYHandle);
  printf("[ESP] Init complete, OTA_READY released\r\n");

  // 循环处理MQTT消息
  while (1)
  {
    ESP_MQTT_MsgProcess();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  /* USER CODE END ESP_RxTask */
}

/* USER CODE BEGIN Header_OTA_Task */
/**
 * @brief  OTA 固件升级任务入口
 *
 * 全部业务流程封装在 APP_OTA_Run() 中 (见 APP/APP_ota.c),
 * 本函数仅负责创建 FreeRTOS 任务外壳并调用它
 *
 * @note  OTA_READY 信号量由 ESP_RxTask 释放, 本任务获取后开始升级
 *        espRxHandle 在 OTA 期间被挂起, 完成后恢复
 *
 * @param argument 未使用
 */
/* USER CODE END Header_OTA_Task */
void OTA_Task(void *argument)
{
  /* USER CODE BEGIN OTA_Task */
  /* 等待 OTA_READY 后执行升级流程，并周期性检查/下载新版本 */
  APP_OTA_Run(); /* 阻塞执行完整 OTA 流程 */

  /* 注册 OTA 下载按钮回调: 用户按下 LVGL "下载" 按钮后设置请求标志 */
  gui_set_ota_download_callback(APP_OTA_RequestDownload);

/* OTA 完成后, 周期性检查新版本 */
#define OTA_CHECK_INTERVAL_MS 10000
#define OTA_POLL_INTERVAL_MS 500 /* 短轮询: 快速响应下载请求 */
  uint32_t next_check_tick = HAL_GetTick() + OTA_CHECK_INTERVAL_MS;

  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS));

    /* ════════════════════════════════════════════════════════
     * 优先处理用户下载请求 (由 LVGL 按钮触发)
     * LVGL 按钮只设标志位, 实际下载在 OTA_Task 后台执行,
     * 这样 LVGL 任务可以继续刷新 UI, 屏幕不会卡死
     * ════════════════════════════════════════════════════════ */
    if (ota_download_requested)
    {
      ota_download_requested = 0;
      vTaskDelay(pdMS_TO_TICKS(200));
      APP_OTA_StartDownload();
      /* 下载完成: 成功→重启  失败→已恢复MQTT, 继续循环 */
      next_check_tick = HAL_GetTick() + OTA_CHECK_INTERVAL_MS;
      continue;
    }

    /* 下载进行中则跳过周期性检查, 避免 +++ 掐断 TCP */
    if (ota_busy)
    {
      continue;
    }

    /* 每 OTA_CHECK_INTERVAL_MS 执行一次周期性版本检查 */
    if (HAL_GetTick() < next_check_tick)
    {
      continue;
    }
    next_check_tick = HAL_GetTick() + OTA_CHECK_INTERVAL_MS;

    /* 暂停 MQTT, 接管 ESP8266 做 OTA 查询 */
    osThreadSuspend(espRxHandle);
    vTaskDelay(pdMS_TO_TICKS(100));
    OTA_ESP_EnterCmdMode();    /* [1] 退出数据模式 + AT 确认命令模式    */
    ESP_MQTT_Disconnect(2000); /* [2] 释放 MQTT 会话                    */
    OTA_ESP_CloseAllTCP();     /* [3] 关闭 TCP + 设置 CIPMUX=0            */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 查询 OneNET 是否有新版本 */
    OTA_Result ret = OTA_CheckTask();
    if (ret == OTA_OK && OTA_CompareVersion(ota_task.target_ver, OTA_VERSION_STR) > 0)
    {
      gui_set_ota_available(1);
    }
    else
    {
      gui_set_ota_available(0);
    }

    /* 恢复 MQTT */
    /* 若下载已在此间触发则跳过 MQTT 重连, 让下载独占 UART */
    if (!ota_busy)
    {
      ESP_MQTT_Connect(5000);
      osThreadResume(espRxHandle);
    }
  }
  /* USER CODE END OTA_Task */
}

/* USER CODE BEGIN Header_IWDG_Task */
/**
 * @brief Function implementing the IWDG thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_IWDG_Task */
void IWDG_Task(void *argument)
{
  /* USER CODE BEGIN IWDG_Task */
  /* 周期性刷新独立看门狗；OTA 下载期间停止喂狗，由看门狗超时复位 */
  /* Infinite loop */
  for (;;)
  {
    if (!g_ota_stop_feed_dog)
    {
      HAL_IWDG_Refresh(&hiwdg); /* stop feeding after download, let IWDG reset */
    }
    osDelay(2000);
  }
  /* USER CODE END IWDG_Task */
}

/* USER CODE BEGIN Header_CAN_Task */
/**
 * @brief Function implementing the CAN thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_CAN_Task */
void CAN_Task(void *argument)
{
  /* USER CODE BEGIN CAN_Task */
  /* 初始化 CAN 后周期性轮询收发 */
  APP_CAN_Init(); /* 初始化 CAN: 滤波器 + 启动 */

  /* Infinite loop */
  for (;;)
  {
    // APP_CAN_DebugPoll(); /* 调试: 串口输入 转 CAN 发送 (接 F1 后屏蔽) */
    APP_CAN_Run(); /* 轮询接收并解析 */
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  /* USER CODE END CAN_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */


/**
 * @file    APP_ota.c
 * @brief   OTA 固件升级应用层 — 完整流程实现
 *
 * @note    依赖 ota_http / ota_flags / ESP_MQTT / esp8266
 */

#include "APP_ota.h"

#include "ota_http.h"
#include "ota_flags.h"
#include "ESP_MQTT.h"
#include "esp8266.h"
#include "APP_lv_gui.h"
#include "iwdg.h"

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/*=== 外部引用 (定义在 freertos.c) ===*/
extern osSemaphoreId_t OTA_READYHandle; /**< OTA 同步信号量 */
extern osThreadId_t espRxHandle;        /**< MQTT 接收任务句柄 */

#define OTA_CHUNK_SIZE 3584 /**< OTA 下载分片大小 */

/*=== 内部函数声明 ===*/
static void APP_OTA_SyncVersion(void);
static void APP_OTA_CheckFlags(uint32_t *up_flag);
static void APP_OTA_BackupFirmware(uint32_t fw_size);

volatile uint8_t ota_busy = 0;               /* 下载忙标志 */
volatile uint8_t ota_download_requested = 0; /* 用户下载请求标志 */
volatile uint8_t g_ota_stop_feed_dog = 0;    /* 下载完成停止喂狗标志 */

/**
 * @brief 请求开始下载 (非阻塞, 立即返回)
 */
void APP_OTA_RequestDownload(void)
{
    if (ota_busy)
    {
        printf("[OTA] Already downloading, ignore request\r\n");
        return;
    }
    printf("[OTA] Download requested by user\r\n");
    ota_download_requested = 1;
}

/* 备份搬运缓冲区 */
#define BACKUP_BUF_SIZE 4096
static uint8_t __attribute__((aligned(4))) backup_buf[BACKUP_BUF_SIZE];

/*=== 主入口 ===*/

/**
 * @brief 执行完整 OTA 升级流程
 */
void APP_OTA_Run(void)
{
    /*=== 阶段 A: 等待网络就绪 ===*/
    osSemaphoreAcquire(OTA_READYHandle, osWaitForever);
    // printf("[OTA] ESP ready, starting OTA\r\n");

    /*=== 阶段 B: 接管 ESP8266 ===*/
    osThreadSuspend(espRxHandle);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 三步清理: 退出数据模式 -> 释放 MQTT -> 关闭 TCP */
    OTA_ESP_EnterCmdMode();
    ESP_MQTT_Disconnect(2000);
    OTA_ESP_CloseAllTCP();
    vTaskDelay(pdMS_TO_TICKS(100));

    /*=== 阶段 C: 版本同步 + 标志自检 ===*/
    APP_OTA_SyncVersion();

    uint32_t up_flag;
    APP_OTA_CheckFlags(&up_flag);

    /*=== 阶段 D: 状态机分流 ===*/
    if (up_flag == UPGRADE_MAGIC_COMMITTED)
    {
        /* BootLoader 已搬运新固件 */
        printf("[OTA] BootLoader has flashed new firmware!\r\n");

        /* Step 1: 上报新版本号 */
        OTA_Result ret = OTA_ReportVersion();
        /* Step 2: 备份新固件 Zone1 -> Zone2 */
        uint32_t fw_size = Flags_ReadFwSize();
        APP_OTA_BackupFirmware(fw_size);
        Flags_WriteBackupVer(OTA_VERSION_STR);
        Flags_WriteBackupFwSize(fw_size);

        /* Step 3~5: 更新标志区, 确认新固件运行 */
        Flags_WriteCurrentVer(OTA_VERSION_STR);
        Flags_SetProtectFlag(PROTECT_MAGIC_OK);
        Flags_ClearBootFailCnt();
        Flags_SetUpgradeFlag(UPGRADE_MAGIC_NONE);
        Flags_SaveDownloadedSize(0);
        Flags_WriteNewVer("");

        /* 恢复 MQTT */
        ESP_MQTT_Connect(5000);
        osThreadResume(espRxHandle);
        return;
    }
    else if (up_flag == UPGRADE_MAGIC_READY)
    {
        /* 固件已在 Zone1, 本地 MD5 校验 */
        char saved_md5[33];
        Flags_ReadMd5(saved_md5);
        uint32_t fw_size = Flags_ReadFwSize();

        if (fw_size > 0 && OTA_VerifyFirmware(fw_size, saved_md5))
        {
            printf("[OTA] Firmware VERIFIED OK, ready for bootloader\r\n");
        }
        else
        {
            printf("[OTA] Firmware CORRUPTED, clearing\r\n");
            Flags_SetUpgradeFlag(UPGRADE_MAGIC_NONE);
            Flags_SaveDownloadedSize(0);
            Flags_WriteNewVer("");
        }
    }
    else /* NONE: 联网 OTA 流程 */
    {
        /* Step 1+2: 单次 TCP 完成版本上报 + 任务查询 */
        OTA_Result ret = OTA_ReportAndCheck();
        if (ret == OTA_OK)
        {
            // printf("[OTA] New firmware: %s (%lu bytes)\r\n",ota_task.target_ver, (unsigned long)ota_task.size);

            if (OTA_CompareVersion(ota_task.target_ver, OTA_VERSION_STR) > 0)
            {
                // printf("[OTA] New version available, waiting for user\r\n");
                gui_set_ota_available(1); /* 显示通知 + 下载按钮 */
            }
            else
            {
                // printf("[OTA] Target %s <= current %s, skip download\r\n", ota_task.target_ver, OTA_VERSION_STR);
            }
        }
        else
        {
            // printf("[OTA] Check: %s\r\n", OTA_ResultStr(ret));
        }
    }

    /*=== 阶段 E: 恢复网络 ===*/
    ESP_MQTT_Connect(5000);
    osThreadResume(espRxHandle);
}

/*=== 按钮触发: 固件下载 ===*/

/**
 * @brief 由下载按钮回调触发, 执行固件下载
 *
 * 下载到 Zone1, 成功重启进 BootLoader, 失败恢复 MQTT。
 */
void APP_OTA_StartDownload(void)
{
    printf("[OTA] User triggered download\r\n");

    ota_busy = 1; /* 阻止周期性检查抢占 ESP8266 */

    /* 接管 ESP8266 — 三步清理, 同阶段 B */
    osThreadSuspend(espRxHandle);
    vTaskDelay(pdMS_TO_TICKS(100));
    OTA_ESP_EnterCmdMode();
    ESP_MQTT_Disconnect(2000);
    OTA_ESP_CloseAllTCP();
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("[OTA] Downloading...\r\n");
    OTA_Result ret = OTA_DownloadFirmware(OTA_CHUNK_SIZE);
    printf("[OTA] OTA_DownloadFirmware returned: %s (%d)\r\n",
           OTA_ResultStr(ret), (int)ret);

    if (ret == OTA_OK)
    {
        printf("\r\n========================================\r\n");
        printf("[OTA] Download OK, rebooting...\r\n");
        printf("========================================\r\n\r\n");

        /* 停止喂狗 + 缩短 IWDG 超时, 利用看门狗快速复位进 BootLoader */
        g_ota_stop_feed_dog = 1;
        IWDG->KR = 0x5555;   /* 使能 PR/RLR 写访问 */
        IWDG->PR = 0x00;     /* 分频 ÷4 */
        IWDG->RLR = 4000;    /* 4000 计数, 约 0.5~1s 超时 */
        IWDG->KR = 0xAAAA;   /* 重载计数器, 新超时生效 */

        while (1)
            ;
    }
    else
    {
        printf("[OTA] Download failed: %s\r\n", OTA_ResultStr(ret));
        ota_busy = 0;
        ESP_MQTT_Connect(5000);
        osThreadResume(espRxHandle);
        /* 隐藏进度条, 恢复 GO 按钮 */
        gui_ota_download_failed();
    }
}

/*=== 阶段 C: 版本同步 ===*/

/**
 * @brief 同步编译版本到 FLAGS_CURRENT_VER
 */
static void APP_OTA_SyncVersion(void)
{
    char flags_ver[24];
    Flags_ReadCurrentVer(flags_ver);

    if (flags_ver[0] != '\0' && flags_ver[0] != 0xFF)
    {
        if (strcmp(flags_ver, OTA_VERSION_STR) != 0)
            printf("[OTA] WARNING: flags ver mismatch!\r\n");
    }
    else
    {
        Flags_WriteCurrentVer(OTA_VERSION_STR);
    }
}

/*=== 标志区自检 ===*/

/**
 * @brief 校验 upgrade_flag 魔数, 垃圾值则格式化
 *
 * @param up_flag 出参: 格式化后的 upgrade_flag
 */
static void APP_OTA_CheckFlags(uint32_t *up_flag)
{
    *up_flag = Flags_ReadUpgradeFlag();

    if (*up_flag != UPGRADE_MAGIC_NONE &&
        *up_flag != UPGRADE_MAGIC_READY &&
        *up_flag != UPGRADE_MAGIC_COMMITTED)
    {
        printf("[OTA] Flags uninitialized (0x%08lX), formatting...\r\n",
               (unsigned long)*up_flag);
        Flags_Format();
        *up_flag = UPGRADE_MAGIC_NONE;
    }
}

/*=== 固件备份: Zone1 -> Zone2 ===*/

/**
 * @brief 将 Zone1 新固件搬运到 Zone2 作备份
 *
 * @param fw_size 固件大小 (字节)
 */
static void APP_OTA_BackupFirmware(uint32_t fw_size)
{
#define ZONE1_BASE 0x00000000U
#define ZONE2_BASE 0x00600000U

    uint32_t offset;
    uint32_t sector;

    if (fw_size == 0)
    {
        printf("[OTA] Backup skipped: fw_size=0\r\n");
        return;
    }

    /* 1. 擦除 Zone2 目标区域 */
    for (sector = 0; sector < fw_size; sector += W25Q128_SECTOR_SIZE)
    {
        W25Q128_SectorErase(ZONE2_BASE + sector);
    }

    /* 2. 分块搬运 Zone1 -> Zone2 */
    for (offset = 0; offset < fw_size; offset += BACKUP_BUF_SIZE)
    {
        uint32_t chunk = (fw_size - offset > BACKUP_BUF_SIZE)
                             ? BACKUP_BUF_SIZE
                             : (fw_size - offset);

        W25Q128_Read(ZONE1_BASE + offset, backup_buf, chunk);
        W25Q128_Write(ZONE2_BASE + offset, backup_buf, chunk);
    }

    printf("[OTA] Backup done (%lu bytes)\r\n", (unsigned long)fw_size);
}

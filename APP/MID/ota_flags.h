/**
 * @file    ota_flags.h
 * @brief   OTA 标志区管理模块 — W25Q128 分区 3 (0x00C00000~0x00C0FFFF, 64KB)
 *
 * ===========================================================================
 * 设计目标
 * ===========================================================================
 *   将 OTA 流程中所有需要持久化的标志、版本号、进度信息集中存储在
 *   W25Q128 外部 Flash 的一个专用分区中, 断电不丢失。
 *
 * ===========================================================================
 * 存储策略: 读-改-擦-写 (R-M-E-W)
 * ===========================================================================
 *   W25Q128 最小擦除单位是 4KB 扇区, 不支持字节级覆盖写入。
 *   因此本模块采用 "读-改-擦-写" 机制:
 *     ① 读:  将整个 4KB 扇区读到 RAM 缓冲区
 *     ② 改:  在缓冲区中修改目标字节
 *     ③ 擦:  擦除该 4KB 扇区
 *     ④ 写:  将修改后的缓冲区整体写回
 *   -> 保证同扇区内其他字段完整保留, 不会因修改 1 个字段而破坏其他数据。
 *
 * ===========================================================================
 * 扇区布局 (4KB, 地址映射)
 * ===========================================================================
 *   Offset   Size    Name              说明
 *   ─────────────────────────────────────────────────────
 *   0x0000    4B     upgrade_flag       升级状态魔数
 *   0x0004    4B     protect_flag       保护标志魔数
 *   0x0010   24B     current_ver        当前运行固件版本
 *   0x0030   24B     new_ver            新固件版本 (OTA 目标)
 *   0x0050   24B     backup_ver         备份固件版本
 *   0x0070    4B     fw_size            新固件大小 (字节)
 *   0x0080    4B     boot_fail_cnt      启动失败计数
 *   0x0100    4B     downloaded_size    已下载字节数 (断点续传)
 *   0x0200   64B     token              OTA 鉴权 token 备份
 *   0x0240   33B     file_md5           固件 MD5 值 (32字符+'\0')
 *
 * ===========================================================================
 * 升级状态机
 * ===========================================================================
 *   NONE ──(OTA下载完成)──-> READY ──(BootLoader搬运完成)──-> COMMITTED
 *     ↑                                                       │
 *     └────────────(APP确认新固件正常)────────────────────────┘
 *
 *   魔数值:
 *     NONE      = 0x00000000  无升级任务
 *     READY     = 0xAA55AA55  Zone1 已存新固件, BootLoader 应执行搬运
 *     COMMITTED = 0x55AA55AA  固件已搬运到 APP 区, 等待 APP 确认
 */

#ifndef __OTA_FLAGS_H
#define __OTA_FLAGS_H

#include "w25q128.h"
#include <stdint.h>

/*==========================================================================
 * 1. 分区基地址
 *==========================================================================*/

/** @brief 标志区基地址 (W25Q128 分区 3 起始) */
#define FLAGS_BASE_ADDR 0x00C00000

/** @brief 标志区单扇区大小 (所有标志集中在首个 4KB 扇区内) */
#define FLAGS_SECTOR_SIZE 4096

/*==========================================================================
 * 2. 字段偏移地址 (相对于 FLAGS_BASE_ADDR)
 *==========================================================================*/

#define FLAGS_UPGRADE_FLAG (FLAGS_BASE_ADDR + 0x0000)    /**< 升级标志     */
#define FLAGS_PROTECT_FLAG (FLAGS_BASE_ADDR + 0x0004)    /**< 保护标志     */
#define FLAGS_CURRENT_VER (FLAGS_BASE_ADDR + 0x0010)     /**< 当前版本     */
#define FLAGS_NEW_VER (FLAGS_BASE_ADDR + 0x0030)         /**< 新固件版本   */
#define FLAGS_BACKUP_VER (FLAGS_BASE_ADDR + 0x0050)      /**< 备份版本     */
#define FLAGS_FW_SIZE (FLAGS_BASE_ADDR + 0x0070)         /**< 固件大小     */
#define FLAGS_BOOT_FAIL_CNT (FLAGS_BASE_ADDR + 0x0080)   /**< 启动失败计数 */
#define FLAGS_BACKUP_FW_SIZE (FLAGS_BASE_ADDR + 0x0090)  /**< 备份固件大小   */
#define FLAGS_DOWNLOADED_SIZE (FLAGS_BASE_ADDR + 0x0100) /**< 已下载字节数 */
#define FLAGS_TOKEN (FLAGS_BASE_ADDR + 0x0200)           /**< Token 备份   */
#define FLAGS_FILE_MD5 (FLAGS_BASE_ADDR + 0x0240)        /**< 固件 MD5     */

/*==========================================================================
 * 3. 升级状态魔数
 *==========================================================================*/

#define UPGRADE_MAGIC_NONE 0x00000000      /**< 无升级任务, 空闲状态           */
#define UPGRADE_MAGIC_READY 0xAA55AA55     /**< Zone1 已存新固件, 等待 BootLoader */
#define UPGRADE_MAGIC_COMMITTED 0x55AA55AA /**< BootLoader 已完成搬运, 等待 APP 确认 */

/*==========================================================================
 * 4. 保护标志魔数
 *==========================================================================*/

#define PROTECT_MAGIC_NONE 0x00000000 /**< 未确认 / 已回滚到旧版本 */
#define PROTECT_MAGIC_OK 0xBB66BB66   /**< APP 确认新固件运行正常    */

/*==========================================================================
 * 5. 基础读写 API (底层, 一般不直接调用)
 *==========================================================================*/

/**
 * @brief  从标志区指定偏移直接读取 (W25Q128 支持随机读)
 * @param  offset 扇区内偏移 (0 ~ FLAGS_SECTOR_SIZE-1)
 * @param  buf    读出缓冲区
 * @param  len    读取字节数
 */
void Flags_ReadBytes(uint32_t offset, uint8_t *buf, uint16_t len);

/**
 * @brief  向标志区写入 — 核心 R-M-E-W 操作
 * @note   内部执行: 读整扇区 -> 改目标字节 -> 擦除 -> 写回
 * @param  offset 扇区内偏移
 * @param  data   写入数据
 * @param  len    写入字节数
 */
void Flags_WriteBytes(uint32_t offset, const uint8_t *data, uint16_t len);

/*==========================================================================
 * 6. 升级标志 API
 *==========================================================================*/

uint32_t Flags_ReadUpgradeFlag(void);     /**< 读取升级状态魔数         */
void Flags_SetUpgradeFlag(uint32_t flag); /**< 设置升级状态魔数         */

uint32_t Flags_ReadProtectFlag(void);     /**< 读取保护标志             */
void Flags_SetProtectFlag(uint32_t flag); /**< 设置保护标志             */

/*==========================================================================
 * 7. 版本号 API (24 字节定长字符串, 如 "V2.0.0\0")
 *==========================================================================*/

void Flags_ReadCurrentVer(char *ver_buf);    /**< 读取当前运行版本          */
void Flags_WriteCurrentVer(const char *ver); /**< 写入当前运行版本          */

void Flags_ReadNewVer(char *ver_buf);    /**< 读取 OTA 目标版本         */
void Flags_WriteNewVer(const char *ver); /**< 写入 OTA 目标版本         */

void Flags_ReadBackupVer(char *ver_buf);    /**< 读取备份固件版本          */
void Flags_WriteBackupVer(const char *ver); /**< 写入备份固件版本          */

/*==========================================================================
 * 8. 固件大小 / 启动计数 / 断点续传 API
 *==========================================================================*/

uint32_t Flags_ReadFwSize(void);       /**< 读取新固件文件大小         */
void Flags_WriteFwSize(uint32_t size); /**< 写入新固件文件大小         */

uint32_t Flags_ReadBootFailCnt(void); /**< 读取启动失败计数          */
void Flags_IncBootFailCnt(void);      /**< 启动失败计数 +1           */
void Flags_ClearBootFailCnt(void);    /**< 清零启动失败计数          */

uint32_t Flags_ReadBackupFwSize(void);       /**< 读取备份固件大小          */
void Flags_WriteBackupFwSize(uint32_t size); /**< 写入备份固件大小          */

uint32_t Flags_ReadDownloadedSize(void);      /**< 读取已下载字节数 (断点)   */
void Flags_SaveDownloadedSize(uint32_t size); /**< 保存已下载字节数 (断点)   */

/*==========================================================================
 * 9. Token / MD5 存取 API
 *==========================================================================*/

void Flags_SaveToken(const char *token); /**< 保存 OTA 鉴权 token       */
void Flags_ReadToken(char *token_buf);   /**< 读取 OTA 鉴权 token       */

void Flags_SaveMd5(const char *md5); /**< 保存固件 MD5 (32字符hex)  */
void Flags_ReadMd5(char *md5_buf);   /**< 读取固件 MD5              */

/*==========================================================================
 * 10. 整区管理 & 调试
 *==========================================================================*/

/**
 * @brief  格式化标志区 — 擦除首扇区 (所有字段归零/0xFF)
 */
void Flags_Format(void);

/**
 * @brief  打印所有标志区字段 (调试用, 串口输出格式化表格)
 */
void Flags_PrintAll(void);

#endif /* __OTA_FLAGS_H */

/**
 * @file    bootloader.c
 * @brief   BootLoader 固件升级模块 — 实现
 *
 * ===========================================================================
 * 依赖
 * ===========================================================================
 *   bootloader.c
 *     ├── w25q128.h       — W25Q128 外部 Flash 读写擦 API
 *     ├── stm32f4xx_hal.h — HAL 库 (内部 Flash 操作、Delay、UART DeInit)
 *     └── usart.h         — huart2 句柄 (跳转前 DeInit)
 *
 * ===========================================================================
 * STM32F407 Flash 扇区布局
 * ===========================================================================
 *   Sector 0~3:  16KB each  (BootLoader 区, 0x08000000-0x0800FFFF)
 *   Sector 4:    64KB       (APP 起始,   0x08010000-0x0801FFFF)
 *   Sector 5~11: 128KB each (APP 其余,   0x08020000-0x080FFFFF)
 *
 * ===========================================================================
 * W25Q128 分区布局 (与 APP ota_flags.h 保持一致)
 * ===========================================================================
 *   Zone1: 0x000000-0x5FFFFF (6MB) — 固件下载区
 *   Zone2: 0x600000-0xBFFFFF (6MB) — 备份区
 *   Zone3: 0xC00000-0xC0FFFF (64KB) — OTA 标志区
 */

#include "bootloader.h"

#include "w25q128.h"
#include "stm32f4xx_hal.h"
#include "usart.h"
#include "iwdg.h"
#include <stdio.h>
#include <string.h>

/*==========================================================================
 * 1. 常量定义
 *==========================================================================*/

/** @brief STM32 APP 固件起始地址 (Sector 4) */
#define FLASH_APP_ADDR          0x08010000U

/** @brief STM32 Flash 结束地址 */
#define FLASH_END_ADDR          0x08100000U

/** @brief 固件头部验证: SP 必须指向 SRAM (0x20000000 ~ 0x20030000) */
#define FW_HEADER_SP_MASK       0x2FFE0000U
#define FW_HEADER_SP_EXPECTED  0x20000000U

/* ---- W25Q128 OTA 标志区地址 ---- */
#define FLAGS_BASE_ADDR         0x00C00000U
#define FLAGS_UPGRADE_OFFSET    0x0000
#define FLAGS_PROTECT_OFFSET    0x0004
#define FLAGS_CURRENT_VER       0x0010
#define FLAGS_NEW_VER           0x0030
#define FLAGS_BACKUP_VER        0x0050
#define FLAGS_FW_SIZE           0x0070
#define FLAGS_BOOT_FAIL_CNT     0x0080
#define FLAGS_BACKUP_FW_SIZE    0x0090
#define FLAGS_DOWNLOADED_SIZE   0x0100
#define FLAGS_FILE_MD5          0x0240

/* ---- 升级状态魔数 ---- */
#define UPGRADE_MAGIC_NONE      0x00000000U
#define UPGRADE_MAGIC_READY     0xAA55AA55U
#define UPGRADE_MAGIC_COMMITTED 0x55AA55AAU

/* ---- 保护标志魔数 ---- */
#define PROTECT_MAGIC_NONE      0x00000000U
#define PROTECT_MAGIC_OK        0xBB66BB66U

/* ---- 回滚阈值 ---- */
#define BOOT_FAIL_MAX           3

/* ---- W25Q128 分区地址 ---- */
#define ZONE1_BASE              0x00000000U
#define ZONE2_BASE              0x00600000U

/** @brief Flash 搬运缓冲区大小 (一个 W25Q128 扇区) */
#define COPY_BUF_SIZE           4096

/** @brief 跳转函数指针类型 */
typedef void (*pFunction)(void);

/*==========================================================================
 * 2. 静态变量 (不在栈上, 避免溢出)
 *==========================================================================*/

static uint8_t __attribute__((aligned(4))) copy_buf[COPY_BUF_SIZE];
static uint8_t __attribute__((aligned(4))) flags_buf[COPY_BUF_SIZE];

/*==========================================================================
 * 3. 内部辅助函数声明
 *==========================================================================*/

static uint32_t GetSector(uint32_t addr);
static uint32_t GetSectorSize(uint32_t sector);
static int      EraseAppArea(uint32_t start_addr, uint32_t size);
static void     SetCommittedFlag(const char *new_ver);
static void     PrintFlag(const char *name, uint32_t val,
                          uint32_t none_val, const char *none_str,
                          uint32_t ok_val,   const char *ok_str,
                          uint32_t rdy_val,  const char *rdy_str);

/*==========================================================================
 * 4. 扇区辅助函数
 *==========================================================================*/

static uint32_t GetSector(uint32_t addr)
{
    if      (addr < 0x08004000) return FLASH_SECTOR_0;
    else if (addr < 0x08008000) return FLASH_SECTOR_1;
    else if (addr < 0x0800C000) return FLASH_SECTOR_2;
    else if (addr < 0x08010000) return FLASH_SECTOR_3;
    else if (addr < 0x08020000) return FLASH_SECTOR_4;
    else if (addr < 0x08040000) return FLASH_SECTOR_5;
    else if (addr < 0x08060000) return FLASH_SECTOR_6;
    else if (addr < 0x08080000) return FLASH_SECTOR_7;
    else if (addr < 0x080A0000) return FLASH_SECTOR_8;
    else if (addr < 0x080C0000) return FLASH_SECTOR_9;
    else if (addr < 0x080E0000) return FLASH_SECTOR_10;
    else                        return FLASH_SECTOR_11;
}

static uint32_t GetSectorSize(uint32_t sector)
{
    if (sector <= FLASH_SECTOR_3)       return 16 * 1024;
    else if (sector == FLASH_SECTOR_4)  return 64 * 1024;
    else                                return 128 * 1024;
}

/*==========================================================================
 * 5. 内部 Flash 擦除
 *==========================================================================*/

static int EraseAppArea(uint32_t start_addr, uint32_t size)
{
    FLASH_EraseInitTypeDef cfg;
    uint32_t sector_error = 0;
    uint32_t addr      = start_addr;
    uint32_t remaining = size;

    HAL_FLASH_Unlock();

    while (remaining > 0 && addr < FLASH_END_ADDR)
    {
        uint32_t sector   = GetSector(addr);
        uint32_t sec_size = GetSectorSize(sector);

        cfg.TypeErase    = FLASH_TYPEERASE_SECTORS;
        cfg.Sector       = sector;
        cfg.NbSectors    = 1;
        cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        printf("[BOOT] Erasing sector %lu (0x%08lX, %lu KB)...\r\n",
               (unsigned long)sector, (unsigned long)addr,
               (unsigned long)(sec_size / 1024));

        if (HAL_FLASHEx_Erase(&cfg, &sector_error) != HAL_OK)
        {
            printf("[BOOT] ERROR: Sector %lu erase failed! code=0x%08lX\r\n",
                   (unsigned long)sector, (unsigned long)sector_error);
            HAL_FLASH_Lock();
            return -1;
        }

        HAL_IWDG_Refresh(&hiwdg);  /* 喂狗, 防止擦除超时复位 */
        addr += sec_size;
        remaining = (remaining > sec_size) ? (remaining - sec_size) : 0;
    }

    HAL_FLASH_Lock();
    printf("[BOOT] APP area erase complete\r\n");
    return 0;
}

/*==========================================================================
 * 6. COMMITTED 标志 + CURRENT_VER 写入 (R-M-E-W)
 *==========================================================================*/

static void SetCommittedFlag(const char *new_ver)
{
    uint32_t committed   = UPGRADE_MAGIC_COMMITTED;
    uint32_t none        = UPGRADE_MAGIC_NONE;
    uint32_t zero        = 0;
    uint32_t vf;
    char vv[25] = {0};

    printf("[BOOT] Setting COMMITTED + resetting protect/fail_cnt + updating CURRENT_VER...\r\n");

    /* ① 读 */
    W25Q128_Read(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));

    /* ②a 改 upgrade_flag → COMMITTED */
    memcpy(&flags_buf[FLAGS_UPGRADE_OFFSET], &committed, 4);

    /* ②b 关键: 清除 protect_flag 和 boot_fail_cnt, 迫使新固件必须自行确认 */
    memcpy(&flags_buf[FLAGS_PROTECT_OFFSET], &none, 4);
    memcpy(&flags_buf[FLAGS_BOOT_FAIL_CNT], &zero, 4);

    /* ②c 改 current_ver → new_ver */
    memset(&flags_buf[FLAGS_CURRENT_VER], 0, 24);
    if (new_ver && new_ver[0])
        strncpy((char *)&flags_buf[FLAGS_CURRENT_VER], new_ver, 23);

    /* ③ 擦除 + ④ 写回 */
    W25Q128_SectorErase(FLAGS_BASE_ADDR);
    W25Q128_Write(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));

    /* 验证 */
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_UPGRADE_OFFSET, (uint8_t *)&vf, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_CURRENT_VER,    (uint8_t *)vv, 24);
    vv[24] = '\0';

    printf("[BOOT] COMMITTED=%s  current_ver=\"%s\"\r\n",
           (vf == UPGRADE_MAGIC_COMMITTED) ? "OK" : "FAIL!", vv);
}

/*==========================================================================
 * 7. 标志位打印辅助
 *==========================================================================*/

/** @brief 打印一个 hex 标志位, 并标注可读名称 */
static void PrintFlag(const char *name, uint32_t val,
                      uint32_t none_val, const char *none_str,
                      uint32_t ok_val,   const char *ok_str,
                      uint32_t rdy_val,  const char *rdy_str)
{
    printf("%-16s = 0x%08lX", name, (unsigned long)val);

    if      (val == none_val) printf(" (%s)\r\n", none_str);
    else if (val == ok_val)   printf(" (%s)\r\n", ok_str);
    else if (val == rdy_val)  printf(" (%s)\r\n", rdy_str);
    else                      printf(" (GARBAGE)\r\n");
}

/*==========================================================================
 * 8. 跳转函数
 *==========================================================================*/

void Jump_to_APP(uint32_t app_addr)
{
    uint32_t jump_addr;
    printf("jump to app: %#x\r\n", FLASH_APP_ADDR);

    if (((*(volatile uint32_t *)FLASH_APP_ADDR) & FW_HEADER_SP_MASK)
        == FW_HEADER_SP_EXPECTED)
    {
        HAL_UART_DeInit(&huart2);
        HAL_DeInit();
        SysTick->CTRL = 0;

        __set_PRIMASK(1);
        jump_addr = *(volatile uint32_t *)(FLASH_APP_ADDR + 4);
        printf("jump %#x success\r\n", FLASH_APP_ADDR);
        __set_MSP(*(volatile uint32_t *)FLASH_APP_ADDR);

        ((pFunction)jump_addr)();
    }
    else
    {
        printf("error [0x%08lx]\r\n",
               (unsigned long)(*(volatile uint32_t *)FLASH_APP_ADDR));
    }
}

/*==========================================================================
 * 9. 标志区单字段写入 (R-M-E-W, 保留其他字段)
 *==========================================================================*/

static void WriteFlagsWord(uint32_t offset, uint32_t value)
{
    W25Q128_Read(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));
    memcpy(&flags_buf[offset], &value, 4);
    W25Q128_SectorErase(FLAGS_BASE_ADDR);
    W25Q128_Write(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));
}

/*==========================================================================
 * 10. 升级确认完成 → 清除 upgrade/protect/boot_fail_cnt
 *==========================================================================*/

static void ClearUpgradeComplete(void)
{
    uint32_t none = UPGRADE_MAGIC_NONE;
    uint32_t zero = 0;

    W25Q128_Read(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));
    memcpy(&flags_buf[FLAGS_UPGRADE_OFFSET], &none, 4);
    memcpy(&flags_buf[FLAGS_PROTECT_OFFSET], &none, 4);
    memcpy(&flags_buf[FLAGS_BOOT_FAIL_CNT], &zero, 4);
    W25Q128_SectorErase(FLAGS_BASE_ADDR);
    W25Q128_Write(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));

    printf("[BOOT] Flags cleared → NONE (upgrade confirmed)\r\n");
}

/*==========================================================================
 * 11. 回滚 — Zone2 备份固件 → APP Flash
 *==========================================================================*/

static void DoRollback(const char *bk_ver, uint32_t bk_size)
{
    uint32_t offset_rb, prog_err_rb;
    uint32_t bk_sp, bk_pc;

    printf("\r\n[BOOT] ========== ROLLBACK START ==========\r\n");
    printf("[BOOT] Source: Zone2, ver=%s, size=%lu bytes\r\n",
           bk_ver, (unsigned long)bk_size);

    /* 1. 验证备份固件头 */
    W25Q128_Read(ZONE2_BASE,     (uint8_t *)&bk_sp, 4);
    W25Q128_Read(ZONE2_BASE + 4, (uint8_t *)&bk_pc, 4);
    printf("[BOOT] Backup header: SP=0x%08lX PC=0x%08lX\r\n",
           (unsigned long)bk_sp, (unsigned long)bk_pc);

    if ((bk_sp & FW_HEADER_SP_MASK) != FW_HEADER_SP_EXPECTED
        || bk_size == 0 || bk_size > (ZONE2_BASE - ZONE1_BASE))
    {
        printf("[BOOT] ERROR: Backup invalid, cannot rollback!\r\n");
        return;
    }

    /* 2. 擦除 APP 区 */
    if (EraseAppArea(FLASH_APP_ADDR, bk_size) != 0)
    {
        printf("[BOOT] ERROR: Rollback erase failed!\r\n");
        return;
    }

    /* 3. 搬运 Zone2 → APP Flash */
    printf("[BOOT] Copying: W25Q128[0x%08lX] → STM32[0x%08lX]\r\n",
           (unsigned long)ZONE2_BASE, (unsigned long)FLASH_APP_ADDR);

    HAL_FLASH_Unlock();
    offset_rb   = 0;
    prog_err_rb = 0;

    while (offset_rb < bk_size && !prog_err_rb)
    {
        uint32_t chunk = (bk_size - offset_rb > sizeof(copy_buf))
                         ? sizeof(copy_buf) : (bk_size - offset_rb);

        W25Q128_Read(ZONE2_BASE + offset_rb, copy_buf, chunk);

        {
            uint32_t i;
            uint32_t words = chunk / 4;
            for (i = 0; i < words; i++)
            {
                uint32_t word;
                memcpy(&word, &copy_buf[i * 4], 4);
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                      FLASH_APP_ADDR + offset_rb + i * 4,
                                      word) != HAL_OK)
                {
                    prog_err_rb = 1;
                    break;
                }
            }
        }

        if (!prog_err_rb && (chunk % 4) != 0)
        {
            uint32_t last_word = 0;
            uint32_t words = chunk / 4;
            memcpy(&last_word, &copy_buf[words * 4], chunk % 4);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  FLASH_APP_ADDR + offset_rb + words * 4,
                                  last_word) != HAL_OK)
                prog_err_rb = 1;
        }

        offset_rb += chunk;
        HAL_IWDG_Refresh(&hiwdg);
        if ((offset_rb % (64 * 1024)) == 0 || offset_rb >= bk_size)
        {
            printf("[BOOT] Rollback: %lu / %lu (%lu%%)\r\n",
                   (unsigned long)offset_rb, (unsigned long)bk_size,
                   (unsigned long)(offset_rb * 100 / bk_size));
        }
    }
    HAL_FLASH_Lock();

    if (prog_err_rb)
    {
        printf("[BOOT] ERROR: Rollback programming failed!\r\n");
        return;
    }

    /* 4. 更新标志区: 清 upgrade/protect/fail_cnt, current_ver=backup_ver */
    {
        uint32_t none = UPGRADE_MAGIC_NONE;
        uint32_t zero = 0;

        W25Q128_Read(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));
        memcpy(&flags_buf[FLAGS_UPGRADE_OFFSET], &none, 4);
        memcpy(&flags_buf[FLAGS_PROTECT_OFFSET], &none, 4);
        memcpy(&flags_buf[FLAGS_BOOT_FAIL_CNT], &zero, 4);
        memset(&flags_buf[FLAGS_CURRENT_VER], 0, 24);
        if (bk_ver && bk_ver[0])
            strncpy((char *)&flags_buf[FLAGS_CURRENT_VER], bk_ver, 23);

        W25Q128_SectorErase(FLAGS_BASE_ADDR);
        W25Q128_Write(FLAGS_BASE_ADDR, flags_buf, sizeof(flags_buf));
    }

    printf("[BOOT] Rollback SUCCESS! current_ver=%s\r\n", bk_ver);
    printf("[BOOT] ========== ROLLBACK END ==========\r\n\r\n");
}

/*==========================================================================
 * 12. 主入口
 *==========================================================================*/

void BootLoader_Run(void)
{
    /* ── 初始化 W25Q128 ── */
    W25Q128_Init();
    printf("\r\n========== BootLoader Started ==========\r\n");

    /* ── 读标志区 ── */
    uint32_t upgrade_flag, protect_flag, fw_size, downloaded_size, boot_fail_cnt;
    uint32_t backup_fw_size, fw_sp, fw_pc, offset, prog_err;
    char current_ver[25] = {0}, new_ver[25] = {0}, backup_ver[25] = {0};
    char file_md5[34] = {0};
    int verify_ok;

    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_UPGRADE_OFFSET,    (uint8_t *)&upgrade_flag, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_PROTECT_OFFSET,    (uint8_t *)&protect_flag, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_CURRENT_VER,        (uint8_t *)current_ver, 24);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_NEW_VER,            (uint8_t *)new_ver, 24);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_BACKUP_VER,         (uint8_t *)backup_ver, 24);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_FW_SIZE,            (uint8_t *)&fw_size, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_BOOT_FAIL_CNT,      (uint8_t *)&boot_fail_cnt, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_BACKUP_FW_SIZE,     (uint8_t *)&backup_fw_size, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_DOWNLOADED_SIZE,    (uint8_t *)&downloaded_size, 4);
    W25Q128_Read(FLAGS_BASE_ADDR + FLAGS_FILE_MD5,           (uint8_t *)file_md5, 33);

    current_ver[24] = '\0';  new_ver[24] = '\0';
    backup_ver[24]  = '\0';  file_md5[33] = '\0';

    /* ── 打印标志区 ── */
    printf("\r\n========== OTA Flags Dump ==========\r\n");

    PrintFlag("upgrade_flag",   upgrade_flag,
              UPGRADE_MAGIC_NONE,      "NONE",
              UPGRADE_MAGIC_COMMITTED, "COMMITTED",
              UPGRADE_MAGIC_READY,     "READY - zone1 has firmware");

    PrintFlag("protect_flag",   protect_flag,
              PROTECT_MAGIC_NONE, "NONE",
              PROTECT_MAGIC_OK,   "OK",
              0, NULL);  /* protect 没有第3种魔数 */

    printf("current_ver     = \"%s\"\r\n",   current_ver);
    printf("new_ver         = \"%s\"\r\n",   new_ver);
    printf("backup_ver      = \"%s\"\r\n",   backup_ver);
    printf("fw_size         = %lu bytes\r\n", (unsigned long)fw_size);
    printf("backup_fw_size  = %lu bytes\r\n", (unsigned long)backup_fw_size);
    printf("boot_fail_cnt   = %lu\r\n",       (unsigned long)boot_fail_cnt);
    printf("downloaded_size = %lu bytes\r\n", (unsigned long)downloaded_size);
    printf("file_md5        = \"%s\"\r\n",   file_md5);
    printf("=====================================\r\n\r\n");

    /* ═══════════════════════════════════════════════
     * 决策 A: COMMITTED → APP 是否已确认正常运行?
     * ═══════════════════════════════════════════════ */
    if (upgrade_flag == UPGRADE_MAGIC_COMMITTED)
    {
        if (protect_flag == PROTECT_MAGIC_OK)
        {
            /* APP 确认新固件正常 → 升级成功, 清理标志 */
            printf("[BOOT] APP confirmed OK → upgrade SUCCESS\r\n");
            ClearUpgradeComplete();
        }
        else
        {
            /* APP 未确认 → 可能崩了, 累加失败计数 */
            boot_fail_cnt++;
            printf("[BOOT] APP NOT confirmed! fail_cnt=%lu/%d\r\n",
                   (unsigned long)boot_fail_cnt, BOOT_FAIL_MAX);
            WriteFlagsWord(FLAGS_BOOT_FAIL_CNT, boot_fail_cnt);

            if (boot_fail_cnt >= BOOT_FAIL_MAX)
            {
                /* 超过阈值 → 回滚到备份固件 */
                if (backup_ver[0] != '\0' && backup_ver[0] != 0xFF
                    && backup_fw_size > 0)
                {
                    DoRollback(backup_ver, backup_fw_size);
                    /* 回滚完成后复位, 让新(旧)固件运行 */
                    printf("[BOOT] Rollback done, rebooting...\r\n");
                    /* 缩短 IWDG 超时 + 停止喂狗, 看门狗复位 */
                    IWDG->KR = 0x5555;
                    IWDG->PR = 0x00;
                    IWDG->RLR = 4000;
                    IWDG->KR = 0xAAAA;
                    while (1);
                }
                else
                {
                    printf("[BOOT] No valid backup, skip rollback\r\n");
                    ClearUpgradeComplete();  /* 没法回滚, 清标志继续 */
                }
            }
        }
        Jump_to_APP(FLASH_APP_ADDR);
    }

    /* ── 决策 B: READY → 没有待升级固件 → 直接跳 APP ── */
    if (upgrade_flag != UPGRADE_MAGIC_READY)
    {
        printf("[BOOT] No firmware to flash, jumping to APP...\r\n");
        Jump_to_APP(FLASH_APP_ADDR);
    }

    /* ═══════════════════════════════════════════════
     * READY: 搬运 Zone1 → APP Flash
     * ═══════════════════════════════════════════════ */
    printf("[BOOT] ===== Firmware Upgrade Started =====\r\n");
    printf("[BOOT] Target: v%s, Size: %lu bytes\r\n",
           new_ver, (unsigned long)fw_size);

    /* 1. 验证固件头 */
    W25Q128_Read(ZONE1_BASE,     (uint8_t *)&fw_sp, 4);
    W25Q128_Read(ZONE1_BASE + 4, (uint8_t *)&fw_pc, 4);
    printf("[BOOT] Header: SP=0x%08lX PC=0x%08lX\r\n",
           (unsigned long)fw_sp, (unsigned long)fw_pc);

    if (fw_size == 0 || fw_size > (ZONE2_BASE - ZONE1_BASE))
    {
        printf("[BOOT] ERROR: Invalid firmware size, aborting!\r\n");
        Jump_to_APP(FLASH_APP_ADDR);
    }
    if ((fw_sp & FW_HEADER_SP_MASK) != FW_HEADER_SP_EXPECTED)
    {
        printf("[BOOT] ERROR: Bad stack pointer, firmware corrupt!\r\n");
        Jump_to_APP(FLASH_APP_ADDR);
    }

    /* 2. 擦除 APP 区 */
    if (EraseAppArea(FLASH_APP_ADDR, fw_size) != 0)
    {
        printf("[BOOT] ERROR: Flash erase failed!\r\n");
        Jump_to_APP(FLASH_APP_ADDR);
    }

    /* 3. 搬运 */
    printf("[BOOT] Copying: W25Q128[0x%08lX] → STM32[0x%08lX]\r\n",
           (unsigned long)ZONE1_BASE, (unsigned long)FLASH_APP_ADDR);

    HAL_FLASH_Unlock();
    offset   = 0;
    prog_err = 0;

    while (offset < fw_size && !prog_err)
    {
        uint32_t chunk = (fw_size - offset > sizeof(copy_buf))
                         ? sizeof(copy_buf) : (fw_size - offset);

        W25Q128_Read(ZONE1_BASE + offset, copy_buf, chunk);

        uint32_t words = chunk / 4;
        {
            uint32_t i;
            for (i = 0; i < words; i++)
            {
                uint32_t word;
                memcpy(&word, &copy_buf[i * 4], 4);
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                      FLASH_APP_ADDR + offset + i * 4,
                                      word) != HAL_OK)
                {
                    prog_err = 1;
                    break;
                }
            }
        }

        if (!prog_err && (chunk % 4) != 0)
        {
            uint32_t last_word = 0;
            memcpy(&last_word, &copy_buf[words * 4], chunk % 4);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  FLASH_APP_ADDR + offset + words * 4,
                                  last_word) != HAL_OK)
                prog_err = 1;
        }

        offset += chunk;
        HAL_IWDG_Refresh(&hiwdg);  /* 喂狗, 防止搬运超时复位 */
        if ((offset % (64 * 1024)) == 0 || offset >= fw_size)
        {
            printf("[BOOT] Progress: %lu / %lu bytes (%lu%%)\r\n",
                   (unsigned long)offset, (unsigned long)fw_size,
                   (unsigned long)(offset * 100 / fw_size));
        }
    }
    HAL_FLASH_Lock();

    if (prog_err)
    {
        printf("[BOOT] ERROR: Flash programming failed at offset %lu!\r\n",
               (unsigned long)offset);
        Jump_to_APP(FLASH_APP_ADDR);
    }

    /* 4. 验证 */
    printf("[BOOT] Verifying flashed data...\r\n");
    W25Q128_Read(ZONE1_BASE, copy_buf, 1024);
    verify_ok = 1;
    {
        uint32_t i;
        for (i = 0; i < 1024; i++)
        {
            if (copy_buf[i] != *(volatile uint8_t *)(FLASH_APP_ADDR + i))
            { verify_ok = 0; break; }
        }
    }

    if (!verify_ok)
    {
        printf("[BOOT] ERROR: Verification failed!\r\n");
        Jump_to_APP(FLASH_APP_ADDR);
    }
    printf("[BOOT] Verify OK (first 1KB matches)\r\n");

    /* 5. 设 COMMITTED + 重启 */
    SetCommittedFlag(new_ver);
    printf("[BOOT] Upgrade SUCCESS! Rebooting...\r\n");
    /* 缩短 IWDG 超时 + 停止喂狗, 看门狗复位 */
    IWDG->KR = 0x5555;
    IWDG->PR = 0x00;
    IWDG->RLR = 4000;
    IWDG->KR = 0xAAAA;
    while (1);
}

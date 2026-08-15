/**
 * @file    ota_flags.c
 * @brief   OTA 标志区管理模块 — 实现
 *
 * ===========================================================================
 * R-M-E-W (读-改-擦-写) 机制
 * ===========================================================================
 *   W25Q128 最小擦除单位 = 4KB 扇区, 不支持字节级覆盖,
 *   因此所有写入操作必须:
 *     ① Read   — 把整个 4KB 扇区读到 RAM 缓冲区 sector_buf[]
 *     ② Modify — 在缓冲区中修改目标字节
 *     ③ Erase  — 擦除该 4KB 扇区
 *     ④ Write  — 将修改后的 4KB 缓冲区写回 Flash
 *   -> 确保同一扇区内其他字段不受影响。
 *
 * ===========================================================================
 * 扇区缓冲区
 * ===========================================================================
 *   static uint8_t sector_buf[4096] — 4KB RAM 缓冲区
 *   所有上层 API 共享, 通过 Flags_WriteBytes() 统一执行 R-M-E-W。
 */

#include "ota_flags.h"
#include <stdio.h>
#include <string.h>

/*==========================================================================
 * 内部辅助：4KB 扇区缓冲区 (static 避免占用任务栈)
 *==========================================================================*/
static uint8_t sector_buf[FLAGS_SECTOR_SIZE];

/*==========================================================================
 * 4. 基础读写实现 — 所有上层 API 的底层
 *==========================================================================*/

/**
 * @brief  从标志区直接读取 (W25Q128 支持任意地址随机读)
 * @param  offset 偏移地址 (相对于标志区起始)
 * @param  buf    目标缓冲区
 * @param  len    读取字节数
 */
void Flags_ReadBytes(uint32_t offset, uint8_t *buf, uint16_t len)
{
    W25Q128_Read(FLAGS_BASE_ADDR + offset, buf, len);
}

/**
 * @brief  写标志区 — 核心：读-改-擦-写
 * @note   操作流程:
 *         ① 读整个4KB扇区到 sector_buf
 *         ② 在 sector_buf 中修改目标字节
 *         ③ 擦除该4KB扇区
 *         ④ 将 sector_buf 整体写回
 *         => 保证同扇区内其他字段完整保留
 * @param  offset 偏移地址 (相对于标志区起始)
 * @param  data   待写入数据缓冲区
 * @param  len    写入字节数
 */
void Flags_WriteBytes(uint32_t offset, const uint8_t *data, uint16_t len)
{
    uint32_t off = offset; /* 扇区内偏移 */

    /* ① 读取整个4KB扇区 */
    W25Q128_Read(FLAGS_BASE_ADDR, sector_buf, FLAGS_SECTOR_SIZE);

    /* ② 在缓冲区中修改目标数据 */
    for (uint16_t i = 0; i < len; i++)
    {
        sector_buf[off + i] = data[i];
    }

    /* ③ 擦除扇区 */
    W25Q128_SectorErase(FLAGS_BASE_ADDR);

    /* ④ 整区写回 (W25Q128_Write 内部自动处理跨页) */
    W25Q128_Write(FLAGS_BASE_ADDR, sector_buf, FLAGS_SECTOR_SIZE);
}

/*==========================================================================
 * 5. 升级标志位实现
 *==========================================================================*/

/**
 * @brief  读取升级状态魔数 (offset 0x0000)
 * @return 当前升级状态: UPGRADE_MAGIC_NONE / READY / COMMITTED
 */
uint32_t Flags_ReadUpgradeFlag(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0000, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  设置升级状态魔数 (offset 0x0000)
 * @param  flag 待写入的升级状态魔数 (UPGRADE_MAGIC_*)
 */
void Flags_SetUpgradeFlag(uint32_t flag)
{
    Flags_WriteBytes(0x0000, (const uint8_t *)&flag, 4);
}

/**
 * @brief  读取保护标志魔数 (offset 0x0004)
 * @return 当前保护标志: PROTECT_MAGIC_NONE / OK
 */
uint32_t Flags_ReadProtectFlag(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0004, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  设置保护标志魔数 (offset 0x0004)
 * @param  flag 待写入的保护标志魔数 (PROTECT_MAGIC_*)
 */
void Flags_SetProtectFlag(uint32_t flag)
{
    Flags_WriteBytes(0x0004, (const uint8_t *)&flag, 4);
}

/*==========================================================================
 * 6. 版本号实现 (24字节定长字符串)
 *==========================================================================*/

/**
 * @brief  读取当前运行固件版本 (offset 0x0010, 24B 定长字符串)
 * @param  ver_buf 输出缓冲区, 至少 24 字节, 末尾已强制补 '\0'
 */
void Flags_ReadCurrentVer(char *ver_buf)
{
    Flags_ReadBytes(0x0010, (uint8_t *)ver_buf, 24);
    ver_buf[23] = '\0'; /* 确保字符串终止 */
}

/**
 * @brief  写入当前运行固件版本 (offset 0x0010)
 * @param  ver 待写入版本字符串, 最多拷贝 23 字符, 超长自动截断
 */
void Flags_WriteCurrentVer(const char *ver)
{
    uint8_t buf[24] = {0};
    if (ver)
    {
        strncpy((char *)buf, ver, 23);
    }
    Flags_WriteBytes(0x0010, buf, 24);
}

/**
 * @brief  读取 OTA 目标版本号 (offset 0x0030, 24B 定长字符串)
 * @param  ver_buf 输出缓冲区, 至少 24 字节, 末尾已强制补 '\0'
 */
void Flags_ReadNewVer(char *ver_buf)
{
    Flags_ReadBytes(0x0030, (uint8_t *)ver_buf, 24);
    ver_buf[23] = '\0';
}

/**
 * @brief  写入 OTA 目标版本号 (offset 0x0030)
 * @param  ver 待写入版本字符串, 最多拷贝 23 字符, 超长自动截断
 */
void Flags_WriteNewVer(const char *ver)
{
    uint8_t buf[24] = {0};
    if (ver)
    {
        strncpy((char *)buf, ver, 23);
    }
    Flags_WriteBytes(0x0030, buf, 24);
}

/**
 * @brief  读取备份固件版本 (offset 0x0050, 24B 定长字符串)
 * @param  ver_buf 输出缓冲区, 至少 24 字节, 末尾已强制补 '\0'
 */
void Flags_ReadBackupVer(char *ver_buf)
{
    Flags_ReadBytes(0x0050, (uint8_t *)ver_buf, 24);
    ver_buf[23] = '\0';
}

/**
 * @brief  写入备份固件版本 (offset 0x0050)
 * @param  ver 待写入版本字符串, 最多拷贝 23 字符, 超长自动截断
 */
void Flags_WriteBackupVer(const char *ver)
{
    uint8_t buf[24] = {0};
    if (ver)
    {
        strncpy((char *)buf, ver, 23);
    }
    Flags_WriteBytes(0x0050, buf, 24);
}

/*==========================================================================
 * 7. 固件大小 / 失败计数 / 断点续传实现
 *==========================================================================*/

/**
 * @brief  读取新固件文件大小 (offset 0x0070)
 * @return 新固件字节数
 */
uint32_t Flags_ReadFwSize(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0070, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  写入新固件文件大小 (offset 0x0070)
 * @param  size 新固件字节数
 */
void Flags_WriteFwSize(uint32_t size)
{
    Flags_WriteBytes(0x0070, (const uint8_t *)&size, 4);
}

/**
 * @brief  读取启动失败计数 (offset 0x0080)
 * @return 累计启动失败次数
 */
uint32_t Flags_ReadBootFailCnt(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0080, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  启动失败计数 +1 (offset 0x0080)
 * @note   用于看门狗复位后的回滚判断, 连续失败达到阈值即触发回滚
 */
void Flags_IncBootFailCnt(void)
{
    uint32_t cnt = Flags_ReadBootFailCnt();
    cnt++;
    Flags_WriteBytes(0x0080, (const uint8_t *)&cnt, 4);
}

/**
 * @brief  清零启动失败计数 (offset 0x0080)
 * @note   新固件被确认正常启动后调用, 重置回滚计数
 */
void Flags_ClearBootFailCnt(void)
{
    uint32_t zero = 0;
    Flags_WriteBytes(0x0080, (const uint8_t *)&zero, 4);
}

/**
 * @brief  读取备份固件大小 (offset 0x0090)
 * @return 备份固件字节数
 */
uint32_t Flags_ReadBackupFwSize(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0090, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  写入备份固件大小 (offset 0x0090)
 * @param  size 备份固件字节数
 */
void Flags_WriteBackupFwSize(uint32_t size)
{
    Flags_WriteBytes(0x0090, (const uint8_t *)&size, 4);
}

/**
 * @brief  读取已下载字节数 (offset 0x0100, 断点续传)
 * @return 本次 OTA 已下载的字节数
 */
uint32_t Flags_ReadDownloadedSize(void)
{
    uint32_t val;
    Flags_ReadBytes(0x0100, (uint8_t *)&val, 4);
    return val;
}

/**
 * @brief  保存已下载字节数 (offset 0x0100, 断点续传)
 * @param  size 本次 OTA 已下载的字节数
 */
void Flags_SaveDownloadedSize(uint32_t size)
{
    Flags_WriteBytes(0x0100, (const uint8_t *)&size, 4);
}

/*==========================================================================
 * 8. Token / MD5 存取实现
 *==========================================================================*/

/**
 * @brief  保存 OTA 鉴权 token (offset 0x0200, 64B)
 * @param  token 待保存 token 字符串, 最多拷贝 63 字符, 超长自动截断
 */
void Flags_SaveToken(const char *token)
{
    uint8_t buf[64] = {0};
    if (token)
    {
        strncpy((char *)buf, token, 63);
    }
    Flags_WriteBytes(0x0200, buf, 64);
}

/**
 * @brief  读取 OTA 鉴权 token (offset 0x0200, 64B)
 * @param  token_buf 输出缓冲区, 至少 64 字节, 末尾已强制补 '\0'
 */
void Flags_ReadToken(char *token_buf)
{
    Flags_ReadBytes(0x0200, (uint8_t *)token_buf, 64);
    token_buf[63] = '\0';
}

/**
 * @brief  保存固件 MD5 值 (offset 0x0240, 33B)
 * @param  md5 待保存的 32 字符十六进制 MD5 字符串, 超长自动截断
 */
void Flags_SaveMd5(const char *md5)
{
    uint8_t buf[33] = {0};
    if (md5)
    {
        strncpy((char *)buf, md5, 32);
    }
    Flags_WriteBytes(0x0240, buf, 33);
}

/**
 * @brief  读取固件 MD5 值 (offset 0x0240, 33B)
 * @param  md5_buf 输出缓冲区, 至少 33 字节, 末尾已强制补 '\0'
 */
void Flags_ReadMd5(char *md5_buf)
{
    Flags_ReadBytes(0x0240, (uint8_t *)md5_buf, 33);
    md5_buf[32] = '\0';
}

/*==========================================================================
 * 9. 整区管理 & 调试
 *==========================================================================*/

/**
 * @brief  格式化标志区 — 擦除首扇区，所有字段归零
 */
void Flags_Format(void)
{
    W25Q128_SectorErase(FLAGS_BASE_ADDR);
}

/**
 * @brief  打印所有标志区内容 (调试用)
 */
void Flags_PrintAll(void)
{
    char ver[25];
    char token[65];
    char md5[34];

    printf("\r\n========== OTA 标志区内容 ==========\r\n");

    uint32_t upgrade = Flags_ReadUpgradeFlag();
    printf("  upgrade_flag    = 0x%08X", (unsigned int)upgrade);
    if (upgrade == UPGRADE_MAGIC_NONE)
        printf(" (无升级)");
    else if (upgrade == UPGRADE_MAGIC_READY)
        printf(" (新固件就绪)");
    else if (upgrade == UPGRADE_MAGIC_COMMITTED)
        printf(" (已搬运待确认)");
    else
        printf(" (异常值!)");
    printf("\r\n");

    uint32_t protect = Flags_ReadProtectFlag();
    printf("  protect_flag    = 0x%08X", (unsigned int)protect);
    if (protect == PROTECT_MAGIC_NONE)
        printf(" (未确认)");
    else if (protect == PROTECT_MAGIC_OK)
        printf(" (已确认)");
    else
        printf(" (异常值!)");
    printf("\r\n");

    Flags_ReadCurrentVer(ver);
    printf("  current_ver     = \"%s\"\r\n", ver);

    Flags_ReadNewVer(ver);
    printf("  new_ver         = \"%s\"\r\n", ver);

    Flags_ReadBackupVer(ver);
    printf("  backup_ver      = \"%s\"\r\n", ver);

    printf("  fw_size         = %lu bytes\r\n", (unsigned long)Flags_ReadFwSize());
    printf("  boot_fail_cnt   = %lu\r\n", (unsigned long)Flags_ReadBootFailCnt());
    printf("  downloaded_size = %lu bytes\r\n", (unsigned long)Flags_ReadDownloadedSize());

    Flags_ReadToken(token);
    printf("  token           = \"%s\"\r\n", token);

    Flags_ReadMd5(md5);
    printf("  file_md5        = \"%s\"\r\n", md5);

    printf("=====================================\r\n\r\n");
}

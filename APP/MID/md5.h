/**
 * @file    md5.h
 * @brief   MD5 消息摘要算法 — 轻量嵌入式实现 (RFC 1321)
 *
 * ===========================================================================
 * 使用场景
 * ===========================================================================
 *   本模块专为 OTA 固件校验设计, 用于验证下载到 W25Q128 的固件是否完整。
 *
 *   典型用法 (增量计算):
 *     MD5_CTX ctx;
 *     uint8_t digest[16];
 *     char hex[33];
 *
 *     MD5_Init(&ctx);
 *     while (has_data) {
 *         MD5_Update(&ctx, chunk_data, chunk_len);  // 逐块喂数据
 *     }
 *     MD5_Final(digest, &ctx);
 *     MD5_HexStr(digest, hex);  // digest -> 32 字符 hex 字符串
 *     printf("MD5: %s\n", hex);
 *
 *   快捷用法 (一次性计算小数据):
 *     uint8_t digest[16];
 *     MD5_Compute(data, len, digest);
 *
 * ===========================================================================
 * 特性
 * ===========================================================================
 *   - 无动态内存分配 (所有上下文在栈上)
 *   - 无全局变量 (可重入)
 *   - 支持增量计算 (适合分块读取 Flash 的场景)
 */

#ifndef __MD5_H
#define __MD5_H

#include <stdint.h>

/** @brief MD5 输出长度: 128 bits = 16 bytes */
#define MD5_HASH_SIZE 16

/*==========================================================================
 * MD5 计算上下文
 *==========================================================================*/

/**
 * @brief MD5 算法状态, 用于增量计算
 *
 * 使用步骤:
 *   1. MD5_Init(&ctx)    — 初始化
 *   2. MD5_Update(...)   — 反复追加数据
 *   3. MD5_Final(...)    — 输出最终 digest
 */
typedef struct
{
    uint32_t total[2];  /**< 已处理字节计数 [low, high]         */
    uint32_t state[4];  /**< 中间状态: A, B, C, D               */
    uint8_t buffer[64]; /**< 缓存未对齐到 64 字节的残留数据      */
} MD5_CTX;

/*==========================================================================
 * API 函数
 *==========================================================================*/

/**
 * @brief  初始化 MD5 上下文
 * @param  ctx 指向栈上或全局的 MD5_CTX
 */
void MD5_Init(MD5_CTX *ctx);

/**
 * @brief  追加数据 (可多次调用, 用于增量计算)
 * @param  ctx  MD5 上下文
 * @param  data 数据指针
 * @param  len  数据长度 (字节)
 */
void MD5_Update(MD5_CTX *ctx, const uint8_t *data, uint32_t len);

/**
 * @brief  输出最终的 16 字节 digest
 * @param  digest 出参, 16 字节缓冲区 (MD5_HASH_SIZE)
 * @param  ctx    MD5 上下文 (调用后不再使用)
 */
void MD5_Final(uint8_t digest[MD5_HASH_SIZE], MD5_CTX *ctx);

/**
 * @brief  一步式计算 MD5 (不适合大文件/Flash 分块读取场景)
 * @param  data   输入数据
 * @param  len    数据长度
 * @param  digest 出参, 16 字节
 */
void MD5_Compute(const uint8_t *data, uint32_t len, uint8_t digest[MD5_HASH_SIZE]);

/**
 * @brief  将 16 字节的 binary digest 转为 32 字符小写 hex 字符串
 * @param  digest  输入, 16 字节
 * @param  hex_out 输出, 33 字节 (32 字符 + '\0')
 *
 * 示例: {0xB6, 0xFB, 0x7F, ...} -> "b6fb7f7c..."
 */
void MD5_HexStr(const uint8_t digest[MD5_HASH_SIZE], char hex_out[33]);

#endif /* __MD5_H */

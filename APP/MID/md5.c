/**
 * @file    md5.c
 * @brief   MD5 消息摘要算法实现 (RFC 1321)
 *
 * ===========================================================================
 * 算法简介
 * ===========================================================================
 *   MD5 (Message Digest 5) 将任意长度输入压缩为 128-bit (16-byte) 摘要。
 *
 *   处理流程:
 *     1. 填充: 在消息末尾追加 0x80 + 零字节 + 64-bit 原始长度
 *     2. 分块: 每 512-bit (64-byte) 一块, 送入压缩函数
 *     3. 压缩: 4 轮 × 16 步, 轮函数分别为 F, G, H, I
 *     4. 输出: A, B, C, D 四个 32-bit 状态字 (little-endian)
 *
 * ===========================================================================
 * 嵌入式优化
 * ===========================================================================
 *   - 无 malloc, 全部操作基于调用者提供的 MD5_CTX
 *   - 使用标准 C99 类型 (uint32_t, uint8_t)
 *   - 编译目标: ARM Cortex-M4 (STM32F407, little-endian)
 */

#include "md5.h"
#include <string.h>

/*==========================================================================
 * 轮函数 (RFC 1321 定义的 4 个非线性函数)
 *==========================================================================*/

/** @brief F(X,Y,Z) = (X & Y) | (~X & Z) — 条件选择函数 */
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))

/** @brief G(X,Y,Z) = (X & Z) | (Y & ~Z) — 类似 F, 输入顺序不同 */
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))

/** @brief H(X,Y,Z) = X ^ Y ^ Z — 奇偶校验 (XOR) */
#define H(x, y, z) ((x) ^ (y) ^ (z))

/** @brief I(X,Y,Z) = Y ^ (X | ~Z) — 更复杂的 XOR 变体 */
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

/*==========================================================================
 * 轮操作宏 (每轮 16 步, 每步做: 加 -> 旋 -> 加)
 *==========================================================================*/

/** @brief 循环左移 (ARM 有 ROR 指令, 编译器会自动优化) */
#define LEFTROTATE(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/**
 * @brief 单步操作模板:
 *        a = b + LEFTROTATE(a + func(b,c,d) + M[k] + T[i], s)
 *        四个宏分别使用 F/G/H/I 轮函数
 */

#define FF(a, b, c, d, x, s, ac)                  \
    do                                            \
    {                                             \
        (a) += F(b, c, d) + (x) + (uint32_t)(ac); \
        (a) = LEFTROTATE(a, s);                   \
        (a) += (b);                               \
    } while (0)

#define GG(a, b, c, d, x, s, ac)                  \
    do                                            \
    {                                             \
        (a) += G(b, c, d) + (x) + (uint32_t)(ac); \
        (a) = LEFTROTATE(a, s);                   \
        (a) += (b);                               \
    } while (0)

#define HH(a, b, c, d, x, s, ac)                  \
    do                                            \
    {                                             \
        (a) += H(b, c, d) + (x) + (uint32_t)(ac); \
        (a) = LEFTROTATE(a, s);                   \
        (a) += (b);                               \
    } while (0)

#define II(a, b, c, d, x, s, ac)                  \
    do                                            \
    {                                             \
        (a) += I(b, c, d) + (x) + (uint32_t)(ac); \
        (a) = LEFTROTATE(a, s);                   \
        (a) += (b);                               \
    } while (0)

/*==========================================================================
 * 初始化
 *==========================================================================*/

/**
 * @brief  初始化 MD5 上下文
 *
 * 初始值 (A, B, C, D) 来自 RFC 1321:
 *   A = 0x67452301
 *   B = 0xEFCDAB89
 *   C = 0x98BADCFE
 *   D = 0x10325476
 */
void MD5_Init(MD5_CTX *ctx)
{
    ctx->total[0] = 0;
    ctx->total[1] = 0;

    ctx->state[0] = 0x67452301; /* A */
    ctx->state[1] = 0xEFCDAB89; /* B */
    ctx->state[2] = 0x98BADCFE; /* C */
    ctx->state[3] = 0x10325476; /* D */
}

/*==========================================================================
 * 核心压缩函数
 *==========================================================================*/

/**
 * @brief  处理一个 512-bit (64-byte) 数据块
 *
 * 将 64 字节数据转为 16 个 32-bit 字 (little-endian),
 * 然后执行 4 轮共 64 步压缩操作,
 * 最后把结果累加到 ctx->state[0..3]。
 *
 * @param ctx  MD5 上下文
 * @param block 64 字节的输入数据块
 */
static void md5_process_block(MD5_CTX *ctx, const uint8_t block[64])
{
    /* ── 保存当前状态 (用于最后的累加) ── */
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];

    /* ── 64 字节 -> 16 个 uint32 (little-endian 字节序) ── */
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
    {
        M[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) | ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
    }

    /* ══════════════════════════════════════════
     * Round 1: F 函数 (16 步)
     * 消息索引 i 依次为 0,1,2,...,15
     * ══════════════════════════════════════════ */
    FF(a, b, c, d, M[0], 7, 0xD76AA478);
    FF(d, a, b, c, M[1], 12, 0xE8C7B756);
    FF(c, d, a, b, M[2], 17, 0x242070DB);
    FF(b, c, d, a, M[3], 22, 0xC1BDCEEE);
    FF(a, b, c, d, M[4], 7, 0xF57C0FAF);
    FF(d, a, b, c, M[5], 12, 0x4787C62A);
    FF(c, d, a, b, M[6], 17, 0xA8304613);
    FF(b, c, d, a, M[7], 22, 0xFD469501);
    FF(a, b, c, d, M[8], 7, 0x698098D8);
    FF(d, a, b, c, M[9], 12, 0x8B44F7AF);
    FF(c, d, a, b, M[10], 17, 0xFFFF5BB1);
    FF(b, c, d, a, M[11], 22, 0x895CD7BE);
    FF(a, b, c, d, M[12], 7, 0x6B901122);
    FF(d, a, b, c, M[13], 12, 0xFD987193);
    FF(c, d, a, b, M[14], 17, 0xA679438E);
    FF(b, c, d, a, M[15], 22, 0x49B40821);

    /* ══════════════════════════════════════════
     * Round 2: G 函数 (16 步)
     * 消息索引 i = (1 + 5*k) mod 16
     * ══════════════════════════════════════════ */
    GG(a, b, c, d, M[1], 5, 0xF61E2562);
    GG(d, a, b, c, M[6], 9, 0xC040B340);
    GG(c, d, a, b, M[11], 14, 0x265E5A51);
    GG(b, c, d, a, M[0], 20, 0xE9B6C7AA);
    GG(a, b, c, d, M[5], 5, 0xD62F105D);
    GG(d, a, b, c, M[10], 9, 0x02441453);
    GG(c, d, a, b, M[15], 14, 0xD8A1E681);
    GG(b, c, d, a, M[4], 20, 0xE7D3FBC8);
    GG(a, b, c, d, M[9], 5, 0x21E1CDE6);
    GG(d, a, b, c, M[14], 9, 0xC33707D6);
    GG(c, d, a, b, M[3], 14, 0xF4D50D87);
    GG(b, c, d, a, M[8], 20, 0x455A14ED);
    GG(a, b, c, d, M[13], 5, 0xA9E3E905);
    GG(d, a, b, c, M[2], 9, 0xFCEFA3F8);
    GG(c, d, a, b, M[7], 14, 0x676F02D9);
    GG(b, c, d, a, M[12], 20, 0x8D2A4C8A);

    /* ══════════════════════════════════════════
     * Round 3: H 函数 (16 步)
     * 消息索引 i = (5 + 3*k) mod 16
     * ══════════════════════════════════════════ */
    HH(a, b, c, d, M[5], 4, 0xFFFA3942);
    HH(d, a, b, c, M[8], 11, 0x8771F681);
    HH(c, d, a, b, M[11], 16, 0x6D9D6122);
    HH(b, c, d, a, M[14], 23, 0xFDE5380C);
    HH(a, b, c, d, M[1], 4, 0xA4BEEA44);
    HH(d, a, b, c, M[4], 11, 0x4BDECFA9);
    HH(c, d, a, b, M[7], 16, 0xF6BB4B60);
    HH(b, c, d, a, M[10], 23, 0xBEBFBC70);
    HH(a, b, c, d, M[13], 4, 0x289B7EC6);
    HH(d, a, b, c, M[0], 11, 0xEAA127FA);
    HH(c, d, a, b, M[3], 16, 0xD4EF3085);
    HH(b, c, d, a, M[6], 23, 0x04881D05);
    HH(a, b, c, d, M[9], 4, 0xD9D4D039);
    HH(d, a, b, c, M[12], 11, 0xE6DB99E5);
    HH(c, d, a, b, M[15], 16, 0x1FA27CF8);
    HH(b, c, d, a, M[2], 23, 0xC4AC5665);

    /* ══════════════════════════════════════════
     * Round 4: I 函数 (16 步)
     * 消息索引 i = (0 + 7*k) mod 16
     * ══════════════════════════════════════════ */
    II(a, b, c, d, M[0], 6, 0xF4292244);
    II(d, a, b, c, M[7], 10, 0x432AFF97);
    II(c, d, a, b, M[14], 15, 0xAB9423A7);
    II(b, c, d, a, M[5], 21, 0xFC93A039);
    II(a, b, c, d, M[12], 6, 0x655B59C3);
    II(d, a, b, c, M[3], 10, 0x8F0CCC92);
    II(c, d, a, b, M[10], 15, 0xFFEFF47D);
    II(b, c, d, a, M[1], 21, 0x85845DD1);
    II(a, b, c, d, M[8], 6, 0x6FA87E4F);
    II(d, a, b, c, M[15], 10, 0xFE2CE6E0);
    II(c, d, a, b, M[6], 15, 0xA3014314);
    II(b, c, d, a, M[13], 21, 0x4E0811A1);
    II(a, b, c, d, M[4], 6, 0xF7537E82);
    II(d, a, b, c, M[11], 10, 0xBD3AF235);
    II(c, d, a, b, M[2], 15, 0x2AD7D2BB);
    II(b, c, d, a, M[9], 21, 0xEB86D391);

    /* ── 累加回 ctx->state (MD 强化) ──
     *     每个 state 字加上本轮压缩前的值,
     *     这是 Davis-Meyer 构造的核心 */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

/*==========================================================================
 * 增量追加数据
 *==========================================================================*/

/**
 * @brief  追加数据到 MD5 计算
 *
 * 处理策略:
 *   1. 如果 ctx 中有未对齐的残留数据, 尝试补满 64 字节后压缩
 *   2. 将剩余数据按 64 字节批量压缩
 *   3. 最后不足 64 字节的部分暂存在 ctx->buffer 中
 *
 * @param ctx  MD5 上下文
 * @param data 输入数据
 * @param len  数据长度 (字节)
 */
void MD5_Update(MD5_CTX *ctx, const uint8_t *data, uint32_t len)
{
    /* ── 当前 buffer 中已缓存的字节数 (0 ~ 63) ── */
    uint32_t fill = ctx->total[0] & 0x3F;

    /* ── 更新总长度计数器 ── */
    ctx->total[0] += len;
    if (ctx->total[0] < len) /* 加法溢出 -> 进位 */
        ctx->total[1]++;

    /* ── 阶段 1: 补全残留数据到 64 字节 ── */
    if (fill > 0)
    {
        uint32_t left = 64 - fill;
        if (len < left)
        {
            /* 数据不足, 全量缓存, 等下次 */
            memcpy(ctx->buffer + fill, data, len);
            return;
        }
        memcpy(ctx->buffer + fill, data, left);
        md5_process_block(ctx, ctx->buffer);
        data += left;
        len -= left;
    }

    /* ── 阶段 2: 批量处理完整的 64 字节块 ── */
    while (len >= 64)
    {
        md5_process_block(ctx, data);
        data += 64;
        len -= 64;
    }

    /* ── 阶段 3: 缓存不足 64 字节的尾部 ── */
    if (len > 0)
        memcpy(ctx->buffer, data, len);
}

/*==========================================================================
 * 最终输出
 *==========================================================================*/

/**
 * @brief  输出最终的 16 字节 MD5 digest
 *
 * 执行步骤:
 *   [1] 填充: 追加 0x80 + 零字节, 使 (总字节数 % 64) == 56
 *   [2] 追加长度: 8 字节, 原始消息 bit 数 (little-endian)
 *   [3] 输出: state[0..3] 按 little-endian 写入 digest[0..15]
 *
 * @param digest 出参, 16 字节
 * @param ctx    MD5 上下文 (调用后状态不确定, 不应再使用)
 */
void MD5_Final(uint8_t digest[MD5_HASH_SIZE], MD5_CTX *ctx)
{
    /* ── [1] 计算填充长度 ──
     *      MD5 要求最后 8 字节存放原始消息长度 (64-bit),
     *      因此填充后数据总长必须是 64 的倍数,
     *      且最后一块剩余 8 字节给长度字段。
     *      -> 填充到 (total % 64) == 56
     *      如果当前剩余 > 56, 需要多填一个整块 (120 - fill)
     */
    uint32_t fill = ctx->total[0] & 0x3F;
    uint32_t pad_len = (fill < 56) ? (56 - fill) : (120 - fill);

    /* 填充数据: 首字节 0x80, 其余全 0 */
    uint8_t padding[64] = {0x80}; /* C99: 首个元素为 0x80, 其余自动 0 */

    /* ── [2] 计算原始消息总 bit 数 (little-endian 64-bit) ── */
    uint64_t bits = (uint64_t)ctx->total[1] * 8 * 0x100000000ULL + (uint64_t)ctx->total[0] * 8;

    /* 追加填充字节 */
    MD5_Update(ctx, padding, pad_len);

    /* 追加 8 字节长度 (little-endian) */
    uint8_t len_buf[8];
    for (int i = 0; i < 8; i++)
        len_buf[i] = (uint8_t)(bits >> (i * 8));
    MD5_Update(ctx, len_buf, 8);

    /* ── [3] 输出 state (little-endian) -> 16 字节 digest ── */
    for (int i = 0; i < 4; i++)
    {
        digest[i * 4 + 0] = (uint8_t)(ctx->state[i]);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

/*==========================================================================
 * 便捷函数
 *==========================================================================*/

/**
 * @brief  一步式 MD5 计算 (不适合大文件)
 *
 * @note   对大数据 (>数MB) 建议使用 MD5_Init/Update/Final 增量计算,
 *         避免占用过大的栈空间。
 */
void MD5_Compute(const uint8_t *data, uint32_t len, uint8_t digest[MD5_HASH_SIZE])
{
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, data, len);
    MD5_Final(digest, &ctx);
}

/**
 * @brief  16 字节 binary digest -> 32 字符小写 hex 字符串
 *
 * 示例:
 *   digest = {0xB6, 0xFB, 0x7F, ...}
 *   -> hex_out = "b6fb7f7c..."
 */
void MD5_HexStr(const uint8_t digest[MD5_HASH_SIZE], char hex_out[33])
{
    static const char hex_chars[] = "0123456789abcdef";

    for (int i = 0; i < MD5_HASH_SIZE; i++)
    {
        hex_out[i * 2] = hex_chars[digest[i] >> 4];
        hex_out[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
    }
    hex_out[32] = '\0';
}

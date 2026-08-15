#include "w25q128.h"

/************************ 底层SPI单字节读写（HAL库SPI1） ************************/
/**
 * @brief  SPI1 全双工收发单字节
 * @param  data: 要发送的字节
 * @retval 接收到的字节
 */
static uint8_t W25Q128_SPI_RW_Byte(uint8_t data)
{
    uint8_t rx_data;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rx_data, 1, HAL_MAX_DELAY);
    return rx_data;
}

/************************ 基础功能函数 ************************/
/**
 * @brief  初始化W25Q128，拉高片选，校验芯片ID
 */
void W25Q128_Init(void)
{
    W25Q128_CS_HIGH();
    // 读取ID校验，可在此增加异常判断
    // uint32_t id = W25Q128_ReadJEDEC_ID();
}

/**
 * @brief  读取JEDEC ID（3字节：厂商ID + 内存类型 + 容量）
 * @retval 24位JEDEC ID
 */
uint32_t W25Q128_ReadJEDEC_ID(void)
{
    uint32_t jedec_id = 0;
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_JedecDeviceID);
    jedec_id |= (uint32_t)W25Q128_SPI_RW_Byte(0xFF) << 16;
    jedec_id |= (uint32_t)W25Q128_SPI_RW_Byte(0xFF) << 8;
    jedec_id |= (uint32_t)W25Q128_SPI_RW_Byte(0xFF);
    W25Q128_CS_HIGH();
    return jedec_id;
}

/**
 * @brief  读状态寄存器1
 * @retval 状态寄存器值，bit0为BUSY忙标志
 */
uint8_t W25Q128_ReadStatusReg(void)
{
    uint8_t status;
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_ReadStatusReg);
    status = W25Q128_SPI_RW_Byte(0xFF);
    W25Q128_CS_HIGH();
    return status;
}

/**
 * @brief  写使能，擦除/写入前必须调用
 */
void W25Q128_WriteEnable(void)
{
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_WriteEnable);
    W25Q128_CS_HIGH();
}

/**
 * @brief  等待Flash操作完成（阻塞等待忙标志清零）
 */
void W25Q128_WaitBusy(void)
{
    while ((W25Q128_ReadStatusReg() & 0x01) == SET)
        ;
}

/************************ 擦除函数 ************************/
/**
 * @brief  4KB扇区擦除
 * @param  addr: 扇区内任意地址（24位）
 */
void W25Q128_SectorErase(uint32_t addr)
{
    W25Q128_WriteEnable();
    W25Q128_WaitBusy();
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_SectorErase);
    W25Q128_SPI_RW_Byte((addr >> 16) & 0xFF);
    W25Q128_SPI_RW_Byte((addr >> 8) & 0xFF);
    W25Q128_SPI_RW_Byte(addr & 0xFF);
    W25Q128_CS_HIGH();
    W25Q128_WaitBusy();
}

/**
 * @brief  32KB块擦除
 * @param  addr: 块内任意地址
 */
void W25Q128_BlockErase_32K(uint32_t addr)
{
    W25Q128_WriteEnable();
    W25Q128_WaitBusy();
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_BlockErase32K);
    W25Q128_SPI_RW_Byte((addr >> 16) & 0xFF);
    W25Q128_SPI_RW_Byte((addr >> 8) & 0xFF);
    W25Q128_SPI_RW_Byte(addr & 0xFF);
    W25Q128_CS_HIGH();
    W25Q128_WaitBusy();
}

/**
 * @brief  64KB块擦除
 * @param  addr: 块内任意地址
 */
void W25Q128_BlockErase_64K(uint32_t addr)
{
    W25Q128_WriteEnable();
    W25Q128_WaitBusy();
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_BlockErase64K);
    W25Q128_SPI_RW_Byte((addr >> 16) & 0xFF);
    W25Q128_SPI_RW_Byte((addr >> 8) & 0xFF);
    W25Q128_SPI_RW_Byte(addr & 0xFF);
    W25Q128_CS_HIGH();
    W25Q128_WaitBusy();
}

/**
 * @brief  全片擦除（耗时较长，约几秒）
 */
void W25Q128_ChipErase(void)
{
    W25Q128_WriteEnable();
    W25Q128_WaitBusy();
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_ChipErase);
    W25Q128_CS_HIGH();
    W25Q128_WaitBusy();
}

/************************ 写入函数 ************************/
/**
 * @brief  单页写入（单次最大256字节，不可跨页）
 * @param  addr: 起始地址
 * @param  buf: 数据缓冲区
 * @param  len: 写入长度，≤256
 */
void W25Q128_PageWrite(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if (len > W25Q128_PAGE_SIZE)
        len = W25Q128_PAGE_SIZE;

    W25Q128_WriteEnable();
    W25Q128_WaitBusy();
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_PageProgram);
    W25Q128_SPI_RW_Byte((addr >> 16) & 0xFF);
    W25Q128_SPI_RW_Byte((addr >> 8) & 0xFF);
    W25Q128_SPI_RW_Byte(addr & 0xFF);

    for (uint16_t i = 0; i < len; i++)
    {
        W25Q128_SPI_RW_Byte(buf[i]);
    }
    W25Q128_CS_HIGH();
    W25Q128_WaitBusy();
}

/**
 * @brief  任意地址任意长度写入（自动处理跨页）
 * @param  addr: 起始地址
 * @param  buf: 数据缓冲区
 * @param  len: 写入总长度
 * @note   写入前必须确保对应区域已擦除
 */
void W25Q128_Write(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint16_t page_left = W25Q128_PAGE_SIZE - (addr % W25Q128_PAGE_SIZE);
    if (len <= page_left)
        page_left = len;

    while (1)
    {
        W25Q128_PageWrite(addr, buf, page_left);
        if (len == page_left)
            break;

        buf += page_left;
        addr += page_left;
        len -= page_left;

        page_left = (len > W25Q128_PAGE_SIZE) ? W25Q128_PAGE_SIZE : len;
    }
}

/************************ 读取函数 ************************/
/**
 * @brief  快速读取任意地址数据（带dummy字节，支持高速SPI）
 * @param  addr: 起始地址
 * @param  buf: 接收缓冲区
 * @param  len: 读取长度
 */
void W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    W25Q128_CS_LOW();
    W25Q128_SPI_RW_Byte(W25X_FastReadData);
    W25Q128_SPI_RW_Byte((addr >> 16) & 0xFF);
    W25Q128_SPI_RW_Byte((addr >> 8) & 0xFF);
    W25Q128_SPI_RW_Byte(addr & 0xFF);
    W25Q128_SPI_RW_Byte(0xFF); // Fast Read 必须的空字节

    for (uint32_t i = 0; i < len; i++)
    {
        buf[i] = W25Q128_SPI_RW_Byte(0xFF);
    }
    W25Q128_CS_HIGH();
}

/**
 * @file    w25q128.h
 * @brief   W25Q128 SPI Flash 驱动 — 指令定义与接口
 *
 * @note    SPI1 驱动, CS 片选由宏控制
 */

#ifndef __W25Q128_H
#define __W25Q128_H

#include "main.h"
#include "spi.h"
#include <stdint.h>

/*=== 引脚配置 ===*/
#define W25Q128_CS_LOW() HAL_GPIO_WritePin(W25Q138_CS_GPIO_Port, W25Q138_CS_Pin, GPIO_PIN_RESET) /**< 片选拉低 */
#define W25Q128_CS_HIGH() HAL_GPIO_WritePin(W25Q138_CS_GPIO_Port, W25Q138_CS_Pin, GPIO_PIN_SET)  /**< 片选拉高 */

/*=== 指令定义 ===*/
#define W25X_WriteEnable 0x06      /**< 写使能 */
#define W25X_WriteDisable 0x04     /**< 写禁止 */
#define W25X_ReadStatusReg 0x05    /**< 读状态寄存器1 */
#define W25X_WriteStatusReg 0x01   /**< 写状态寄存器1 */
#define W25X_ReadData 0x03         /**< 普通读 */
#define W25X_FastReadData 0x0B     /**< 快速读 */
#define W25X_PageProgram 0x02      /**< 页编程 */
#define W25X_SectorErase 0x20      /**< 4KB扇区擦除 */
#define W25X_BlockErase32K 0x52    /**< 32KB块擦除 */
#define W25X_BlockErase64K 0xD8    /**< 64KB块擦除 */
#define W25X_ChipErase 0xC7        /**< 全片擦除 */
#define W25X_ManufactDeviceID 0x90 /**< 厂商设备ID */
#define W25X_JedecDeviceID 0x9F    /**< JEDEC ID */

/*=== 芯片参数 ===*/
#define W25Q128_PAGE_SIZE 256       /**< 单页 256 字节 */
#define W25Q128_SECTOR_SIZE 4096    /**< 单扇区 4KB */
#define W25Q128_BLOCK_32K 32768     /**< 32KB 块 */
#define W25Q128_BLOCK_64K 65536     /**< 64KB 块 */
#define W25Q128_TOTAL_SIZE 16777216 /**< 总容量 16MB */
#define W25Q128_JEDEC_ID 0x522118   /**< 标准 JEDEC ID */

/*=== 对外接口 ===*/
void W25Q128_Init(void);
uint32_t W25Q128_ReadJEDEC_ID(void);
uint8_t W25Q128_ReadStatusReg(void);
void W25Q128_WriteEnable(void);
void W25Q128_WaitBusy(void);
void W25Q128_SectorErase(uint32_t addr);
void W25Q128_BlockErase_32K(uint32_t addr);
void W25Q128_BlockErase_64K(uint32_t addr);
void W25Q128_ChipErase(void);
void W25Q128_PageWrite(uint32_t addr, uint8_t *buf, uint16_t len);
void W25Q128_Write(uint32_t addr, uint8_t *buf, uint32_t len);
void W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len);

#endif /* __W25Q128_H */

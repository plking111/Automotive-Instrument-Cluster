/**
 * @file    myiic.h
 * @brief   软件模拟 I2C 驱动 — 引脚定义与接口
 *
 * @note    SCL=PB8, SDA=PB9, 开漏输出
 */

#ifndef __MYIIC_H
#define __MYIIC_H

#include "main.h"

/* I2C 引脚定义 */
#define IIC_SCL_Pin GPIO_PIN_8
#define IIC_SCL_GPIO_Port GPIOB
#define IIC_SDA_Pin GPIO_PIN_9
#define IIC_SDA_GPIO_Port GPIOB

/* 引脚操作宏 */
#define IIC_SCL_SET() HAL_GPIO_WritePin(IIC_SCL_GPIO_Port, IIC_SCL_Pin, GPIO_PIN_SET)
#define IIC_SCL_CLR() HAL_GPIO_WritePin(IIC_SCL_GPIO_Port, IIC_SCL_Pin, GPIO_PIN_RESET)
#define IIC_SDA_SET() HAL_GPIO_WritePin(IIC_SDA_GPIO_Port, IIC_SDA_Pin, GPIO_PIN_SET)
#define IIC_SDA_CLR() HAL_GPIO_WritePin(IIC_SDA_GPIO_Port, IIC_SDA_Pin, GPIO_PIN_RESET)
#define IIC_SDA_READ() HAL_GPIO_ReadPin(IIC_SDA_GPIO_Port, IIC_SDA_Pin)

/* SDA 方向切换 (输入/输出) */
#define SDA_IN()                                  \
    do                                            \
    {                                             \
        uint32_t temp = IIC_SDA_GPIO_Port->MODER; \
        temp &= ~(3 << (9 * 2));                  \
        temp |= (0 << (9 * 2));                   \
        IIC_SDA_GPIO_Port->MODER = temp;          \
    } while (0)

#define SDA_OUT()                                 \
    do                                            \
    {                                             \
        uint32_t temp = IIC_SDA_GPIO_Port->MODER; \
        temp &= ~(3 << (9 * 2));                  \
        temp |= (1 << (9 * 2));                   \
        IIC_SDA_GPIO_Port->MODER = temp;          \
    } while (0)

/* 基础读写接口 */
void IIC_Init(void);
void IIC_Start(void);
void IIC_Stop(void);
void IIC_Send_Byte(uint8_t txd);
uint8_t IIC_Read_Byte(uint8_t ack);
uint8_t IIC_Wait_Ack(void);
void IIC_Ack(void);
void IIC_NAck(void);

/* 单字节读写 */
void IIC_Write_One_Byte(uint8_t daddr, uint8_t addr, uint8_t data);
uint8_t IIC_Read_One_Byte(uint8_t daddr, uint8_t addr);

#endif /* __MYIIC_H */

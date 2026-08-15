/**
 * @file    myiic.c
 * @brief   软件模拟 I2C 驱动 — 实现
 *
 * @note    SCL=PB8, SDA=PB9, 时序靠 delay_us 延时
 */

#include "myiic.h"
#include "delay.h"

/**
 * @brief  初始化 I2C — SCL/SDA 置高
 */
void IIC_Init(void)
{
    //    GPIO_InitTypeDef GPIO_InitStruct = {0};

    //    __HAL_RCC_GPIOB_CLK_ENABLE();   /* 使能 GPIOB 时钟 */

    //    GPIO_InitStruct.Pin = IIC_SCL_Pin | IIC_SDA_Pin;   /* 配置 SCL/SDA 引脚 */
    //    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    //    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    //    GPIO_InitStruct.Pull = GPIO_PULLUP;
    //    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    IIC_SCL_SET(); /* 总线空闲, 拉高 */
    IIC_SDA_SET();
}

/**
 * @brief  产生起始信号 — SCL 高时 SDA 拉低
 */
void IIC_Start(void)
{
    SDA_OUT();
    IIC_SDA_SET();
    IIC_SCL_SET();
    delay_us(4);
    IIC_SDA_CLR(); /* START: SCL 高时 SDA 从高变低 */
    delay_us(4);
    IIC_SCL_CLR();
}

/**
 * @brief  产生停止信号 — SCL 高时 SDA 拉高
 */
void IIC_Stop(void)
{
    SDA_OUT();
    IIC_SCL_CLR();
    IIC_SDA_CLR();
    delay_us(4);
    IIC_SCL_SET();
    IIC_SDA_SET(); /* STOP: SCL 高时 SDA 从低变高 */
    delay_us(4);
}

/**
 * @brief  等待从机应答
 * @return 0=收到 ACK, 1=超时无应答
 */
uint8_t IIC_Wait_Ack(void)
{
    uint8_t ucErrTime = 0;

    SDA_IN();
    IIC_SDA_SET();
    delay_us(1);
    IIC_SCL_SET();
    delay_us(1);

    while (IIC_SDA_READ())
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            IIC_Stop();
            return 1;
        }
    }
    IIC_SCL_CLR();
    return 0;
}

/**
 * @brief  发送 ACK 应答
 */
void IIC_Ack(void)
{
    IIC_SCL_CLR();
    SDA_OUT();
    IIC_SDA_CLR();
    delay_us(2);
    IIC_SCL_SET();
    delay_us(2);
    IIC_SCL_CLR();
}

/**
 * @brief  发送 NACK 非应答
 */
void IIC_NAck(void)
{
    IIC_SCL_CLR();
    SDA_OUT();
    IIC_SDA_SET();
    delay_us(2);
    IIC_SCL_SET();
    delay_us(2);
    IIC_SCL_CLR();
}

/**
 * @brief  发送一个字节 (高位在前)
 * @param  txd 待发送字节
 */
void IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;
    SDA_OUT();
    IIC_SCL_CLR();

    for (t = 0; t < 8; t++)
    {
        if ((txd & 0x80) >> 7)
            IIC_SDA_SET();
        else
            IIC_SDA_CLR();
        txd <<= 1;
        delay_us(2);
        IIC_SCL_SET();
        delay_us(2);
        IIC_SCL_CLR();
        delay_us(2);
    }
}

/**
 * @brief  读取一个字节 (高位在前)
 * @param  ack 1=读后发 ACK, 0=读后发 NACK
 * @return 读到的字节
 */
uint8_t IIC_Read_Byte(uint8_t ack)
{
    uint8_t i, receive = 0;
    SDA_IN();
    for (i = 0; i < 8; i++)
    {
        IIC_SCL_CLR();
        delay_us(2);
        IIC_SCL_SET();
        receive <<= 1;
        if (IIC_SDA_READ())
            receive++;
        delay_us(1);
    }
    if (!ack)
        IIC_NAck();
    else
        IIC_Ack();
    return receive;
}

/**
 * @brief  写一个字节到指定设备寄存器
 * @param  daddr 设备地址
 * @param  addr  寄存器地址
 * @param  data  写入数据
 */
void IIC_Write_One_Byte(uint8_t daddr, uint8_t addr, uint8_t data)
{
    IIC_Start();
    IIC_Send_Byte(daddr);
    IIC_Wait_Ack();
    IIC_Send_Byte(addr);
    IIC_Wait_Ack();
    IIC_Send_Byte(data);
    IIC_Wait_Ack();
    IIC_Stop();
}

/**
 * @brief  从指定设备寄存器读一个字节
 * @param  daddr 设备地址
 * @param  addr  寄存器地址
 * @return 读到的字节
 */
uint8_t IIC_Read_One_Byte(uint8_t daddr, uint8_t addr)
{
    uint8_t temp = 0;

    IIC_Start();
    IIC_Send_Byte(daddr);
    IIC_Wait_Ack();
    IIC_Send_Byte(addr);
    IIC_Wait_Ack();
    IIC_Start();
    IIC_Send_Byte(daddr | 0x01);
    IIC_Wait_Ack();
    temp = IIC_Read_Byte(0);
    IIC_Stop();

    return temp;
}

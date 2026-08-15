/**
 * @file    24cxx.c
 * @brief   AT24CXX EEPROM 驱动
 * @note    IIC 接口，EE_TYPE 决定器件型号
 */

#include "24cxx.h"

/**
 * @brief  初始化 AT24CXX
 */
void AT24CXX_Init(void)
{
    IIC_Init();
}

/**
 * @brief  从指定地址读取一个字节
 * @param  ReadAddr 读取地址
 * @return 读到的数据
 */
uint8_t AT24CXX_ReadOneByte(uint16_t ReadAddr)
{
    uint8_t temp = 0;
    IIC_Start();

    if (EE_TYPE > AT24C16)
    {
        IIC_Send_Byte(AT24CXX_ADDR); /* 发送器件地址 */
        IIC_Wait_Ack();
        IIC_Send_Byte(ReadAddr >> 8); /* 发送高地址 */
        IIC_Wait_Ack();
    }
    else
    {
        IIC_Send_Byte(AT24CXX_ADDR + ((ReadAddr / 256) << 1)); /* 发送器件地址 */
    }

    IIC_Wait_Ack();
    IIC_Send_Byte(ReadAddr % 256); /* 发送低地址 */
    IIC_Wait_Ack();
    IIC_Start();
    IIC_Send_Byte(AT24CXX_ADDR | 0x01); /* 进入读模式 */
    IIC_Wait_Ack();
    temp = IIC_Read_Byte(0);
    IIC_Stop();

    return temp;
}

/**
 * @brief  向指定地址写入一个字节
 * @param  WriteAddr 写入地址
 * @param  DataToWrite 写入数据
 */
void AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite)
{
    IIC_Start();

    if (EE_TYPE > AT24C16)
    {
        IIC_Send_Byte(AT24CXX_ADDR); /* 发送器件地址 */
        IIC_Wait_Ack();
        IIC_Send_Byte(WriteAddr >> 8); /* 发送高地址 */
        IIC_Wait_Ack();
    }
    else
    {
        IIC_Send_Byte(AT24CXX_ADDR + ((WriteAddr / 256) << 1)); /* 发送器件地址 */
    }

    IIC_Wait_Ack();
    IIC_Send_Byte(WriteAddr % 256); /* 发送低地址 */
    IIC_Wait_Ack();
    IIC_Send_Byte(DataToWrite); /* 写入数据 */
    IIC_Wait_Ack();
    IIC_Stop();

    HAL_Delay(10); /* 等待写入完成 */
}

/**
 * @brief  向指定地址写入指定长度数据
 * @param  WriteAddr 写入起始地址
 * @param  DataToWrite 写入数据
 * @param  Len 写入长度(2或4)
 */
void AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len)
{
    for (uint8_t t = 0; t < Len; t++)
    {
        AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xFF);
    }
}

/**
 * @brief  从指定地址读取指定长度数据
 * @param  ReadAddr 读取起始地址
 * @param  Len 读取长度(2或4)
 * @return 读到的数据
 */
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len)
{
    uint32_t temp = 0;
    for (uint8_t t = 0; t < Len; t++)
    {
        temp <<= 8;
        temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1);
    }
    return temp;
}

/**
 * @brief  检测 AT24CXX 是否正常
 * @return 0=正常 1=异常
 */
uint8_t AT24CXX_Check(void)
{
    uint8_t temp;
    temp = AT24CXX_ReadOneByte(255); /* 读固定地址检测器件 */
    if (temp == 0x55)
        return 0;
    else /* 首次读非 0x55 */
    {
        AT24CXX_WriteOneByte(255, 0x55);
        temp = AT24CXX_ReadOneByte(255);
        if (temp == 0x55)
            return 0;
    }
    return 1;
}

/**
 * @brief  从指定地址开始读取指定个数数据
 * @param  ReadAddr 读取起始地址
 * @param  pBuffer 数据缓冲区
 * @param  NumToRead 读取个数
 */
void AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
    while (NumToRead)
    {
        *pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);
        NumToRead--;
    }
}

/**
 * @brief  从指定地址开始写入指定个数数据
 * @param  WriteAddr 写入起始地址
 * @param  pBuffer 数据缓冲区
 * @param  NumToWrite 写入个数
 */
void AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
    while (NumToWrite--)
    {
        AT24CXX_WriteOneByte(WriteAddr, *pBuffer);
        WriteAddr++;
        pBuffer++;
    }
}

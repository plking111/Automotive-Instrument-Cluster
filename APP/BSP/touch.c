/**
 * @file    touch.c
 * @brief   触摸屏 (XPT2046) 驱动
 * @note    软件模拟 SPI 时序，校准数据存于 AT24CXX
 */

#include "touch.h"
#include "ili9341.h"
#include "24cxx.h"
#include <stdlib.h>
#include <math.h>
#include "delay.h"
#include <stdio.h>
#include "iwdg.h"

TouchTypeDef tp_dev = {
    TP_Init,
    TP_Scan,
    TP_Adjust,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

/* 默认触摸类型: 0 */
static uint8_t CMD_RDX = 0XD0;
static uint8_t CMD_RDY = 0X90;

/**
 * @brief  向触摸屏写入 1 字节数据
 * @param  num 要写入的字节
 */
void TP_Write_Byte(uint8_t num)
{
    uint8_t count;
    for (count = 0; count < 8; count++)
    {
        if (num & 0x80)
            TDIN(1);
        else
            TDIN(0);
        num <<= 1;
        TCLK(0);
        delay_us(1);
        TCLK(1);
    }
}

/**
 * @brief  读取触摸屏 AD 转换值
 * @param  cmd 读取命令
 * @return AD 转换结果
 */
uint16_t TP_Read_AD(uint8_t cmd)
{
    uint8_t count = 0;
    uint16_t Num = 0;
    TCLK(0);
    TDIN(0);
    TCS(0);
    TP_Write_Byte(cmd);
    delay_us(6);
    TCLK(0);
    delay_us(1);
    TCLK(1);
    delay_us(1);
    TCLK(0);
    for (count = 0; count < 16; count++)
    {
        Num <<= 1;
        TCLK(0);
        delay_us(1);
        TCLK(1);
        if (DOUT_READ())
            Num++;
    }
    Num >>= 4;
    TCS(1);
    return (Num);
}

#define READ_TIMES 5
#define LOST_VAL 1

/**
 * @brief  读取触摸坐标（多次采样取均值）
 * @param  xy 坐标通道 (CMD_RDX/CMD_RDY)
 * @return 坐标平均值
 */
uint16_t TP_Read_XOY(uint8_t xy)
{
    uint16_t i, j;
    uint16_t buf[READ_TIMES];
    uint16_t sum = 0;
    uint16_t temp;

    for (i = 0; i < READ_TIMES; i++)
        buf[i] = TP_Read_AD(xy);
    for (i = 0; i < READ_TIMES - 1; i++)
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (buf[i] > buf[j])
            {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }
    sum = 0;
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++)
        sum += buf[i];
    temp = sum / (READ_TIMES - 2 * LOST_VAL);
    return temp;
}

/**
 * @brief  读取 X/Y 坐标
 * @param  x X 坐标指针
 * @param  y Y 坐标指针
 * @return 1=读取成功
 */
uint8_t TP_Read_XY(uint16_t *x, uint16_t *y)
{
    *x = TP_Read_XOY(CMD_RDX);
    *y = TP_Read_XOY(CMD_RDY);
    return 1;
}

#define ERR_RANGE 50

/**
 * @brief  读取 X/Y 坐标（带滤波，两次采样需一致）
 * @param  x X 坐标指针
 * @param  y Y 坐标指针
 * @return 0=无效 1=有效
 */
uint8_t TP_Read_XY2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;
    uint16_t x2, y2;
    uint8_t flag;

    flag = TP_Read_XY(&x1, &y1);
    if (flag == 0)
        return 0;
    flag = TP_Read_XY(&x2, &y2);
    if (flag == 0)
        return 0;

    if (((x2 <= x1 && x1 < x2 + ERR_RANGE) || (x1 <= x2 && x2 < x1 + ERR_RANGE)) &&
        ((y2 <= y1 && y1 < y2 + ERR_RANGE) || (y1 <= y2 && y2 < y1 + ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }
    else
        return 0;
}

/**
 * @brief  扫描触摸屏状态
 * @param  tp 1=只读原始值 0=带校准换算
 * @return 按下标志
 */
uint8_t TP_Scan(uint8_t tp)
{
    if (!PEN_READ())
    {
        if (tp)
        {
            TP_Read_XY2(&tp_dev.x, &tp_dev.y);
        }
        else if (TP_Read_XY2(&tp_dev.x, &tp_dev.y))
        {
            tp_dev.x = tp_dev.xfac * tp_dev.x + tp_dev.xoff;
            tp_dev.y = tp_dev.yfac * tp_dev.y + tp_dev.yoff;
            tp_dev.y = lcddev.height - tp_dev.y;
        }
        if ((tp_dev.sta & TP_PRESS_DOWN) == 0)
        {
            tp_dev.sta = TP_PRESS_DOWN | TP_PRESS_LIFT;
            tp_dev.x0 = tp_dev.x;
            tp_dev.y0 = tp_dev.y;
        }
    }
    else
    {
        if (tp_dev.sta & TP_PRESS_DOWN)
        {
            tp_dev.sta &= ~(1 << 7);
        }
        else
        {
            tp_dev.x0 = 0;
            tp_dev.y0 = 0;
            tp_dev.x = 0xffff;
            tp_dev.y = 0xffff;
        }
    }
    return tp_dev.sta & TP_PRESS_DOWN;
}

#define SAVE_ADDR_BASE 40

/**
 * @brief  保存校准参数到 EEPROM
 */
void TP_Save_Adjdata(void)
{
    int32_t temp;

    temp = tp_dev.xfac * 100000000;
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE, temp, 4);
    temp = tp_dev.yfac * 100000000;
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 4, temp, 4);
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 8, tp_dev.xoff, 2);
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 10, tp_dev.yoff, 2);
    AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 12, tp_dev.touchtype);
    temp = 0x0A;
    AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 13, temp);
}

/**
 * @brief  从 EEPROM 读取校准参数
 * @return 1=读取成功 0=无有效数据
 */
uint8_t TP_Get_Adjdata(void)
{
    int32_t tempfac;
    tempfac = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 13);
    if (tempfac == 0x0A)
    {
        tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE, 4);
        tp_dev.xfac = (float)tempfac / 100000000;
        tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 4, 4);
        tp_dev.yfac = (float)tempfac / 100000000;
        tp_dev.xoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 8, 2);
        tp_dev.yoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 10, 2);
        tp_dev.touchtype = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 12);
        if (tp_dev.touchtype)
        {
            CMD_RDX = 0X90;
            CMD_RDY = 0XD0;
        }
        else
        {
            CMD_RDX = 0XD0;
            CMD_RDY = 0X90;
        }
        return 1;
    }
    return 0;
}

/**
 * @brief  绘制十字触摸点
 * @param  x X 坐标
 * @param  y Y 坐标
 * @param  color 颜色
 */
void TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color)
{
    LCD_DrawLine(x - 12, y, x + 13, y, color); /* 水平线 */
    LCD_DrawLine(x, y - 12, x, y + 13, color); /* 垂直线 */
    LCD_DrawPoint(x + 1, y + 1, color);
    LCD_DrawPoint(x - 1, y + 1, color);
    LCD_DrawPoint(x + 1, y - 1, color);
    LCD_DrawPoint(x - 1, y - 1, color);
}

/**
 * @brief  绘制大触摸点（2x2）
 * @param  x X 坐标
 * @param  y Y 坐标
 * @param  color 颜色
 */
void TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color)
{
    LCD_DrawPoint(x, y, color); /* 画 2x2 点 */
    LCD_DrawPoint(x + 1, y, color);
    LCD_DrawPoint(x, y + 1, color);
    LCD_DrawPoint(x + 1, y + 1, color);
}

/**
 * @brief  触摸屏四点校准
 */
void TP_Adjust(void)
{
    uint16_t pos_temp[4][2];
    uint8_t cnt = 0;
    uint16_t d1, d2;
    uint32_t tem1, tem2;
    double fac;
    uint16_t outtime = 0;

    //    printf("开始校准\r\n");

    TP_Drow_Touch_Point(20, 20, RED);
    //	printf("Please touch point1.\r\n");
    while (1)
    {
        tp_dev.scan(1);
        if ((tp_dev.sta & 0xc0) == TP_PRESS_LIFT)
        {
            outtime = 0;
            tp_dev.sta &= ~(1 << 6);

            pos_temp[cnt][0] = tp_dev.x;
            pos_temp[cnt][1] = tp_dev.y;
            cnt++;

            switch (cnt)
            {
            case 0:

                break;
            case 1:
                TP_Drow_Touch_Point(20, 20, WHITE);              /* 清除点1 */
                TP_Drow_Touch_Point(lcddev.width - 20, 20, RED); /* 画点2 */
                                                                 //				printf("Please touch point2.\r\n");
                break;
            case 2:
                TP_Drow_Touch_Point(lcddev.width - 20, 20, WHITE); /* 清除点2 */
                TP_Drow_Touch_Point(20, lcddev.height - 20, RED);  /* 画点3 */
                                                                   //				printf("Please touch point3.\r\n");
                break;
            case 3:
                TP_Drow_Touch_Point(20, lcddev.height - 20, WHITE);              /* 清除点3 */
                TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, RED); /* 画点4 */
                                                                                 //				printf("Please touch point4.\r\n");
                break;
            case 4:
                /* 计算两点间距离 */
                tem1 = abs(pos_temp[0][0] - pos_temp[1][0]);
                tem2 = abs(pos_temp[0][1] - pos_temp[1][1]);
                tem1 *= tem1;
                tem2 *= tem2;
                d1 = sqrt(tem1 + tem2);

                tem1 = abs(pos_temp[2][0] - pos_temp[3][0]);
                tem2 = abs(pos_temp[2][1] - pos_temp[3][1]);
                tem1 *= tem1;
                tem2 *= tem2;
                d2 = sqrt(tem1 + tem2);
                fac = (float)d1 / d2;

                if (fac < 0.95 || fac > 1.05 || d1 == 0 || d2 == 0)
                {
                    cnt = 0;
                    TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                    TP_Drow_Touch_Point(20, 20, RED); /* 重新画点1 */
                                                      //          printf("Calibration failed! Please try again!\r\n");
                                                      //					printf("Please touch point1.\r\n");
                    continue;
                }

                /* 计算校准系数 */
                tp_dev.xfac = (float)(lcddev.width - 40) / (pos_temp[1][0] - pos_temp[0][0]);
                tp_dev.xoff = (lcddev.width - tp_dev.xfac * (pos_temp[1][0] + pos_temp[0][0])) / 2;

                tp_dev.yfac = (float)(lcddev.height - 40) / (pos_temp[2][1] - pos_temp[0][1]);
                tp_dev.yoff = (lcddev.height - tp_dev.yfac * (pos_temp[2][1] + pos_temp[0][1])) / 2;

                if (abs(tp_dev.xfac) > 2 || abs(tp_dev.yfac) > 2)
                {
                    cnt = 0;
                    TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                    TP_Drow_Touch_Point(20, 20, RED); /* 重新画点1 */
                                                      //					printf("Calibration failed! Please try again!\r\n");
                                                      //					printf("Please touch point1.\r\n");
                    tp_dev.touchtype = !tp_dev.touchtype;
                    if (tp_dev.touchtype)
                    {
                        CMD_RDX = 0X90;
                        CMD_RDY = 0XD0;
                    }
                    else
                    {
                        CMD_RDX = 0XD0;
                        CMD_RDY = 0X90;
                    }
                    continue;
                }
                TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                //                    printf("Touch Screen Adjust OK!\r\n");
                HAL_Delay(1000);
                TP_Save_Adjdata();
                return;
            }
        }
        HAL_Delay(10);
        HAL_IWDG_Refresh(&hiwdg); /* 校准期间喂狗, 防止超时复位 */
        outtime++;
        if (outtime > 1000)
        {
            TP_Get_Adjdata();
            break;
        }
    }
}

/**
 * @brief  触摸屏初始化
 * @return 0=成功 1=需校准
 */
uint8_t TP_Init(void)
{
#if USE_TP_ADJUST
    TP_Read_XY(&tp_dev.x, &tp_dev.y);
    AT24CXX_Init();

    /* 检测屏幕方向是否变化, 若变化则自动清除旧校准数据 */
    {
        uint8_t stored_orient = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 14);
        if (stored_orient != USE_HORIZONTAL)
        {
            AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 13, 0); /* 清除 magic byte */
            AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 14, USE_HORIZONTAL);
        }
    }

    if (TP_Get_Adjdata())
    {
        return 0;
    }
    else
    {
        TP_Adjust();
        TP_Save_Adjdata();
        AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 14, USE_HORIZONTAL);
    }
#else
    tp_dev.xfac = -0.067454;
    tp_dev.yfac = -0.090615;
    tp_dev.xoff = 258;
    tp_dev.yoff = 357;
    tp_dev.touchtype = 0;
#endif
    return 1;
}

void Touch_Test(void)
{
    TP_Init();
    uint16_t x, y;
    uint8_t touchStatus;

    while (1)
    {
        /* 扫描触摸屏状态 */
        touchStatus = TP_Scan(0); /* 0 表示带校准换算 */

        if (touchStatus & TP_PRESS_DOWN) /* 有按键按下 */
        {
            x = tp_dev.x;
            y = tp_dev.y;
            //            printf("Touch detected at: x=%d, y=%d\r\n", x, y);

            /* 画点显示触摸位置 */
            TP_Draw_Big_Point(x, y, BLACK);
        }
        else
        {
        }
    }
}

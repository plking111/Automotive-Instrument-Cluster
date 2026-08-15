/**
 * @file    ili9341.c
 * @brief   ILI9341 LCD 显示屏驱动
 * @details 基于 SPI 通信的 ILI9341 TFT LCD 驱动文件，包含初始化、绘图、显示控制等功能。
 *          支持 DMA 加速传输和多种显示方向。
 * @author  CSDN_LVGL
 * @date    2024
 */
#include "ili9341.h"
#include "spi.h"
#include "dma.h"
/** @brief LCD 设备全局结构体，存储屏幕参数（宽度、高度、命令地址等） */
_lcd_dev lcddev;
/**
 * @brief  向 LCD 发送命令字节
 * @param  cmd : 要发送的命令字节
 * @note   发送命令时，DC（数据/命令选择）引脚拉低，CS（片选）引脚拉低
 * @retval None
 */
void LCD_SendCmd(uint8_t cmd)
{
    LCD_DC_Clr(); // DC=0，选择命令模式
    LCD_CS_Clr(); // CS=0，使能片选
    HAL_SPI_Transmit(&hspi3, &cmd, 1, HAL_MAX_DELAY);
    LCD_CS_Set(); // CS=1，释放片选
}
/**
 * @brief  向 LCD 发送数据字节
 * @param  data : 要发送的数据字节
 * @note   发送数据时，DC（数据/命令选择）引脚拉高，CS（片选）引脚拉低
 * @retval None
 */
void LCD_SendData(uint8_t data)
{
    LCD_DC_Set(); // DC=1，选择数据模式
    LCD_CS_Clr(); // CS=0，使能片选
    HAL_SPI_Transmit(&hspi3, &data, 1, HAL_MAX_DELAY);
    LCD_CS_Set(); // CS=1，释放片选
}
/**
 * @brief  向 LCD 寄存器写入数据
 * @param  reg  : 寄存器地址（命令）
 * @param  data : 要写入的 16 位数据
 * @retval None
 */
void LCD_WriteReg(uint8_t reg, uint16_t data)
{
    LCD_SendCmd(reg);
    LCD_SendData(data);
}
/**
 * @brief  准备写入 LCD GRAM（图形显示内存）
 * @note   发送写内存命令后，后续数据将直接写入当前窗口区域
 * @param  None
 * @retval None
 */
void LCD_WriteRAM_Prepare(void)
{
    LCD_SendCmd(lcddev.wramcmd);
}
/**
 * @brief  向 LCD 写入 16 位颜色数据
 * @param  data : 要发送的 16 位颜色值（RGB565 格式）
 * @note   先发送高 8 位，再发送低 8 位
 * @retval None
 */
void LCD_WriteData_16Bit(uint16_t data)
{
    LCD_DC_Set(); // DC=1，选择数据模式
    LCD_CS_Clr(); // CS=0，使能片选
    // 先发送高 8 位，再发送低 8 位
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&data + 1, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&data, 1, HAL_MAX_DELAY);
    LCD_CS_Set(); // CS=1，释放片选
}
/**
 * @brief  通过 SPI 批量写入 16 位颜色数组
 * @param  data : 16 位颜色数据的数组指针
 * @param  len  : 数组长度（像素个数）
 * @note   逐个像素拆分为高/低 8 位后通过 SPI 发送
 * @retval None
 */
void lcd_write_16bit_data_array(const uint16_t *data, uint32_t len)
{
    uint16_t i;
    LCD_CS_Clr();
    LCD_DC_Set();
    for (i = 0; i < len; i++)
    {
        uint8_t high_byte = data[i] >> 8;  // 获取高 8 位
        uint8_t low_byte = data[i] & 0xFF; // 获取低 8 位
        HAL_SPI_Transmit(&hspi3, &high_byte, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&hspi3, &low_byte, 1, HAL_MAX_DELAY);
    }
    LCD_CS_Set();
}
/**
 * @brief  LCD 硬件复位
 * @note   通过拉低 RST 引脚 50ms 后拉高来实现硬件复位
 * @param  None
 * @retval None
 */
void LCD_RESET(void)
{
    LCD_RST_Clr();
    delay_ms(50);
    LCD_RST_Set();
    delay_ms(50);
}
/**
 * @brief  打开 LCD 背光
 * @param  None
 * @retval None
 */
void LCD_BackLight_On(void)
{
    HAL_GPIO_WritePin(LCD_LED_GPIO_Port, LCD_LED_Pin, GPIO_PIN_SET);
}
/**
 * @brief  关闭 LCD 背光
 * @param  None
 * @retval None
 */
void LCD_BackLight_Off(void)
{
    HAL_GPIO_WritePin(LCD_LED_GPIO_Port, LCD_LED_Pin, GPIO_PIN_RESET);
}
/**
 * @brief   设置 LCD 绘图窗口区域
 * @details 通过设置列地址和行地址来限定后续绘图操作的范围，所有绘图操作将只在此窗口内生效
 * @param   startX : 窗口起始 X 坐标（列地址）
 * @param   startY : 窗口起始 Y 坐标（行地址）
 * @param   endX   : 窗口结束 X 坐标（列地址）
 * @param   endY   : 窗口结束 Y 坐标（行地址）
 * @note    设置完窗口后自动发送 RAMWR (0x2C) 内存写入命令，准备接收像素数据
 * @retval  None
 */
void LCD_SetWindow(uint16_t startX, uint16_t startY, uint16_t endX, uint16_t endY)
{
    // 设置列地址范围（X 方向）
    LCD_SendCmd(0x2A);           // Column Address Set（列地址设置）
    LCD_SendData(startX >> 8);   // 起始列地址高 8 位
    LCD_SendData(startX & 0xFF); // 起始列地址低 8 位
    LCD_SendData(endX >> 8);     // 结束列地址高 8 位
    LCD_SendData(endX & 0xFF);   // 结束列地址低 8 位
    // 设置行地址范围（Y 方向）
    LCD_SendCmd(0x2B);           // Row Address Set（行地址设置）
    LCD_SendData(startY >> 8);   // 起始行地址高 8 位
    LCD_SendData(startY & 0xFF); // 起始行地址低 8 位
    LCD_SendData(endY >> 8);     // 结束行地址高 8 位
    LCD_SendData(endY & 0xFF);   // 结束行地址低 8 位
    // 启动内存写入
    LCD_SendCmd(0x2C); // Memory Write（内存写入）
}
/**
 * @brief  设置 LCD 绘图光标位置
 * @param  x : X 坐标
 * @param  y : Y 坐标
 * @note   本质上是设置一个 1x1 像素的窗口，用于单点绘制
 * @retval None
 */
void LCD_SetCursor(uint16_t x, uint16_t y)
{
    LCD_SetWindow(x, y, x, y);
}
/**
 * @brief  全屏填充指定颜色（清屏）
 * @param  color : 16 位 RGB565 格式的填充颜色
 * @note   逐像素通过 SPI 发送颜色数据，速度较慢，建议使用 DMA 方式
 * @retval None
 */
void LCD_Clear(uint16_t color)
{
    uint32_t total_pixels = lcddev.width * lcddev.height;
    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;
    // 设置全屏窗口
    LCD_SetWindow(0, 0, lcddev.width, lcddev.height);
    // 进入数据发送模式
    LCD_DC_Set(); // DC=1，数据模式
    LCD_CS_Clr(); // CS=0，使能片选
    for (uint32_t i = 0; i < total_pixels; i++)
    {
        HAL_SPI_Transmit(&hspi3, &color_high, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&hspi3, &color_low, 1, HAL_MAX_DELAY);
    }
    LCD_CS_Set(); // CS=1，释放片选
}
/**
 * @brief   设置 LCD 显示方向
 * @param   direction : 显示方向（旋转角度）
 *          @arg 0 : 0°   （竖屏，默认方向）
 *          @arg 1 : 90°  （顺时针旋转 90°）
 *          @arg 2 : 180° （顺时针旋转 180°）
 *          @arg 3 : 270° （顺时针旋转 270°）
 * @note    通过配置 MADCTL 寄存器（0x36）的 MY/MX/MV 位来控制扫描方向，
 *          同时交换 width 和 height 以匹配旋转后的屏幕尺寸
 * @retval None
 */
void LCD_SetDirection(uint8_t direction)
{
    // 设置 ILI9341 标准控制命令地址
    lcddev.setxcmd = 0x2A; // 列地址设置命令
    lcddev.setycmd = 0x2B; // 行地址设置命令
    lcddev.wramcmd = 0x2C; // 写 GRAM 命令
    switch (direction)
    {
    case 0: // 0° 方向（竖屏）
        lcddev.width = LCD_WIDTH;
        lcddev.height = LCD_HEIGHT;
        // BGR=1, MY=0, MX=0, MV=0 : 不翻转，从左到右，从上到下
        LCD_SendCmd(0x36);
        LCD_SendData((1 << 3) | (0 << 6) | (0 << 7));
        break;
    case 1: // 90° 方向（横屏）
        lcddev.width = LCD_HEIGHT;
        lcddev.height = LCD_WIDTH;
        // BGR=1, MY=0, MX=1, MV=1 : X 轴镜像，行列交换
        LCD_SendCmd(0x36);
        LCD_SendData((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
        break;
    case 2: // 180° 方向（竖屏倒转）
        lcddev.width = LCD_WIDTH;
        lcddev.height = LCD_HEIGHT;
        // BGR=1, MY=1, MX=1, MV=0 : X 轴和 Y 轴均镜像翻转
        LCD_SendCmd(0x36);
        LCD_SendData((1 << 3) | (1 << 6) | (1 << 7));
        break;
    case 3: // 270° 方向（横屏倒转）
        lcddev.width = LCD_HEIGHT;
        lcddev.height = LCD_WIDTH;
        // BGR=1, MY=1, MX=0, MV=1 : Y 轴镜像，行列交换
        LCD_SendCmd(0x36);
        LCD_SendData((1 << 3) | (1 << 7) | (1 << 5));
        break;
    default:
        break;
    }
}
/**
 * @brief  LCD 初始化
 * @details 执行 ILI9341 的完整初始化流程，包括：
 *          1. 硬件复位
 *          2. 电源控制寄存器配置
 *          3. VCM 控制配置
 *          4. 内存访问控制配置
 *          5. 像素格式设置（RGB565, 16bit/pixel）
 *          6. 帧速率控制
 *          7. Gamma 校正曲线配置
 *          8. 显示区域设置
 *          9. 退出睡眠模式并开启显示
 *          10. 设置显示方向和开启背光
 * @param   None
 * @retval  None
 */
void LCD_Init(void)
{
    // 硬件复位 LCD
    LCD_RESET();
    delay_ms(100);
    // ===== 电源控制配置 =====
    LCD_SendCmd(0xCF); // Power Control B（电源控制 B）
    LCD_SendData(0x00);
    LCD_SendData(0xD9);
    LCD_SendData(0X30);
    LCD_SendCmd(0xED); // Power On Sequence Control（上电时序控制）
    LCD_SendData(0x64);
    LCD_SendData(0x03);
    LCD_SendData(0X12);
    LCD_SendData(0X81);
    LCD_SendCmd(0xE8); // Driver Timing Control A（驱动时序控制 A）
    LCD_SendData(0x85);
    LCD_SendData(0x10);
    LCD_SendData(0x7A);
    LCD_SendCmd(0xCB); // Power Control A（电源控制 A）
    LCD_SendData(0x39);
    LCD_SendData(0x2C);
    LCD_SendData(0x00);
    LCD_SendData(0x34);
    LCD_SendData(0x02);
    LCD_SendCmd(0xF7); // Pump Ratio Control（泵比控制）
    LCD_SendData(0x20);
    LCD_SendCmd(0xEA); // Driver Timing Control B（驱动时序控制 B）
    LCD_SendData(0x00);
    LCD_SendData(0x00);
    // Power Control 1 & 2（电源控制 1 和 2）
    LCD_SendCmd(0xC0); // Power Control 1
    LCD_SendData(0x1B);
    LCD_SendCmd(0xC1); // Power Control 2
    LCD_SendData(0x12);
    // VCM Control 1 & 2（VCOM 电压控制）
    LCD_SendCmd(0xC5);
    LCD_SendData(0x08);
    LCD_SendData(0x26);
    LCD_SendCmd(0xC7); // VCM Control 2
    LCD_SendData(0XB7);
    // Memory Access Control（内存访问控制：BGR 顺序）
    LCD_SendCmd(0x36);
    LCD_SendData(0x08);
    // Pixel Format Set（像素格式设置：RGB565, 16bit/pixel）
    LCD_SendCmd(0x3A);
    LCD_SendData(0x55);
    // Frame Rate Control（帧速率控制）
    LCD_SendCmd(0xB1);
    LCD_SendData(0x00);
    LCD_SendData(0x1A);
    // Display Function Control（显示功能控制）
    LCD_SendCmd(0xB6);
    LCD_SendData(0x0A);
    LCD_SendData(0xA2);
    // 3Gamma Function（3Gamma 功能控制）
    LCD_SendCmd(0xF2);
    LCD_SendData(0x00);
    LCD_SendCmd(0x26); // Gamma Set（Gamma 曲线选择）
    LCD_SendData(0x01);
    // ===== 正极性 Gamma 校正（Positive Gamma Correction） =====
    LCD_SendCmd(0xE0);
    LCD_SendData(0x0F);
    LCD_SendData(0x1D);
    LCD_SendData(0x1A);
    LCD_SendData(0x0A);
    LCD_SendData(0x0D);
    LCD_SendData(0x07);
    LCD_SendData(0x49);
    LCD_SendData(0x66);
    LCD_SendData(0x3B);
    LCD_SendData(0x07);
    LCD_SendData(0x11);
    LCD_SendData(0x01);
    LCD_SendData(0x09);
    LCD_SendData(0x05);
    LCD_SendData(0x04);
    // ===== 负极性 Gamma 校正（Negative Gamma Correction） =====
    LCD_SendCmd(0XE1);
    LCD_SendData(0x00);
    LCD_SendData(0x18);
    LCD_SendData(0x1D);
    LCD_SendData(0x02);
    LCD_SendData(0x0F);
    LCD_SendData(0x04);
    LCD_SendData(0x36);
    LCD_SendData(0x13);
    LCD_SendData(0x4C);
    LCD_SendData(0x07);
    LCD_SendData(0x13);
    LCD_SendData(0x0F);
    LCD_SendData(0x2E);
    LCD_SendData(0x2F);
    LCD_SendData(0x05);
    // ===== 设置显示区域 =====
    LCD_SendCmd(0x2B); // Row Address Set（行地址：0 ~ 0x013F = 319）
    LCD_SendData(0x00);
    LCD_SendData(0x00);
    LCD_SendData(0x01);
    LCD_SendData(0x3F);
    LCD_SendCmd(0x2A); // Column Address Set（列地址：0 ~ 0x00EF = 239）
    LCD_SendData(0x00);
    LCD_SendData(0x00);
    LCD_SendData(0x00);
    LCD_SendData(0xEF);
    // ===== 退出睡眠模式 =====
    LCD_SendCmd(0x11); // Sleep Out（退出睡眠）
    HAL_Delay(120);
    // ===== 开启显示 =====
    LCD_SendCmd(0x29); // Display On（显示开启）
    // 设置显示方向（根据配置宏）
    LCD_SetDirection(USE_HORIZONTAL);
    // 打开背光并清屏为白色
    LCD_BackLight_On();
    LCD_Clear(WHITE);
}
/** @brief DMA 传输缓冲区大小（8KB） */
#define LCD_BUF_SIZE 8192
/** @brief 引用外部 SPI3 TX DMA 句柄 */
extern DMA_HandleTypeDef hdma_spi3_tx;
/** @brief LCD DMA 传输缓冲区，4 字节对齐以优化 DMA 性能 */
static uint8_t lcd_buf[LCD_BUF_SIZE] __attribute__((aligned(4)));
/**
 * @brief  填充 LCD 指定矩形区域（支持 DMA 加速）
 * @param  xsta  : 矩形起始 X 坐标
 * @param  ysta  : 矩形起始 Y 坐标
 * @param  xend  : 矩形结束 X 坐标
 * @param  yend  : 矩形结束 Y 坐标
 * @param  color : 16 位 RGB565 颜色数据数组指针
 * @note   当 USE_DMA_FillBlock 宏定义为 1 时使用 DMA 传输（速度快），
 *          否则使用普通 SPI 逐行传输（兼容性好）
 * @retval None
 */
void LCD_FillBlock(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t *color)
{
#if USE_DMA_FillBlock
    uint16_t height, width;
    uint32_t byte_sum;                     // 总字节数
    width = xend - xsta + 1;               // 计算矩形宽度
    height = yend - ysta + 1;              // 计算矩形高度
    byte_sum = width * height * 2;         // 总字节数 = 像素数 × 2 字节/像素
    LCD_SetWindow(xsta, ysta, xend, yend); // 设置 LCD 写入窗口
    LCD_DC_Set();                          // DC=1，数据模式
    LCD_CS_Clr();
    // 分批传输：每批最多发送 LCD_BUF_SIZE 字节
    while (byte_sum > 0)
    {
        uint32_t bytes_to_send = (byte_sum > LCD_BUF_SIZE) ? LCD_BUF_SIZE : byte_sum;
        // 填充 DMA 缓冲区：将 16 位颜色数据拆分为高/低字节
        for (uint32_t i = 0; i < bytes_to_send; i += 2)
        {
            uint8_t high_byte = *color >> 8;  // 获取颜色高 8 字节
            uint8_t low_byte = *color & 0xFF; // 获取颜色低 8 字节
            lcd_buf[i] = high_byte;
            lcd_buf[i + 1] = low_byte;
            color++;
        }
        // 等待上次 DMA 传输完成
        // 若 DMA 忙则阻塞等待，避免数据覆盖
        while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY)
            ;
        // 启动 DMA SPI 传输
        if (HAL_SPI_Transmit_DMA(&hspi3, lcd_buf, bytes_to_send) != HAL_OK)
        {
            /* DMA 未启动(状态未就绪), 退回阻塞发送兜底, 避免丢数据 */
            while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY)
                ;
            HAL_SPI_Transmit(&hspi3, lcd_buf, bytes_to_send, HAL_MAX_DELAY);
        }
        // 等待本次 DMA 传输完成
        while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY)
            ;
        while (__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_BSY) != RESET)
            ;
        // 更新剩余待发送字节数
        byte_sum -= bytes_to_send;
    }
    LCD_CS_Set();
#else
    // 非 DMA 模式：一次性连续逐字节发送（与 LCD_Clear 完全一致，CS 全程保持低）
    uint16_t width = xend - xsta + 1;  // 计算矩形宽度
    uint16_t height = yend - ysta + 1; // 计算矩形高度
    // 设置写入窗口
    LCD_SetWindow(xsta, ysta, xend, yend);
    // 进入数据发送模式
    LCD_DC_Set(); // DC=1，数据模式
    LCD_CS_Clr(); // CS=0，使能片选
    // 一次性连续发送全部像素，不逐行拉高 CS
    for (uint32_t i = 0; i < (uint32_t)width * height; i++)
    {
        uint8_t high_byte = *color >> 8;
        uint8_t low_byte = *color & 0xFF;
        HAL_SPI_Transmit(&hspi3, &high_byte, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&hspi3, &low_byte, 1, HAL_MAX_DELAY);
        color++;
    }
    LCD_CS_Set(); // CS=1，释放片选
#endif
}
/**************************** 基本绘图 GUI 接口 **********************************************************/
/**
 * @brief  在指定坐标绘制单个像素点
 * @param  startX : 像素 X 坐标
 * @param  startY : 像素 Y 坐标
 * @param  color  : 像素颜色（RGB565 格式，16 位）
 * @note   通过设置 1x1 窗口并写入一个 16 位颜色值来实现单点绘制
 * @retval None
 */
void LCD_DrawPoint(uint16_t startX, uint16_t startY, uint16_t color)
{
    // 设置 1x1 像素的写入窗口
    LCD_SetWindow(startX, startY, startX, startY);
    LCD_WriteData_16Bit(color);
}
/*******************************************************************
 * @name       : LCD_DrawLine（画线）
 * @function   : 使用 Bresenham 直线算法在两点之间绘制直线
 * @parameters :
 *      x1    : 起点 X 坐标
 *      y1    : 起点 Y 坐标
 *      x2    : 终点 X 坐标
 *      y2    : 终点 Y 坐标
 *      color : 线条颜色（RGB565 格式）
 * @retvalue   : None
 *
 * @algorithm  : Bresenham 直线算法
 *    - 根据两点坐标差值确定步进方向和步长
 *    - 使用误差累积方式判断何时在次要轴方向步进
 *    - 实现无需浮点运算的高效直线绘制
 ********************************************************************/
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;
    delta_x = x2 - x1; // 计算 X 轴差值
    delta_y = y2 - y1; // 计算 Y 轴差值
    uRow = x1;         // 当前绘制列（X 坐标）
    uCol = y1;         // 当前绘制行（Y 坐标）
    // 确定 X 轴步进方向和步长
    if (delta_x > 0)
        incx = 1; // 正方向，步进 +1
    else if (delta_x == 0)
        incx = 0; // 垂直线，X 不变
    else
    {
        incx = -1;
        delta_x = -delta_x;
    } // 负方向，步进 -1，取绝对值
    // 确定 Y 轴步进方向和步长
    if (delta_y > 0)
        incy = 1; // 正方向，步进 +1
    else if (delta_y == 0)
        incy = 0; // 水平线，Y 不变
    else
    {
        incy = -1;
        delta_y = -delta_y;
    } // 负方向，步进 -1，取绝对值
    // 取较大差值作为绘制步数
    if (delta_x > delta_y)
        distance = delta_x;
    else
        distance = delta_y;
    // Bresenham 迭代绘制
    for (t = 0; t <= distance + 1; t++)
    {
        LCD_DrawPoint(uRow, uCol, color); // 绘制当前点
        xerr += delta_x;                  // 累积 X 误差
        yerr += delta_y;                  // 累积 Y 误差
        // 当误差超过阈值时，在对应方向步进并修正误差
        if (xerr > distance)
        {
            xerr -= distance;
            uRow += incx;
        }
        if (yerr > distance)
        {
            yerr -= distance;
            uCol += incy;
        }
    }
}
/**
 * @brief  绘制实心圆
 * @param  centerX : 圆心 X 坐标
 * @param  centerY : 圆心 Y 坐标
 * @param  radius  : 圆的半径
 * @param  color   : 填充颜色（RGB565 格式）
 * @note   使用中点画圆算法（Midpoint Circle Algorithm）计算圆的边界，
 *         然后通过绘制水平线填充圆内部
 * @retval None
 */
void LCD_DrawFilledCircle(uint16_t centerX, uint16_t centerY, uint16_t radius, uint16_t color)
{
    int16_t x = 0;
    int16_t y = radius;
    int16_t d = 1 - radius;            // 初始决策参数
    int16_t deltaE = 3;                // 水平步进增量
    int16_t deltaSE = -2 * radius + 5; // 对角步进增量
    // 绘制圆心水平直径线
    LCD_DrawLine(centerX - radius, centerY, centerX + radius, centerY, color);
    // 中点画圆算法迭代
    while (y > x)
    {
        if (d < 0) // 选择水平方向步进（E 点）
        {
            d += deltaE;
            deltaE += 2;
            deltaSE += 2;
        }
        else // 选择对角方向步进（SE 点）
        {
            d += deltaSE;
            deltaE += 2;
            deltaSE += 4;
            y--;
        }
        x++;
        // 利用八分对称性绘制四条水平填充线
        LCD_DrawLine(centerX - x, centerY + y, centerX + x, centerY + y, color);
        LCD_DrawLine(centerX - x, centerY - y, centerX + x, centerY - y, color);
        LCD_DrawLine(centerX - y, centerY + x, centerX + y, centerY + x, color);
        LCD_DrawLine(centerX - y, centerY - x, centerX + y, centerY - x, color);
    }
}

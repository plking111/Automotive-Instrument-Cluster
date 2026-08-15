/**
 * @file    ili9341.h
 * @brief   ILI9341 LCD 驱动头文件
 * @details 定义 ILI9341 LCD 驱动所需的宏、数据结构、GPIO 操作和 API 函数声明。
 *          包含屏幕分辨率、颜色定义、引脚映射等硬件相关配置。
 */

#ifndef __ILI9341_H__
#define __ILI9341_H__

#include <stdint.h>
#include "main.h"
#include "spi.h"
#include "gpio.h"
#include "stm32f4xx_hal.h"
#include "delay.h"

/** @brief DMA 快速填充使能宏：1=启用 DMA 传输，0=使用普通 SPI 传输 */
#define USE_DMA_FillBlock 0

/** @brief LCD DMA 传输缓冲区（定义在 ili9341.c，供外部引用） */
extern uint8_t lcd_buf[];

/**
 * @brief LCD 设备参数结构体
 * @note  存储当前 LCD 的工作参数，包括分辨率、显示方向和寄存器命令地址
 */
typedef struct
{
    uint16_t width;   /**< LCD 当前宽度（会根据显示方向在 width/height 之间切换） */
    uint16_t height;  /**< LCD 当前高度 */
    uint16_t id;      /**< LCD 驱动芯片 ID */
    uint8_t dir;      /**< 显示方向：0=竖屏，1=横屏 */
    uint16_t wramcmd; /**< 写 GRAM 命令地址（通常为 0x2C） */
    uint16_t setxcmd; /**< 设置 X 坐标命令地址（通常为 0x2A） */
    uint16_t setycmd; /**< 设置 Y 坐标命令地址（通常为 0x2B） */
} _lcd_dev;

/** @brief 全局 LCD 设备实例（定义在 ili9341.c） */
extern _lcd_dev lcddev;

/**
 * @brief 默认显示方向
 * @note  0=0°（竖屏），1=90°（横屏），2=180°（竖屏倒转），3=270°（横屏倒转）
 */
#define USE_HORIZONTAL 1

// ==================== LCD 物理分辨率 ====================
#define LCD_WIDTH 240  /**< LCD 物理宽度（像素） */
#define LCD_HEIGHT 320 /**< LCD 物理高度（像素） */

// ==================== LCD 控制引脚映射（GPIOB） ====================
#define LCD_RST_GPIO_Port GPIOB /**< 复位引脚端口 */
#define LCD_RST_Pin GPIO_PIN_12 /**< 复位引脚编号 */
#define LCD_DC_GPIO_Port GPIOB  /**< 数据/命令选择引脚端口 */
#define LCD_DC_Pin GPIO_PIN_11  /**< 数据/命令选择引脚编号 */
#define LCD_CS_GPIO_Port GPIOB  /**< SPI 片选引脚端口 */
#define LCD_CS_Pin GPIO_PIN_15  /**< SPI 片选引脚编号 */
#define LCD_LED_GPIO_Port GPIOB /**< 背光控制引脚端口 */
#define LCD_LED_Pin GPIO_PIN_13 /**< 背光控制引脚编号 */

// ==================== GPIO 控制宏 ====================
/** @brief 复位引脚拉低 */
#define LCD_RST_Clr() HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET)
/** @brief 复位引脚拉高 */
#define LCD_RST_Set() HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET)

/** @brief DC 引脚拉低（命令模式：后续 SPI 字节为命令） */
#define LCD_DC_Clr() HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET)
/** @brief DC 引脚拉高（数据模式：后续 SPI 字节为数据） */
#define LCD_DC_Set() HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET)

/** @brief CS 引脚拉低（使能片选，选中 LCD） */
#define LCD_CS_Clr() HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET)
/** @brief CS 引脚拉高（释放片选，取消选中 LCD） */
#define LCD_CS_Set() HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET)

/** @brief 背光开启 */
#define LCD_LED_ON() HAL_GPIO_WritePin(LCD_LED_GPIO_Port, LCD_LED_Pin, GPIO_PIN_SET)
/** @brief 背光关闭 */
#define LCD_LED_OFF() HAL_GPIO_WritePin(LCD_LED_GPIO_Port, LCD_LED_Pin, GPIO_PIN_RESET)

// ==================== RGB565 颜色定义 ====================
#define WHITE 0xFFFF   /**< 白色       RGB(255, 255, 255) */
#define BLACK 0x0000   /**< 黑色       RGB(0, 0, 0)       */
#define BLUE 0x001F    /**< 蓝色       RGB(0, 0, 255)     */
#define BRED 0XF81F    /**< 棕红色     RGB(248, 0, 31)    */
#define GRED 0XFFE0    /**< 灰红色 */
#define GBLUE 0X07FF   /**< 灰蓝色 */
#define RED 0xF800     /**< 红色       RGB(255, 0, 0)     */
#define MAGENTA 0xF81F /**< 品红       RGB(255, 0, 255)   */
#define GREEN 0x07E0   /**< 绿色       RGB(0, 255, 0)     */
#define CYAN 0x7FFF    /**< 青色       RGB(0, 255, 255)   */
#define YELLOW 0xFFE0  /**< 黄色       RGB(255, 255, 0)   */
#define BROWN 0XBC40   /**< 棕色 */
#define BRRED 0XFC07   /**< 棕红色 */
#define GRAY 0X8430    /**< 灰色 */

// ==================== GUI 扩展颜色 ====================
#define DARKBLUE 0X01CF  /**< 深蓝色 */
#define LIGHTBLUE 0X7D7C /**< 浅蓝色 */
#define GRAYBLUE 0X5458  /**< 灰蓝色 */

// ==================== 面板主题颜色（PANEL UI） ====================
#define LIGHTGREEN 0X841F /**< 浅绿色 */
#define LIGHTGRAY 0XEF5B  /**< 浅灰色（面板底色） */
#define LGRAY 0XC618      /**< 浅灰色（面板），通用浅灰 */
#define LGRAYBLUE 0XA651  /**< 浅灰蓝色（界面强调色） */
#define LBBLUE 0X2B12     /**< 浅蓝灰色（界面强调色的浅色款） */

// ==================== API 函数声明 ====================
void LCD_Init(void);
void LCD_Clear(uint16_t color);
void LCD_BackLight_On(void);
void LCD_BackLight_Off(void);

void LCD_FillBlock(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t *color);
void LCD_SetWindow(uint16_t startX, uint16_t startY, uint16_t endX, uint16_t endY);

void LCD_DrawPoint(uint16_t startX, uint16_t startY, uint16_t color);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_DrawFilledCircle(uint16_t centerX, uint16_t centerY, uint16_t radius, uint16_t color);

#endif // __ILI9341_H__

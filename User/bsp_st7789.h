#ifndef __BSP_ST7789_H
#define __BSP_ST7789_H

#include "stm32f10x.h"

#define ST7789_WIDTH               240U    // 屏幕可见区域宽度
#define ST7789_HEIGHT              240U    // 屏幕可见区域高度
#define ST7789_X_OFFSET            0U      // 可见区域在控制器显存中的 X 偏移
#define ST7789_Y_OFFSET            0U      // 本面板可见区域从显存第 0 行开始
#define ST7789_USE_CS              0U      // 七针模块没有 CS 引脚
#define ST7789_SPI_MODE            3U      // 当前屏幕实测使用 SPI Mode 3
#define ST7789_SPI_PRESCALER       SPI_BaudRatePrescaler_2 // SPI2约18 MHz
#define ST7789_MADCTL_VALUE        0x00U   // 默认扫描方向和 RGB 顺序

#define ST7789_BLACK               0x0000U // RGB565 黑色
#define ST7789_WHITE               0xFFFFU // RGB565 白色
#define ST7789_RED                 0xF800U // RGB565 红色
#define ST7789_GREEN               0x07E0U // RGB565 绿色
#define ST7789_BLUE                0x001FU // RGB565 蓝色
#define ST7789_YELLOW              0xFFE0U // RGB565 黄色
#define ST7789_CYAN                0x07FFU // RGB565 青色
#define ST7789_MAGENTA             0xF81FU // RGB565 品红色

/* 保留旧颜色名称，避免后续恢复未参与当前构建的实验代码时立即失效。 */
#define ST7789_COLOR_BLACK         ST7789_BLACK
#define ST7789_COLOR_WHITE         ST7789_WHITE
#define ST7789_COLOR_RED           ST7789_RED
#define ST7789_COLOR_GREEN         ST7789_GREEN
#define ST7789_COLOR_BLUE          ST7789_BLUE

void ST7789_GPIO_Init(void);
void ST7789_SPI_Init(void);
void ST7789_WriteByte(uint8_t data);
void ST7789_WriteCmd(uint8_t cmd);
void ST7789_WriteData8(uint8_t data);
void ST7789_WriteData16(uint16_t data);
void ST7789_Reset(void);
void ST7789_Backlight_On(void);
void ST7789_Backlight_Off(void);
void ST7789_Init(void);
void ST7789_SetAddressWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
);
void ST7789_BeginDataWrite(void);
void ST7789_WaitWriteComplete(void);
void ST7789_Clear(uint16_t color);
void ST7789_FillRect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t color
);
void ST7789_ShowIndexed4Image(
    const uint8_t *image,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
);
void ST7789_ApplyIndexed4Delta(
    const uint8_t *delta,
    uint32_t delta_size,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
);
typedef enum
{
    ST7789_ANIM_DELTA_RESULT_BUSY = 0,  // Job is still active.
    ST7789_ANIM_DELTA_RESULT_DONE,      // Job completed normally.
    ST7789_ANIM_DELTA_RESULT_ERROR      // Job stopped because DMA or data failed.
} ST7789_AnimDeltaResult;

uint8_t ST7789_AnimDeltaStart(
    const uint8_t *delta,
    uint32_t delta_size,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
);
ST7789_AnimDeltaResult ST7789_AnimDeltaTask(void);
uint8_t ST7789_AnimDeltaBusy(void);

#endif

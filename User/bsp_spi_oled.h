#ifndef __BSP_SPI_OLED_H
#define __BSP_SPI_OLED_H

#include "stm32f10x.h"

#define OLED_SPI_WIDTH             128U    // SSD1306 显示宽度
#define OLED_SPI_HEIGHT            64U     // SSD1306 显示高度
#define OLED_SPI_PAGE_COUNT        8U      // 纵向 8 像素为一页
#define OLED_SPI_FRAME_SIZE        1024U   // 一帧全屏图像字节数

void OLED_SPI_Init(void);
void OLED_SPI_WriteByte(uint8_t data);
void OLED_SPI_WriteCmd(uint8_t cmd);
void OLED_SPI_WriteData(uint8_t data);
void OLED_SPI_Reset(void);
void OLED_SPI_PanelInit(void);
void OLED_SPI_Clear(void);
void OLED_SPI_SetPos(uint8_t page, uint8_t col);
void OLED_SPI_ShowChar6x8(uint8_t page, uint8_t col, char ch);
void OLED_SPI_ShowString6x8(uint8_t page, uint8_t col, const char *text);
void OLED_SPI_ShowImage128x64(const uint8_t *image);

#endif

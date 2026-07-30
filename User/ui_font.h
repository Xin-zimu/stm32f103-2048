#ifndef __UI_FONT_H
#define __UI_FONT_H

#include "stm32f10x.h"

#define UI_FONT_WIDTH          8U      // Character cell width.
#define UI_FONT_HEIGHT        12U      // Character cell height.
#define UI_FONT_LARGE_WIDTH   12U      // Large ASCII cell width.
#define UI_FONT_LARGE_HEIGHT  18U      // Large ASCII cell height.
#define UI_FONT_CN_WIDTH      16U      // GB2312 Chinese glyph width.
#define UI_FONT_CN_HEIGHT     16U      // GB2312 Chinese glyph height.

const uint8_t *UI_FontGetGlyph(char ch);
const uint8_t *UI_FontGetChineseGlyph(uint16_t gb2312_code);

#endif

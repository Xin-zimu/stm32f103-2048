#ifndef __UI_DRAW_H
#define __UI_DRAW_H

#include "ui_types.h"

void UI_DrawClear(uint16_t color);
void UI_DrawBeginBuffer(uint8_t *buffer, const UI_Rect *strip, const UI_Rect *clip);
void UI_DrawEndBuffer(void);
void UI_DrawSetClip(const UI_Rect *clip);
void UI_DrawClearClip(uint16_t color);
void UI_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void UI_DrawFrame(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void UI_DrawText(int16_t x, int16_t y, const char *text, uint16_t color);
void UI_DrawTextLarge(int16_t x, int16_t y, const char *text, uint16_t color);
void UI_DrawTextCN(int16_t x, int16_t y, const char *text, uint16_t color);
void UI_DrawStatusBar(const char *title, uint16_t accent_color);
void UI_DrawFooter(const char *hint);
void UI_DrawMenuRow(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected
);
void UI_DrawMenuRowCN(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected
);
void UI_DrawMenuRowCNEx(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected,
    uint8_t pressed
);
void UI_DrawFocusMarker(int16_t x, int16_t y, int16_t h, uint16_t color);
void UI_DrawInfoRowCN(int16_t y, const char *label, const char *value);
void UI_DrawProgressBar(int16_t x, int16_t y, int16_t w, uint8_t value, uint8_t max);
void UI_DrawToggle(int16_t x, int16_t y, uint8_t enabled);

#endif

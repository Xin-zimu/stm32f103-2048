#ifndef __UI_TYPES_H
#define __UI_TYPES_H

#include "app_config.h"
#include "stm32f10x.h"

#define UI_SCREEN_W          APP_LCD_WIDTH       // ST7789 visible width.
#define UI_SCREEN_H          APP_LCD_HEIGHT      // ST7789 visible height.
#define UI_STATUS_H          APP_UI_STATUS_H     // Top status bar height.
#define UI_FOOTER_H          APP_UI_FOOTER_H     // Bottom hint area height.
#define UI_MARGIN            APP_UI_MARGIN       // Standard outer margin.
#define UI_ROW_H             APP_UI_ROW_H        // Standard row height.
#define UI_ROW_GAP           APP_UI_ROW_GAP      // Gap between rows.

#define UI_COLOR_BG        0x0841U    // Dark neutral background.
#define UI_COLOR_SURFACE   0x2104U    // Primary surface color.
#define UI_COLOR_SURFACE_2 0x3186U    // Secondary surface color.
#define UI_COLOR_TEXT      0xFFFFU    // Primary text color.
#define UI_COLOR_MUTED     0xA514U    // Muted text and hint color.
#define UI_COLOR_DIM       0x632CU    // Low-emphasis divider color.
#define UI_COLOR_ACCENT    0x07FFU    // Cyan accent color.
#define UI_COLOR_SELECTED  0xFD20U    // Selected row color.
#define UI_COLOR_OK        0x07E0U    // Success state color.
#define UI_COLOR_WARN      0xFBE0U    // Warning state color.
#define UI_COLOR_DANGER    0xF800U    // Error or reset state color.

typedef struct
{
    int16_t x;                         // Left coordinate.
    int16_t y;                         // Top coordinate.
    int16_t w;                         // Width in pixels.
    int16_t h;                         // Height in pixels.
} UI_Rect;

typedef enum
{
    UI_PAGE_HOME = 0,                  // Main menu page.
    UI_PAGE_GAME,                      // 2048 game page.
    UI_PAGE_SNAKE,                     // Snake game page.
    UI_PAGE_PAUSE,                     // 2048 pause menu page.
    UI_PAGE_SETTINGS,                  // Settings page.
    UI_PAGE_INFO,                      // System information page.
    UI_PAGE_COUNT                      // Number of registered pages.
} UI_PageId;

#endif

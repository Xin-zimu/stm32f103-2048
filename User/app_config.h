#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include "stm32f10x.h"

#define APP_LCD_WIDTH                    240U     // Visible ST7789 width in pixels.
#define APP_LCD_HEIGHT                   240U     // Visible ST7789 height in pixels.

#define APP_LCD_DMA_MAX_BYTES          65535U     // DMA CNDTR limit for one SPI transfer.
#define APP_LCD_DMA_LINE_BUFFERS           2U     // Existing ST7789 line buffer count.

#define APP_UI_STRIP_HEIGHT                4U     // Future UI renderer strip height.
#define APP_UI_STRIP_BUFFER_COUNT          2U     // Future UI renderer buffer count.
#define APP_UI_DIRTY_RECT_MAX              8U     // Maximum tracked local refresh areas.
#define APP_UI_EVENT_QUEUE_SIZE           16U     // Fixed UI event queue capacity.
#define APP_UI_ANIMATION_MAX               8U     // Reserved animation slot count.
#define APP_UI_PAGE_STACK_DEPTH            4U     // Reserved page stack depth.

#define APP_UI_STATUS_H                   32U     // Top status bar height.
#define APP_UI_FOOTER_H                   28U     // Bottom hint area height.
#define APP_UI_MARGIN                     12U     // Standard outer margin.
#define APP_UI_ROW_H                      48U     // Standard menu/control row height.
#define APP_UI_ROW_GAP                     6U     // Gap between rows.

#define APP_KEY_SCAN_INTERVAL_MS          10U     // Key scan period in milliseconds.
#define APP_KEY_DEBOUNCE_MS               20U     // Stable time before key state changes.
#define APP_KEY_LONG_PRESS_MS            600U     // Long press threshold.
#define APP_KEY_REPEAT_DELAY_MS          350U     // First repeat delay for direction keys.
#define APP_KEY_REPEAT_INTERVAL_MS       100U     // Repeat interval after first repeat.
#define APP_KEY_RST_RESET_MS            2000U     // RST hold time before software reset.

#define APP_UI_TARGET_FRAME_INTERVAL_MS   16U     // Target local animation interval.
#define APP_UI_PAGE_TRANSITION_MS        220U     // Reserved page transition duration.
#define APP_UI_FOCUS_ANIMATION_MS        160U     // Current focus marker slide duration.
#define APP_UI_FOCUS_ANIMATION_STEP_MS    16U     // Minimum focus marker repaint interval.
#define APP_UI_FEEDBACK_MS                60U     // Pressed feedback duration.

#define APP_UI_ENABLE_PERFORMANCE_STATS    1U     // Keep lightweight counters available.
#define APP_UI_ENABLE_PAGE_TRANSITION      0U     // Sliding transitions remain disabled.
#define APP_UI_ENABLE_BACKLIGHT_PWM        0U     // PB12 backlight is GPIO-only for now.

#endif

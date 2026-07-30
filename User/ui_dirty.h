#ifndef __UI_DIRTY_H
#define __UI_DIRTY_H

#include "ui_types.h"

#define UI_DIRTY_MAX_RECTS       APP_UI_DIRTY_RECT_MAX       // Maximum tracked local refresh areas.

typedef struct
{
    uint32_t overflow_count;           // Times local dirty list promoted to full-screen.
    uint32_t full_screen_count;        // Times a full-screen repaint was requested.
    uint8_t max_pending_rects;         // Highest queued local dirty count observed.
    uint8_t pending_rects;             // Current queued local dirty count.
    uint8_t full_screen_pending;       // Nonzero when full-screen repaint is pending.
} UI_DirtyStats;

void UI_DirtyInit(void);
void UI_DirtyAdd(const UI_Rect *rect);
void UI_DirtyAddIsolated(const UI_Rect *rect);
void UI_DirtyAddXYWH(int16_t x, int16_t y, int16_t w, int16_t h);
void UI_DirtyAddUnion(const UI_Rect *a, const UI_Rect *b);
void UI_DirtyFullScreen(void);
uint8_t UI_DirtyPop(UI_Rect *rect);
void UI_DirtyClear(void);
void UI_DirtyGetStats(UI_DirtyStats *stats);
void UI_DirtyResetStats(void);

#endif

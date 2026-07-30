#ifndef __UI_PAGE_H
#define __UI_PAGE_H

#include "ui_event.h"
#include "ui_types.h"

typedef struct
{
    void (*on_enter)(void);                // Called when the page becomes active.
    void (*on_event)(const UI_Event *event); // Called for non-global events.
    void (*task)(uint32_t now);            // Optional periodic page work.
    void (*draw)(const UI_Rect *clip);     // Draws content intersecting clip.
} UI_PageOps;

void UI_PageInit(void);
void UI_PageTask(uint32_t now);
void UI_PageDispatchEvent(const UI_Event *event);
void UI_PageGoto(UI_PageId page);
void UI_PageBack(void);
void UI_PageHome(void);
UI_PageId UI_PageGetCurrent(void);
void UI_PageRequestRedraw(void);
void UI_PageInvalidate(const UI_Rect *rect);
void UI_PageInvalidateXYWH(int16_t x, int16_t y, int16_t w, int16_t h);

#endif

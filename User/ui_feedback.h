#ifndef __UI_FEEDBACK_H
#define __UI_FEEDBACK_H

#include "ui_types.h"

#define UI_FEEDBACK_MS        APP_UI_FEEDBACK_MS      // Visual press feedback duration.

void UI_FeedbackInit(void);
void UI_FeedbackPress(const UI_Rect *rect, uint32_t now);
void UI_FeedbackTask(uint32_t now);
uint8_t UI_FeedbackIsActive(const UI_Rect *rect, uint32_t now);

#endif

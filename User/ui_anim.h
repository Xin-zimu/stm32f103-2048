#ifndef __UI_ANIM_H
#define __UI_ANIM_H

#include "ui_types.h"

#define UI_FOCUS_ANIM_MS      APP_UI_FOCUS_ANIMATION_MS        // Focus slide duration.
#define UI_FOCUS_ANIM_STEP_MS APP_UI_FOCUS_ANIMATION_STEP_MS   // Minimum repaint interval.

typedef struct
{
    uint8_t active;                     // Nonzero while the animation is running.
    int16_t from_y;                     // Start Y coordinate.
    int16_t to_y;                       // Target Y coordinate.
    int16_t current_y;                  // Last computed Y coordinate.
    int16_t last_y;                     // Previous Y coordinate for dirty union.
    uint32_t start_ms;                  // Animation start timestamp.
    uint32_t last_step_ms;              // Last repaint timestamp.
} UI_FocusAnim;

void UI_FocusAnimInit(UI_FocusAnim *anim, int16_t y);
void UI_FocusAnimStart(UI_FocusAnim *anim, int16_t from_y, int16_t to_y, uint32_t now);
uint8_t UI_FocusAnimTask(UI_FocusAnim *anim, uint32_t now, UI_Rect *dirty);
int16_t UI_FocusAnimGetY(const UI_FocusAnim *anim);

#endif

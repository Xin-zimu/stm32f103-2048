#include "ui_anim.h"

/*
 * Calculate an Ease Out Cubic interpolation value.
 *
 * The math uses 16-bit progress units instead of floating point so it remains
 * cheap on STM32F103. The returned value ranges from 0 to 1024.
 *
 * Parameters:
 * elapsed_ms: Time elapsed since animation start.
 * duration_ms: Total animation duration.
 *
 * Return value:
 * Eased progress in 0..1024 units.
 *
 * Side effects:
 * None.
 */
static uint16_t UI_AnimEaseOutCubic(uint32_t elapsed_ms, uint16_t duration_ms)
{
    uint32_t t;
    uint32_t inv;

    if (elapsed_ms >= duration_ms)
    {
        return 1024U;
    }

    t = (elapsed_ms * 1024U) / duration_ms;
    inv = 1024U - t;

    return (uint16_t)(1024U - ((inv * inv * inv) >> 20));
}

/*
 * Build a row-level dirty rectangle for old and new focus marker positions.
 *
 * The strip renderer currently sends full-width bands for each dirty Y range.
 * A narrow marker-only dirty rectangle would still become a full-width DMA
 * transfer, but it would not merge with pending row cleanup. Covering complete
 * rows lets selection background and moving marker pixels repaint together.
 *
 * Parameters:
 * old_y: Previous marker Y coordinate.
 * new_y: New marker Y coordinate.
 * dirty: Receives the row-level repaint rectangle.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes dirty.
 */
static void UI_FocusAnimBuildDirty(int16_t old_y, int16_t new_y, UI_Rect *dirty)
{
    int16_t top;
    int16_t bottom;

    top = (old_y < new_y) ? old_y : new_y;
    bottom = (old_y > new_y) ? old_y : new_y;

    dirty->x = 0;
    dirty->y = top;
    dirty->w = (int16_t)UI_SCREEN_W;
    dirty->h = (int16_t)((bottom - top) + (int16_t)UI_ROW_H);
}

/*
 * Initialize a focus animation at a fixed position.
 *
 * Parameters:
 * anim: Animation state to initialize.
 * y: Initial marker Y coordinate.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets the supplied animation state.
 */
void UI_FocusAnimInit(UI_FocusAnim *anim, int16_t y)
{
    if (anim == 0)
    {
        return;
    }

    anim->active = 0U;
    anim->from_y = y;
    anim->to_y = y;
    anim->current_y = y;
    anim->last_y = y;
    anim->start_ms = 0U;
    anim->last_step_ms = 0U;
}

/*
 * Start a focus slide animation.
 *
 * Parameters:
 * anim: Animation state to update.
 * from_y: Start marker Y coordinate.
 * to_y: Target marker Y coordinate.
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the supplied animation state.
 */
void UI_FocusAnimStart(UI_FocusAnim *anim, int16_t from_y, int16_t to_y, uint32_t now)
{
    int16_t start_y;

    if (anim == 0)
    {
        return;
    }

    start_y = (anim->active != 0U) ? anim->current_y : from_y;
    anim->active = (start_y != to_y) ? 1U : 0U;
    anim->from_y = start_y;
    anim->to_y = to_y;
    anim->current_y = start_y;
    anim->last_y = start_y;
    anim->start_ms = now;
    anim->last_step_ms = now;
}

/*
 * Advance a focus animation and return the area to repaint.
 *
 * Parameters:
 * anim: Animation state to advance.
 * now: Current system tick.
 * dirty: Receives the union of the old and new marker areas.
 *
 * Return value:
 * 1: Animation moved and dirty is valid.
 * 0: No repaint is needed.
 *
 * Side effects:
 * Updates the supplied animation state.
 */
uint8_t UI_FocusAnimTask(UI_FocusAnim *anim, uint32_t now, UI_Rect *dirty)
{
    uint32_t elapsed;
    uint16_t eased;
    int16_t distance;
    int16_t next_y;

    if ((anim == 0) || (dirty == 0) || (anim->active == 0U))
    {
        return 0U;
    }

    if ((now - anim->last_step_ms) < UI_FOCUS_ANIM_STEP_MS)
    {
        return 0U;
    }

    elapsed = now - anim->start_ms;
    eased = UI_AnimEaseOutCubic(elapsed, UI_FOCUS_ANIM_MS);
    distance = (int16_t)(anim->to_y - anim->from_y);
    next_y = (int16_t)(anim->from_y + ((distance * (int16_t)eased) / 1024));
    if (elapsed >= UI_FOCUS_ANIM_MS)
    {
        next_y = anim->to_y;
        anim->active = 0U;
    }

    if (next_y == anim->current_y)
    {
        anim->last_step_ms = now;
        return 0U;
    }

    anim->last_y = anim->current_y;
    anim->current_y = next_y;
    anim->last_step_ms = now;
    UI_FocusAnimBuildDirty(anim->last_y, anim->current_y, dirty);

    return 1U;
}

/*
 * Read the current focus marker Y coordinate.
 *
 * Parameters:
 * anim: Animation state to inspect.
 *
 * Return value:
 * Current Y coordinate, or 0 when anim is invalid.
 *
 * Side effects:
 * None.
 */
int16_t UI_FocusAnimGetY(const UI_FocusAnim *anim)
{
    if (anim == 0)
    {
        return 0;
    }

    return anim->current_y;
}

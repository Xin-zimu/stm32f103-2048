#include "ui_dirty.h"

#define UI_DIRTY_MERGE_GAP       2        // Pixel gap treated as one repaint area.

typedef struct
{
    UI_Rect rects[UI_DIRTY_MAX_RECTS];     // Pending local repaint rectangles.
    uint8_t count;                         // Number of valid rectangles.
    uint8_t full_screen;                   // Nonzero when a full repaint is queued.
} UI_DirtyList;

static UI_DirtyList g_ui_dirty;
static UI_DirtyStats g_ui_dirty_stats;

/*
 * Update the maximum observed pending dirty count.
 *
 * The value is used as a lightweight load indicator while tuning UI refresh
 * scheduling. It only tracks local dirty rectangles because a pending
 * full-screen refresh clears the local list by design.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May update g_ui_dirty_stats.max_pending_rects.
 */
static void UI_DirtyUpdateMaxPending(void)
{
    if (g_ui_dirty.count > g_ui_dirty_stats.max_pending_rects)
    {
        g_ui_dirty_stats.max_pending_rects = g_ui_dirty.count;
    }
}

/*
 * Clip a rectangle to the visible ST7789 area.
 *
 * Invalid or fully off-screen rectangles are rejected before they can enter
 * the dirty list. This keeps later drawing code simple and prevents unsigned
 * coordinate wrap when the repaint is passed to the LCD driver.
 *
 * Parameters:
 * rect: Rectangle to clip in place.
 *
 * Return value:
 * 1: Rectangle remains valid after clipping.
 * 0: Rectangle is empty or outside the screen.
 *
 * Side effects:
 * May modify rect.
 */
static uint8_t UI_DirtyClipRect(UI_Rect *rect)
{
    int16_t right;
    int16_t bottom;

    if ((rect == 0) || (rect->w <= 0) || (rect->h <= 0))
    {
        return 0U;
    }

    right = (int16_t)(rect->x + rect->w);
    bottom = (int16_t)(rect->y + rect->h);
    if ((right <= 0) || (bottom <= 0) ||
        (rect->x >= (int16_t)UI_SCREEN_W) ||
        (rect->y >= (int16_t)UI_SCREEN_H))
    {
        return 0U;
    }

    if (rect->x < 0)
    {
        rect->x = 0;
    }
    if (rect->y < 0)
    {
        rect->y = 0;
    }
    if (right > (int16_t)UI_SCREEN_W)
    {
        right = (int16_t)UI_SCREEN_W;
    }
    if (bottom > (int16_t)UI_SCREEN_H)
    {
        bottom = (int16_t)UI_SCREEN_H;
    }

    rect->w = (int16_t)(right - rect->x);
    rect->h = (int16_t)(bottom - rect->y);

    return ((rect->w > 0) && (rect->h > 0)) ? 1U : 0U;
}

/*
 * Test whether two rectangles should be merged.
 *
 * Adjacent controls on this UI are separated by only a few pixels. Merging
 * overlapping or nearly touching dirty areas reduces ST7789 address-window
 * setup overhead without reverting every small change into a full-screen draw.
 *
 * Parameters:
 * a: First clipped rectangle.
 * b: Second clipped rectangle.
 *
 * Return value:
 * 1: Rectangles overlap or are close enough to repaint together.
 * 0: Rectangles should remain separate.
 *
 * Side effects:
 * None.
 */
static uint8_t UI_DirtyShouldMerge(const UI_Rect *a, const UI_Rect *b)
{
    int16_t a_right;
    int16_t a_bottom;
    int16_t b_right;
    int16_t b_bottom;

    a_right = (int16_t)(a->x + a->w);
    a_bottom = (int16_t)(a->y + a->h);
    b_right = (int16_t)(b->x + b->w);
    b_bottom = (int16_t)(b->y + b->h);

    if ((a_right + UI_DIRTY_MERGE_GAP) < b->x)
    {
        return 0U;
    }
    if ((b_right + UI_DIRTY_MERGE_GAP) < a->x)
    {
        return 0U;
    }
    if ((a_bottom + UI_DIRTY_MERGE_GAP) < b->y)
    {
        return 0U;
    }
    if ((b_bottom + UI_DIRTY_MERGE_GAP) < a->y)
    {
        return 0U;
    }

    return 1U;
}

/*
 * Expand one rectangle so it covers another rectangle as well.
 *
 * Parameters:
 * target: Rectangle to expand.
 * source: Rectangle that must become covered by target.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Modifies target.
 */
static void UI_DirtyUnionInto(UI_Rect *target, const UI_Rect *source)
{
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;

    left = (target->x < source->x) ? target->x : source->x;
    top = (target->y < source->y) ? target->y : source->y;
    right = ((target->x + target->w) > (source->x + source->w)) ?
        (int16_t)(target->x + target->w) :
        (int16_t)(source->x + source->w);
    bottom = ((target->y + target->h) > (source->y + source->h)) ?
        (int16_t)(target->y + target->h) :
        (int16_t)(source->y + source->h);

    target->x = left;
    target->y = top;
    target->w = (int16_t)(right - left);
    target->h = (int16_t)(bottom - top);
}

/*
 * Reset all pending dirty areas.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears the module-local repaint queue.
 */
void UI_DirtyInit(void)
{
    g_ui_dirty.count = 0U;
    g_ui_dirty.full_screen = 0U;
    UI_DirtyResetStats();
}

/*
 * Add a local dirty rectangle.
 *
 * The rectangle is clipped to the screen, merged with existing nearby areas,
 * and promoted to a full-screen repaint only when the fixed dirty list is
 * exhausted. The UI has no dynamic memory, so overflow must be deterministic.
 *
 * Parameters:
 * rect: Rectangle that must be redrawn.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the pending dirty list.
 */
void UI_DirtyAdd(const UI_Rect *rect)
{
    UI_Rect clipped;
    uint8_t index;

    if (g_ui_dirty.full_screen != 0U)
    {
        return;
    }
    if (rect == 0)
    {
        return;
    }

    clipped = *rect;
    if (UI_DirtyClipRect(&clipped) == 0U)
    {
        return;
    }

    for (index = 0U; index < g_ui_dirty.count; index++)
    {
        if (UI_DirtyShouldMerge(&g_ui_dirty.rects[index], &clipped) != 0U)
        {
            UI_DirtyUnionInto(&g_ui_dirty.rects[index], &clipped);
            UI_DirtyUpdateMaxPending();
            return;
        }
    }

    if (g_ui_dirty.count >= UI_DIRTY_MAX_RECTS)
    {
        g_ui_dirty_stats.overflow_count++;
        UI_DirtyFullScreen();
        return;
    }

    g_ui_dirty.rects[g_ui_dirty.count] = clipped;
    g_ui_dirty.count++;
    UI_DirtyUpdateMaxPending();
}

/*
 * Add a dirty rectangle without merging it with existing areas.
 *
 * Some future callers may need to keep a repaint request separate from nearby
 * areas. The current strip renderer usually prefers merged row-level focus
 * repaint, but this entry point remains available for deliberately isolated
 * work. It still clips and promotes overflow to full-screen.
 *
 * Parameters:
 * rect: Rectangle that must be redrawn independently.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the pending dirty list.
 */
void UI_DirtyAddIsolated(const UI_Rect *rect)
{
    UI_Rect clipped;

    if (g_ui_dirty.full_screen != 0U)
    {
        return;
    }
    if (rect == 0)
    {
        return;
    }

    clipped = *rect;
    if (UI_DirtyClipRect(&clipped) == 0U)
    {
        return;
    }

    if (g_ui_dirty.count >= UI_DIRTY_MAX_RECTS)
    {
        g_ui_dirty_stats.overflow_count++;
        UI_DirtyFullScreen();
        return;
    }

    g_ui_dirty.rects[g_ui_dirty.count] = clipped;
    g_ui_dirty.count++;
    UI_DirtyUpdateMaxPending();
}

/*
 * Add a dirty rectangle from raw coordinates.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Width in pixels.
 * h: Height in pixels.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the pending dirty list.
 */
void UI_DirtyAddXYWH(int16_t x, int16_t y, int16_t w, int16_t h)
{
    UI_Rect rect;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    UI_DirtyAdd(&rect);
}

/*
 * Add the union of two rectangles as one dirty area.
 *
 * Animation repaint often needs to cover the previous and current marker
 * positions together. Adding their union avoids fragmented ST7789 windows and
 * keeps the repaint visually coherent.
 *
 * Parameters:
 * a: First rectangle.
 * b: Second rectangle.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Adds one clipped union rectangle to the dirty list.
 */
void UI_DirtyAddUnion(const UI_Rect *a, const UI_Rect *b)
{
    UI_Rect merged;

    if ((a == 0) || (b == 0))
    {
        return;
    }

    merged = *a;
    UI_DirtyUnionInto(&merged, b);
    UI_DirtyAdd(&merged);
}

/*
 * Queue a full-screen repaint.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears local dirty rectangles and marks the whole screen dirty.
 */
void UI_DirtyFullScreen(void)
{
    g_ui_dirty.count = 0U;
    g_ui_dirty.full_screen = 1U;
    g_ui_dirty_stats.full_screen_count++;
}

/*
 * Pop one pending dirty rectangle.
 *
 * A queued full-screen repaint is emitted as one 240x240 rectangle. Local
 * rectangles are popped in insertion order so old row cleanup cannot be
 * starved by a burst of newer focus animation rectangles. The list is tiny,
 * so shifting entries is cheaper than showing stale pixels.
 *
 * Parameters:
 * rect: Receives the next dirty rectangle.
 *
 * Return value:
 * 1: A rectangle was returned.
 * 0: No dirty work is pending or rect is invalid.
 *
 * Side effects:
 * Removes the returned dirty area from the queue.
 */
uint8_t UI_DirtyPop(UI_Rect *rect)
{
    uint8_t index;

    if (rect == 0)
    {
        return 0U;
    }

    if (g_ui_dirty.full_screen != 0U)
    {
        rect->x = 0;
        rect->y = 0;
        rect->w = (int16_t)UI_SCREEN_W;
        rect->h = (int16_t)UI_SCREEN_H;
        g_ui_dirty.full_screen = 0U;
        return 1U;
    }

    if (g_ui_dirty.count == 0U)
    {
        return 0U;
    }

    *rect = g_ui_dirty.rects[0];
    for (index = 1U; index < g_ui_dirty.count; index++)
    {
        g_ui_dirty.rects[index - 1U] = g_ui_dirty.rects[index];
    }
    g_ui_dirty.count--;

    return 1U;
}

/*
 * Clear all pending repaint work.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Drops all queued local and full-screen repaint requests.
 */
void UI_DirtyClear(void)
{
    g_ui_dirty.count = 0U;
    g_ui_dirty.full_screen = 0U;
}

/*
 * Read current dirty queue statistics.
 *
 * This accessor lets later UI scheduling code inspect whether repaint requests
 * are overflowing or building up. It copies the counters instead of
 * exposing the internal dirty list, keeping the drawing contract unchanged.
 *
 * Parameters:
 * stats: Destination structure that receives the latest counters.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes stats when the pointer is valid.
 */
void UI_DirtyGetStats(UI_DirtyStats *stats)
{
    if (stats == 0)
    {
        return;
    }

    *stats = g_ui_dirty_stats;
    stats->pending_rects = g_ui_dirty.count;
    stats->full_screen_pending = g_ui_dirty.full_screen;
}

/*
 * Reset dirty queue statistics without changing pending repaint work.
 *
 * Runtime diagnostics can call this after a manual test starts so max pending
 * and overflow counters describe only that test interval. The pending repaint
 * list itself is not touched.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears accumulated dirty statistics.
 */
void UI_DirtyResetStats(void)
{
    g_ui_dirty_stats.overflow_count = 0U;
    g_ui_dirty_stats.full_screen_count = 0U;
    g_ui_dirty_stats.max_pending_rects = g_ui_dirty.count;
    g_ui_dirty_stats.pending_rects = g_ui_dirty.count;
    g_ui_dirty_stats.full_screen_pending = g_ui_dirty.full_screen;
}

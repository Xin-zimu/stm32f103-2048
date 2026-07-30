#include "ui_feedback.h"
#include "ui_dirty.h"

typedef struct
{
    uint8_t active;                     // Nonzero while feedback is visible.
    UI_Rect rect;                       // Area using the pressed state.
    uint32_t end_ms;                    // Timestamp when feedback ends.
} UI_FeedbackState;

static UI_FeedbackState g_ui_feedback;

/*
 * Compare two rectangles for exact equality.
 *
 * Parameters:
 * a: First rectangle.
 * b: Second rectangle.
 *
 * Return value:
 * 1: Rectangles match.
 * 0: Rectangles differ or either pointer is invalid.
 *
 * Side effects:
 * None.
 */
static uint8_t UI_FeedbackRectEquals(const UI_Rect *a, const UI_Rect *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    return ((a->x == b->x) && (a->y == b->y) &&
            (a->w == b->w) && (a->h == b->h)) ? 1U : 0U;
}

/*
 * Reset visual feedback state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears any active feedback.
 */
void UI_FeedbackInit(void)
{
    g_ui_feedback.active = 0U;
    g_ui_feedback.rect.x = 0;
    g_ui_feedback.rect.y = 0;
    g_ui_feedback.rect.w = 0;
    g_ui_feedback.rect.h = 0;
    g_ui_feedback.end_ms = 0U;
}

/*
 * Start a short visual press feedback.
 *
 * Parameters:
 * rect: Row or control area to show as pressed.
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Stores feedback state and marks the area dirty immediately.
 */
void UI_FeedbackPress(const UI_Rect *rect, uint32_t now)
{
    if (rect == 0)
    {
        return;
    }

    g_ui_feedback.active = 1U;
    g_ui_feedback.rect = *rect;
    g_ui_feedback.end_ms = now + UI_FEEDBACK_MS;
    UI_DirtyAdd(rect);
}

/*
 * Expire visual feedback after its short display window.
 *
 * Parameters:
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Marks the feedback rectangle dirty when pressed state ends.
 */
void UI_FeedbackTask(uint32_t now)
{
    if (g_ui_feedback.active == 0U)
    {
        return;
    }

    if ((int32_t)(now - g_ui_feedback.end_ms) >= 0)
    {
        g_ui_feedback.active = 0U;
        UI_DirtyAdd(&g_ui_feedback.rect);
    }
}

/*
 * Check whether a rectangle should use pressed visual state.
 *
 * Parameters:
 * rect: Row or control rectangle being drawn.
 * now: Current system tick.
 *
 * Return value:
 * 1: The rectangle is inside its feedback window.
 * 0: Normal visual state should be used.
 *
 * Side effects:
 * None.
 */
uint8_t UI_FeedbackIsActive(const UI_Rect *rect, uint32_t now)
{
    if (g_ui_feedback.active == 0U)
    {
        return 0U;
    }
    if ((int32_t)(now - g_ui_feedback.end_ms) >= 0)
    {
        return 0U;
    }

    return UI_FeedbackRectEquals(rect, &g_ui_feedback.rect);
}

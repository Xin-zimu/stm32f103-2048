#include "page_home.h"
#include "ui_anim.h"
#include "ui_dirty.h"
#include "ui_draw.h"
#include "ui_feedback.h"

#define PAGE_HOME_ITEM_COUNT       4U      // 2048, Snake, settings, and info.
#define PAGE_HOME_ROW_Y0          38       // First large menu row top.
#define PAGE_HOME_ROW_STEP        44       // Distance between large menu rows.
#define TEXT_HOME_TITLE           "\xD6\xF7\xD2\xB3"
#define TEXT_GAME                 "2048"
#define TEXT_SNAKE                "SNAKE"
#define TEXT_SETTINGS             "\xC9\xE8\xD6\xC3"
#define TEXT_INFO                 "\xCF\xB5\xCD\xB3\xD0\xC5\xCF\xA2"
#define TEXT_CONFIRM              "\xC8\xB7\xC8\xCF"
#define TEXT_FOOTER_HOME          "\xC9\xCF\xCF\xC2\xD1\xA1\xD4\xF1  \xC8\xB7\xC8\xCF\xBD\xF8\xC8\xEB"

static uint8_t g_home_selected = 0U;
static uint8_t g_home_enter_pending = 0U;
static UI_PageId g_home_pending_page = UI_PAGE_HOME;
static uint32_t g_home_enter_due_ms = 0U;
static UI_FocusAnim g_home_focus_anim;
static uint32_t g_home_draw_now = 0U;

/*
 * Build the repaint rectangle for one home menu row.
 *
 * Parameters:
 * index: Menu item index.
 *
 * Return value:
 * Rectangle covering the full row.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Home_GetRowRect(uint8_t index)
{
    UI_Rect rect;

    rect.x = 12;
    rect.y = (int16_t)(PAGE_HOME_ROW_Y0 + ((int16_t)index * PAGE_HOME_ROW_STEP));
    rect.w = 216;
    rect.h = (int16_t)UI_ROW_H;

    return rect;
}

/*
 * Mark one home menu row dirty.
 *
 * Parameters:
 * index: Menu item index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the row.
 */
static void Page_Home_InvalidateRow(uint8_t index)
{
    UI_Rect rect;

    rect = Page_Home_GetRowRect(index);
    UI_PageInvalidate(&rect);
}

/*
 * Mark a focus animation row range dirty.
 *
 * Parameters:
 * dirty: Row-level rectangle produced by the focus animation task.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues local repaint that can merge with pending row cleanup.
 */
static void Page_Home_InvalidateAnim(const UI_Rect *dirty)
{
    UI_DirtyAdd(dirty);
}

/*
 * Cancel a pending HOME enter action.
 *
 * Navigation keys can arrive while the short pressed feedback is visible. When
 * that happens, the pending page change must be dropped so a later HOME focus
 * position cannot unexpectedly enter the previously selected page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears the pending enter state.
 */
static void Page_Home_CancelEnter(void)
{
    g_home_enter_pending = 0U;
    g_home_pending_page = UI_PAGE_HOME;
    g_home_enter_due_ms = 0U;
}

/*
 * Enter the home page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * None. Selection is retained so BACK does not surprise the user.
 */
static void Page_Home_OnEnter(void)
{
    UI_Rect rect;

    Page_Home_CancelEnter();
    rect = Page_Home_GetRowRect(g_home_selected);
    UI_FocusAnimInit(&g_home_focus_anim, rect.y);
}

/*
 * Start the visible pressed feedback before entering a HOME item.
 *
 * Immediate page switches hide row feedback because the target page requests a
 * full redraw and resets feedback state. This helper records the destination,
 * marks the selected row pressed, and lets Page_Home_Task perform the actual
 * navigation after the configured feedback window expires.
 *
 * Parameters:
 * now: Timestamp of the key event that requested enter.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues selected-row feedback and arms a pending page change.
 */
static void Page_Home_StartEnter(uint32_t now)
{
    UI_Rect rect;

    g_home_pending_page = UI_PAGE_INFO;
    if (g_home_selected == 0U)
    {
        g_home_pending_page = UI_PAGE_GAME;
    }
    if (g_home_selected == 1U)
    {
        g_home_pending_page = UI_PAGE_SNAKE;
    }
    if (g_home_selected == 2U)
    {
        g_home_pending_page = UI_PAGE_SETTINGS;
    }

    rect = Page_Home_GetRowRect(g_home_selected);
    UI_FeedbackPress(&rect, now);
    UI_PageInvalidate(&rect);
    g_home_enter_due_ms = now + UI_FEEDBACK_MS;
    g_home_enter_pending = 1U;
}

/*
 * Handle home page navigation.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates focus or enters another page.
 */
static void Page_Home_OnEvent(const UI_Event *event)
{
    uint8_t old_selected;
    UI_Rect old_rect;
    UI_Rect new_rect;

    if (event->type == UI_EVENT_UP)
    {
        Page_Home_CancelEnter();
        old_selected = g_home_selected;
        old_rect = Page_Home_GetRowRect(old_selected);
        g_home_selected = (g_home_selected == 0U) ?
            (PAGE_HOME_ITEM_COUNT - 1U) :
            (uint8_t)(g_home_selected - 1U);
        new_rect = Page_Home_GetRowRect(g_home_selected);
        UI_FocusAnimStart(&g_home_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Home_InvalidateRow(old_selected);
        Page_Home_InvalidateRow(g_home_selected);
    }
    else if (event->type == UI_EVENT_DOWN)
    {
        Page_Home_CancelEnter();
        old_selected = g_home_selected;
        old_rect = Page_Home_GetRowRect(old_selected);
        g_home_selected++;
        if (g_home_selected >= PAGE_HOME_ITEM_COUNT)
        {
            g_home_selected = 0U;
        }
        new_rect = Page_Home_GetRowRect(g_home_selected);
        UI_FocusAnimStart(&g_home_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Home_InvalidateRow(old_selected);
        Page_Home_InvalidateRow(g_home_selected);
    }
    else if ((event->type == UI_EVENT_OK) || (event->type == UI_EVENT_RIGHT))
    {
        Page_Home_StartEnter(event->timestamp);
    }
    else if (event->type == UI_EVENT_LEFT)
    {
        Page_Home_CancelEnter();
        old_selected = g_home_selected;
        old_rect = Page_Home_GetRowRect(old_selected);
        g_home_selected = 0U;
        new_rect = Page_Home_GetRowRect(g_home_selected);
        UI_FocusAnimStart(&g_home_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Home_InvalidateRow(old_selected);
        Page_Home_InvalidateRow(g_home_selected);
    }
}

/*
 * Run home page animation work.
 *
 * Parameters:
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Advances focus animation and requests marker repaint.
 */
static void Page_Home_Task(uint32_t now)
{
    UI_Rect dirty;

    g_home_draw_now = now;
    if ((g_home_enter_pending != 0U) &&
        ((int32_t)(now - g_home_enter_due_ms) >= 0))
    {
        g_home_enter_pending = 0U;
        UI_PageGoto(g_home_pending_page);
        return;
    }

    if (UI_FocusAnimTask(&g_home_focus_anim, now, &dirty) != 0U)
    {
        Page_Home_InvalidateAnim(&dirty);
    }
}

/*
 * Draw the home page within the requested clip.
 *
 * Parameters:
 * clip: Dirty rectangle currently being repainted.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the ST7789 area intersecting clip.
 */
static void Page_Home_Draw(const UI_Rect *clip)
{
    UI_Rect row;

    (void)clip;

    UI_DrawStatusBar(TEXT_HOME_TITLE, UI_COLOR_ACCENT);
    row = Page_Home_GetRowRect(0U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_GAME, ">", (g_home_selected == 0U) ? 1U : 0U, UI_FeedbackIsActive(&row, g_home_draw_now));
    row = Page_Home_GetRowRect(1U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_SNAKE, ">", (g_home_selected == 1U) ? 1U : 0U, UI_FeedbackIsActive(&row, g_home_draw_now));
    row = Page_Home_GetRowRect(2U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_SETTINGS, ">", (g_home_selected == 2U) ? 1U : 0U, UI_FeedbackIsActive(&row, g_home_draw_now));
    row = Page_Home_GetRowRect(3U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_INFO, ">", (g_home_selected == 3U) ? 1U : 0U, UI_FeedbackIsActive(&row, g_home_draw_now));
    UI_DrawFocusMarker(12, UI_FocusAnimGetY(&g_home_focus_anim), (int16_t)UI_ROW_H, UI_COLOR_ACCENT);
    UI_DrawFooter(TEXT_FOOTER_HOME);
}

const UI_PageOps PAGE_HOME_OPS =
{
    Page_Home_OnEnter,
    Page_Home_OnEvent,
    Page_Home_Task,
    Page_Home_Draw
};

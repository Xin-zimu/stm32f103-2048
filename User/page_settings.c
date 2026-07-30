#include "page_settings.h"
#include "ui_anim.h"
#include "ui_dirty.h"
#include "ui_draw.h"
#include "ui_feedback.h"

#define PAGE_SETTINGS_ITEM_COUNT   3U      // Animation, brightness, and theme.
#define PAGE_SETTINGS_ROW_Y0      46       // First large setting row top.
#define PAGE_SETTINGS_ROW_STEP    54       // Distance between large setting rows.
#define PAGE_SETTINGS_BRIGHT_X   128       // Brightness progress bar left coordinate.
#define PAGE_SETTINGS_BRIGHT_Y   116       // Brightness progress bar top coordinate.
#define PAGE_SETTINGS_BRIGHT_W    78       // Brightness progress bar width.
#define PAGE_SETTINGS_BRIGHT_H    10       // Brightness progress bar height.
#define PAGE_SETTINGS_TOGGLE_X   188       // Animation toggle left coordinate.
#define PAGE_SETTINGS_TOGGLE_Y    63       // Animation toggle top coordinate.
#define PAGE_SETTINGS_TOGGLE_W    30       // Animation toggle width.
#define PAGE_SETTINGS_TOGGLE_H    14       // Animation toggle height.
#define PAGE_SETTINGS_VALUE_X    156       // Right-side value repaint left coordinate.
#define PAGE_SETTINGS_VALUE_W     68       // Right-side value repaint width.
#define PAGE_SETTINGS_VALUE_H     28       // Right-side value repaint height.
#define TEXT_SETTINGS_TITLE       "\xC9\xE8\xD6\xC3"
#define TEXT_ANIMATION            "\xB6\xAF\xBB\xAD"
#define TEXT_BRIGHTNESS           "\xC1\xC1\xB6\xC8"
#define TEXT_THEME                "\xD6\xF7\xCC\xE2"
#define TEXT_FOOTER_SETTINGS      "\xC8\xB7\xC8\xCF\xBD\xF8\xC8\xEB  \xD7\xF3\xBC\xFC\xB7\xB5\xBB\xD8"

static uint8_t g_settings_selected = 0U;
static uint8_t g_settings_animation = 1U;
static uint8_t g_settings_brightness = 3U;
static uint8_t g_settings_theme = 0U;
static UI_FocusAnim g_settings_focus_anim;
static uint32_t g_settings_draw_now = 0U;

/*
 * Build the repaint rectangle for the animation toggle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering the switch and its short feedback frame.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Settings_GetAnimationRect(void)
{
    UI_Rect rect;

    rect.x = (int16_t)(PAGE_SETTINGS_TOGGLE_X - 4);
    rect.y = (int16_t)(PAGE_SETTINGS_TOGGLE_Y - 5);
    rect.w = (int16_t)(PAGE_SETTINGS_TOGGLE_W + 8);
    rect.h = (int16_t)(PAGE_SETTINGS_TOGGLE_H + 10);

    return rect;
}

/*
 * Build the repaint rectangle for the brightness progress bar.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering the bar and its feedback frame.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Settings_GetBrightnessRect(void)
{
    UI_Rect rect;

    rect.x = PAGE_SETTINGS_BRIGHT_X;
    rect.y = PAGE_SETTINGS_BRIGHT_Y;
    rect.w = PAGE_SETTINGS_BRIGHT_W;
    rect.h = PAGE_SETTINGS_BRIGHT_H;

    return rect;
}

/*
 * Build the repaint rectangle for the theme value field.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering the right-side theme text and feedback frame.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Settings_GetThemeRect(void)
{
    UI_Rect rect;

    rect.x = PAGE_SETTINGS_VALUE_X;
    rect.y = (int16_t)(PAGE_SETTINGS_ROW_Y0 + (2 * PAGE_SETTINGS_ROW_STEP) + 10);
    rect.w = PAGE_SETTINGS_VALUE_W;
    rect.h = PAGE_SETTINGS_VALUE_H;

    return rect;
}

/*
 * Build the repaint rectangle for one settings row.
 *
 * Parameters:
 * index: Settings item index.
 *
 * Return value:
 * Rectangle covering the full control row.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Settings_GetRowRect(uint8_t index)
{
    UI_Rect rect;

    rect.x = 12;
    rect.y = (int16_t)(PAGE_SETTINGS_ROW_Y0 + ((int16_t)index * PAGE_SETTINGS_ROW_STEP));
    rect.w = 216;
    rect.h = (int16_t)UI_ROW_H;

    return rect;
}

/*
 * Mark one settings row dirty.
 *
 * Parameters:
 * index: Settings item index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the row.
 */
static void Page_Settings_InvalidateRow(uint8_t index)
{
    UI_Rect rect;

    rect = Page_Settings_GetRowRect(index);
    UI_PageInvalidate(&rect);
}

/*
 * Mark a settings focus animation row range dirty.
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
static void Page_Settings_InvalidateAnim(const UI_Rect *dirty)
{
    UI_DirtyAdd(dirty);
}

/*
 * Enter the settings page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * None. Values are RAM settings retained across page visits.
 */
static void Page_Settings_OnEnter(void)
{
    UI_Rect rect;

    rect = Page_Settings_GetRowRect(g_settings_selected);
    UI_FocusAnimInit(&g_settings_focus_anim, rect.y);
}

/*
 * Change the currently focused setting.
 *
 * Parameters:
 * now: Timestamp of the key event that requested the change.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates one RAM setting and requests the smallest repaint area that reflects
 * the changed value.
 */
static void Page_Settings_ChangeValue(uint32_t now)
{
    UI_Rect rect;

    if (g_settings_selected == 0U)
    {
        g_settings_animation ^= 1U;
        rect = Page_Settings_GetAnimationRect();
        UI_FeedbackPress(&rect, now);
        UI_PageInvalidate(&rect);
        return;
    }
    else if (g_settings_selected == 1U)
    {
        g_settings_brightness++;
        if (g_settings_brightness > 5U)
        {
            g_settings_brightness = 1U;
        }
        rect = Page_Settings_GetBrightnessRect();
        UI_FeedbackPress(&rect, now);
        UI_PageInvalidate(&rect);
        return;
    }
    else
    {
        g_settings_theme++;
        if (g_settings_theme > 2U)
        {
            g_settings_theme = 0U;
        }
        rect = Page_Settings_GetThemeRect();
        UI_FeedbackPress(&rect, now);
        UI_PageInvalidate(&rect);
        return;
    }
}

/*
 * Handle settings page events.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates focus, settings, or active page.
 */
static void Page_Settings_OnEvent(const UI_Event *event)
{
    uint8_t old_selected;
    UI_Rect old_rect;
    UI_Rect new_rect;

    if (event->type == UI_EVENT_UP)
    {
        old_selected = g_settings_selected;
        old_rect = Page_Settings_GetRowRect(old_selected);
        g_settings_selected = (g_settings_selected == 0U) ?
            (PAGE_SETTINGS_ITEM_COUNT - 1U) :
            (uint8_t)(g_settings_selected - 1U);
        new_rect = Page_Settings_GetRowRect(g_settings_selected);
        UI_FocusAnimStart(&g_settings_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Settings_InvalidateRow(old_selected);
        Page_Settings_InvalidateRow(g_settings_selected);
    }
    else if (event->type == UI_EVENT_DOWN)
    {
        old_selected = g_settings_selected;
        old_rect = Page_Settings_GetRowRect(old_selected);
        g_settings_selected++;
        if (g_settings_selected >= PAGE_SETTINGS_ITEM_COUNT)
        {
            g_settings_selected = 0U;
        }
        new_rect = Page_Settings_GetRowRect(g_settings_selected);
        UI_FocusAnimStart(&g_settings_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Settings_InvalidateRow(old_selected);
        Page_Settings_InvalidateRow(g_settings_selected);
    }
    else if (event->type == UI_EVENT_LEFT)
    {
        UI_PageBack();
    }
    else if ((event->type == UI_EVENT_RIGHT) || (event->type == UI_EVENT_OK))
    {
        Page_Settings_ChangeValue(event->timestamp);
    }
}

/*
 * Run settings page animation work.
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
static void Page_Settings_Task(uint32_t now)
{
    UI_Rect dirty;

    g_settings_draw_now = now;
    if (UI_FocusAnimTask(&g_settings_focus_anim, now, &dirty) != 0U)
    {
        Page_Settings_InvalidateAnim(&dirty);
    }
}

/*
 * Draw the settings page within the requested clip.
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
static void Page_Settings_Draw(const UI_Rect *clip)
{
    static const char * const theme_names[3] = {"CYAN", "GOLD", "GREEN"};
    UI_Rect rect;
    UI_Rect row;

    (void)clip;

    UI_DrawStatusBar(TEXT_SETTINGS_TITLE, UI_COLOR_WARN);
    row = Page_Settings_GetRowRect(0U);
    UI_DrawMenuRowCNEx(
        12,
        row.y,
        216,
        TEXT_ANIMATION,
        0,
        (g_settings_selected == 0U) ? 1U : 0U,
        UI_FeedbackIsActive(&row, g_settings_draw_now)
    );
    row = Page_Settings_GetRowRect(1U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_BRIGHTNESS, 0, (g_settings_selected == 1U) ? 1U : 0U, UI_FeedbackIsActive(&row, g_settings_draw_now));
    rect = Page_Settings_GetBrightnessRect();
    UI_DrawProgressBar(rect.x, rect.y, rect.w, g_settings_brightness, 5U);
    if (UI_FeedbackIsActive(&rect, g_settings_draw_now) != 0U)
    {
        UI_DrawFrame(rect.x, rect.y, rect.w, rect.h, UI_COLOR_WARN);
    }
    row = Page_Settings_GetRowRect(2U);
    UI_DrawMenuRowCNEx(
        12,
        row.y,
        216,
        TEXT_THEME,
        theme_names[g_settings_theme],
        (g_settings_selected == 2U) ? 1U : 0U,
        UI_FeedbackIsActive(&row, g_settings_draw_now)
    );
    rect = Page_Settings_GetAnimationRect();
    UI_DrawToggle(PAGE_SETTINGS_TOGGLE_X, PAGE_SETTINGS_TOGGLE_Y, g_settings_animation);
    if (UI_FeedbackIsActive(&rect, g_settings_draw_now) != 0U)
    {
        UI_DrawFrame(rect.x, rect.y, rect.w, rect.h, UI_COLOR_WARN);
    }
    rect = Page_Settings_GetThemeRect();
    if (UI_FeedbackIsActive(&rect, g_settings_draw_now) != 0U)
    {
        UI_DrawFrame(rect.x, rect.y, rect.w, rect.h, UI_COLOR_WARN);
    }
    UI_DrawFocusMarker(12, UI_FocusAnimGetY(&g_settings_focus_anim), (int16_t)UI_ROW_H, UI_COLOR_ACCENT);
    UI_DrawFooter(TEXT_FOOTER_SETTINGS);
}

const UI_PageOps PAGE_SETTINGS_OPS =
{
    Page_Settings_OnEnter,
    Page_Settings_OnEvent,
    Page_Settings_Task,
    Page_Settings_Draw
};

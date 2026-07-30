#include "page_pause.h"
#include "game_2048.h"
#include "page_game.h"
#include "ui_anim.h"
#include "ui_dirty.h"
#include "ui_draw.h"

#define PAGE_PAUSE_ITEM_COUNT      4U      // Resume, restart, goal, and home.
#define PAGE_PAUSE_ROW_Y0         38       // First pause menu row top.
#define PAGE_PAUSE_ROW_STEP       44       // Distance between pause menu rows.
#define TEXT_PAUSE_TITLE        "PAUSE"
#define TEXT_PAUSE_RESUME       "RESUME"
#define TEXT_PAUSE_RESTART      "NEW GAME"
#define TEXT_PAUSE_GOAL         "GOAL"
#define TEXT_PAUSE_HOME         "HOME"
#define TEXT_PAUSE_FOOTER       "OK SELECT  LEFT RESUME"

static uint8_t g_pause_selected = 0U;
static UI_FocusAnim g_pause_focus_anim;

/*
 * Build the repaint rectangle for one pause menu row.
 *
 * Parameters:
 * index: Pause menu item index.
 *
 * Return value:
 * Rectangle covering the full row.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Pause_GetRowRect(uint8_t index)
{
    UI_Rect rect;

    rect.x = 12;
    rect.y = (int16_t)(PAGE_PAUSE_ROW_Y0 + ((int16_t)index * PAGE_PAUSE_ROW_STEP));
    rect.w = 216;
    rect.h = (int16_t)UI_ROW_H;

    return rect;
}

/*
 * Mark one pause menu row dirty.
 *
 * Parameters:
 * index: Pause menu item index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the row.
 */
static void Page_Pause_InvalidateRow(uint8_t index)
{
    UI_Rect rect;

    rect = Page_Pause_GetRowRect(index);
    UI_PageInvalidate(&rect);
}

/*
 * Mark a pause focus animation range dirty.
 *
 * Parameters:
 * dirty: Row-level rectangle produced by the focus animation task.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues local repaint for the moving focus marker.
 */
static void Page_Pause_InvalidateAnim(const UI_Rect *dirty)
{
    UI_DirtyAdd(dirty);
}

/*
 * Convert the active game goal into a compact menu value.
 *
 * The pause menu exposes a fixed goal cycle, so the value is returned from a
 * small constant table instead of formatting numbers at runtime.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Null-terminated ASCII goal value.
 *
 * Side effects:
 * None.
 */
static const char *Page_Pause_GetGoalText(void)
{
    static const char * const goal_texts[5] =
    {
        "128",
        "256",
        "512",
        "1024",
        "2048"
    };
    uint8_t goal_exp;

    goal_exp = Game2048_GetGoalExp();
    if (goal_exp <= 7U)
    {
        return goal_texts[0];
    }
    if (goal_exp >= 11U)
    {
        return goal_texts[4];
    }

    return goal_texts[goal_exp - 7U];
}

/*
 * Cycle the active game goal to the next supported tile.
 *
 * Goal values step through 128, 256, 512, 1024, and 2048. The current board is
 * re-evaluated by the game module, but the pause page stays open so the player
 * can decide whether to resume or start a new board.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the game goal and queues the GOAL row for repaint.
 */
static void Page_Pause_CycleGoal(void)
{
    uint8_t goal_exp;

    goal_exp = Game2048_GetGoalExp();
    goal_exp++;
    if (goal_exp > 11U)
    {
        goal_exp = 7U;
    }
    Game2048_SetGoalExp(goal_exp);
    Page_Pause_InvalidateRow(2U);
}

/*
 * Enter the pause page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Initializes the focus marker at the retained menu selection.
 */
static void Page_Pause_OnEnter(void)
{
    UI_Rect rect;

    rect = Page_Pause_GetRowRect(g_pause_selected);
    UI_FocusAnimInit(&g_pause_focus_anim, rect.y);
}

/*
 * Execute the currently selected pause command.
 *
 * Resume returns to the live GAME page without changing the board. Restart
 * resets the 2048 board through the GAME page helper before returning. Goal
 * cycles the target tile while staying paused. Home uses the normal page-home
 * path.
 *
 * Parameters:
 * now: Timestamp used as the restart RNG seed when needed.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May switch pages and may reset the game board.
 */
static void Page_Pause_Activate(uint32_t now)
{
    if (g_pause_selected == 0U)
    {
        UI_PageGoto(UI_PAGE_GAME);
        return;
    }
    if (g_pause_selected == 1U)
    {
        Page_Game_RestartFromMenu(now);
        UI_PageGoto(UI_PAGE_GAME);
        return;
    }
    if (g_pause_selected == 2U)
    {
        Page_Pause_CycleGoal();
        return;
    }

    UI_PageHome();
}

/*
 * Handle pause page navigation.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates menu focus or executes a pause command.
 */
static void Page_Pause_OnEvent(const UI_Event *event)
{
    uint8_t old_selected;
    UI_Rect old_rect;
    UI_Rect new_rect;

    if (event == 0)
    {
        return;
    }

    if (event->type == UI_EVENT_UP)
    {
        old_selected = g_pause_selected;
        old_rect = Page_Pause_GetRowRect(old_selected);
        g_pause_selected = (g_pause_selected == 0U) ?
            (PAGE_PAUSE_ITEM_COUNT - 1U) :
            (uint8_t)(g_pause_selected - 1U);
        new_rect = Page_Pause_GetRowRect(g_pause_selected);
        UI_FocusAnimStart(&g_pause_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Pause_InvalidateRow(old_selected);
        Page_Pause_InvalidateRow(g_pause_selected);
    }
    else if (event->type == UI_EVENT_DOWN)
    {
        old_selected = g_pause_selected;
        old_rect = Page_Pause_GetRowRect(old_selected);
        g_pause_selected++;
        if (g_pause_selected >= PAGE_PAUSE_ITEM_COUNT)
        {
            g_pause_selected = 0U;
        }
        new_rect = Page_Pause_GetRowRect(g_pause_selected);
        UI_FocusAnimStart(&g_pause_focus_anim, old_rect.y, new_rect.y, event->timestamp);
        Page_Pause_InvalidateRow(old_selected);
        Page_Pause_InvalidateRow(g_pause_selected);
    }
    else if (event->type == UI_EVENT_LEFT)
    {
        UI_PageGoto(UI_PAGE_GAME);
    }
    else if ((event->type == UI_EVENT_OK) || (event->type == UI_EVENT_RIGHT))
    {
        Page_Pause_Activate(event->timestamp);
    }
}

/*
 * Run pause page animation work.
 *
 * Parameters:
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Advances the focus animation and queues local repaint.
 */
static void Page_Pause_Task(uint32_t now)
{
    UI_Rect dirty;

    if (UI_FocusAnimTask(&g_pause_focus_anim, now, &dirty) != 0U)
    {
        Page_Pause_InvalidateAnim(&dirty);
    }
}

/*
 * Draw the pause page within the requested clip.
 *
 * Parameters:
 * clip: Dirty rectangle currently being repainted.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the pause menu.
 */
static void Page_Pause_Draw(const UI_Rect *clip)
{
    UI_Rect row;

    (void)clip;

    UI_DrawStatusBar(TEXT_PAUSE_TITLE, UI_COLOR_WARN);
    row = Page_Pause_GetRowRect(0U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_PAUSE_RESUME, ">", (g_pause_selected == 0U) ? 1U : 0U, 0U);
    row = Page_Pause_GetRowRect(1U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_PAUSE_RESTART, ">", (g_pause_selected == 1U) ? 1U : 0U, 0U);
    row = Page_Pause_GetRowRect(2U);
    UI_DrawMenuRowCNEx(
        12,
        row.y,
        216,
        TEXT_PAUSE_GOAL,
        Page_Pause_GetGoalText(),
        (g_pause_selected == 2U) ? 1U : 0U,
        0U
    );
    row = Page_Pause_GetRowRect(3U);
    UI_DrawMenuRowCNEx(12, row.y, 216, TEXT_PAUSE_HOME, ">", (g_pause_selected == 3U) ? 1U : 0U, 0U);
    UI_DrawFocusMarker(12, UI_FocusAnimGetY(&g_pause_focus_anim), (int16_t)UI_ROW_H, UI_COLOR_ACCENT);
    UI_DrawFooter(TEXT_PAUSE_FOOTER);
}

const UI_PageOps PAGE_PAUSE_OPS =
{
    Page_Pause_OnEnter,
    Page_Pause_OnEvent,
    Page_Pause_Task,
    Page_Pause_Draw
};

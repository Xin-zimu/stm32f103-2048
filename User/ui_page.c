#include "ui_page.h"
#include "page_home.h"
#include "page_info.h"
#include "page_game.h"
#include "page_settings.h"
#include "ui_dirty.h"
#include "ui_draw.h"
#include "ui_feedback.h"
#include "ui_renderer.h"

static const UI_PageOps * const UI_PAGES[UI_PAGE_COUNT] =
{
    &PAGE_HOME_OPS,
    &PAGE_GAME_OPS,
    &PAGE_SETTINGS_OPS,
    &PAGE_INFO_OPS
};

static UI_PageId g_ui_current_page = UI_PAGE_HOME;

/*
 * Initialize page routing and enter the home page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets the current page and requests the first redraw.
 */
void UI_PageInit(void)
{
    UI_DirtyInit();
    UI_FeedbackInit();
    UI_RendererInit();
    g_ui_current_page = UI_PAGE_HOME;
    if (UI_PAGES[g_ui_current_page]->on_enter != 0)
    {
        UI_PAGES[g_ui_current_page]->on_enter();
    }
    UI_PageRequestRedraw();
}

/*
 * Request a full redraw of the active page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Marks the active page dirty.
 */
void UI_PageRequestRedraw(void)
{
    UI_DirtyFullScreen();
}

/*
 * Request a local redraw of the active page.
 *
 * Parameters:
 * rect: Page-space rectangle that needs repainting.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Adds one clipped rectangle to the dirty list.
 */
void UI_PageInvalidate(const UI_Rect *rect)
{
    UI_DirtyAdd(rect);
}

/*
 * Request a local redraw from raw coordinates.
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
 * Adds one clipped rectangle to the dirty list.
 */
void UI_PageInvalidateXYWH(int16_t x, int16_t y, int16_t w, int16_t h)
{
    UI_DirtyAddXYWH(x, y, w, h);
}

/*
 * Switch to a new page.
 *
 * Parameters:
 * page: Page identifier to activate.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates current page state and requests redraw.
 */
void UI_PageGoto(UI_PageId page)
{
    if (page >= UI_PAGE_COUNT)
    {
        return;
    }

    g_ui_current_page = page;
    UI_FeedbackInit();
    if (UI_PAGES[g_ui_current_page]->on_enter != 0)
    {
        UI_PAGES[g_ui_current_page]->on_enter();
    }
    UI_PageRequestRedraw();
}

/*
 * Return to the home page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Activates HOME and requests redraw.
 */
void UI_PageHome(void)
{
    UI_PageGoto(UI_PAGE_HOME);
}

/*
 * Handle a page back action.
 *
 * The current stage uses shallow navigation: every child page returns to HOME.
 * HOME consumes BACK by staying visible.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May activate HOME and request redraw.
 */
void UI_PageBack(void)
{
    if (g_ui_current_page != UI_PAGE_HOME)
    {
        UI_PageHome();
    }
    else
    {
        UI_PageRequestRedraw();
    }
}

/*
 * Read the current active page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Active page identifier.
 *
 * Side effects:
 * None.
 */
UI_PageId UI_PageGetCurrent(void)
{
    return g_ui_current_page;
}

/*
 * Dispatch one UI event with global priority.
 *
 * SYSTEM_RESET, HOME, SETTINGS, and BACK are handled before page-local event
 * handlers so those commands remain consistent across all pages.
 *
 * Parameters:
 * event: Logical event to process.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May change the active page, request redraw, or reset the MCU.
 */
void UI_PageDispatchEvent(const UI_Event *event)
{
    if (event == 0)
    {
        return;
    }

    switch (event->type)
    {
        case UI_EVENT_SYSTEM_RESET:
            NVIC_SystemReset();
            break;

        case UI_EVENT_HOME:
            UI_PageHome();
            break;

        case UI_EVENT_SETTINGS:
            UI_PageGoto(UI_PAGE_SETTINGS);
            break;

        case UI_EVENT_BACK:
            UI_PageBack();
            break;

        default:
            if (UI_PAGES[g_ui_current_page]->on_event != 0)
            {
                UI_PAGES[g_ui_current_page]->on_event(event);
            }
            break;
    }
}

/*
 * Run page redraw work.
 *
 * Rendering runs before page-local animation updates. While the strip renderer
 * is still draining a dirty area, focus animation is held at its current
 * position so separate horizontal strips are not drawn from different animation
 * frames.
 *
 * Parameters:
 * now: Current Timing_GetTick timestamp.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the active page when dirty.
 */
void UI_PageTask(uint32_t now)
{
    const UI_PageOps *page;

    page = UI_PAGES[g_ui_current_page];
    UI_FeedbackTask(now);
    UI_RendererTask(now, page);
    if (UI_RendererIsBusy() != 0U)
    {
        return;
    }

    if (page->task != 0)
    {
        page->task(now);
    }
}

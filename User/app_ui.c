#include "app_ui.h"
#include "ui_event.h"
#include "ui_page.h"

#define UI_MAX_EVENTS_PER_TASK     4U      // Events handled per main-loop pass.

/*
 * Initialize the lightweight UI application.
 *
 * This module only connects the event queue to the page manager. Page layout,
 * rendering, and page-local state live in page-specific modules so the UI can
 * grow without turning this file into a mixed controller.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears queued UI events and draws the initial page on the next page task.
 */
void App_UI_Init(void)
{
    UI_EventQueueInit();
    UI_PageInit();
}

/*
 * Service queued UI events and the active page.
 *
 * The loop processes a bounded number of events so repeated direction input
 * cannot starve display work. Rendering remains full-screen in this stage; a
 * later dirty-rectangle stage can replace the page redraw implementation
 * without changing key scanning or page event dispatch.
 *
 * Parameters:
 * now: Current Timing_GetTick timestamp.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Dispatches UI input and redraws dirty pages.
 */
void App_UI_Task(uint32_t now)
{
    uint8_t processed;
    UI_Event event;

    for (processed = 0U; processed < UI_MAX_EVENTS_PER_TASK; processed++)
    {
        if (UI_EventPop(&event) == 0U)
        {
            break;
        }
        UI_PageDispatchEvent(&event);
    }

    UI_PageTask(now);
}

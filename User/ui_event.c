#include "ui_event.h"

typedef struct
{
    UI_Event items[UI_EVENT_QUEUE_SIZE];   // Static queue storage.
    uint8_t head;                          // Next item to pop.
    uint8_t tail;                          // Next slot to push.
    uint8_t count;                         // Current queued item count.
} UI_EventQueue;

static UI_EventQueue g_ui_event_queue;
volatile uint32_t ui_event_drop_count = 0U;

/*
 * Classify whether an event must be preserved when the queue is full.
 *
 * Page escape and reset events are more important than repeated direction
 * movement. When bursts occur, these events are allowed to replace lower
 * priority queued input so the UI remains recoverable.
 *
 * Parameters:
 * type: Logical UI event type.
 *
 * Return value:
 * 1: Event is high priority.
 * 0: Event may be dropped under pressure.
 *
 * Side effects:
 * None.
 */
static uint8_t UI_EventIsHighPriority(UI_EventType type)
{
    return ((type == UI_EVENT_BACK) ||
            (type == UI_EVENT_HOME) ||
            (type == UI_EVENT_SETTINGS) ||
            (type == UI_EVENT_SYSTEM_RESET)) ? 1U : 0U;
}

/*
 * Classify whether a queued event is replaceable under pressure.
 *
 * Key repeat events are the first replacement target because losing one repeat
 * step is less harmful than dropping a navigation escape. Direction press
 * events are the fallback replacement target for high-priority commands.
 *
 * Parameters:
 * event: Queued event to inspect.
 * allow_direction_press: Nonzero to allow replacing normal direction events.
 *
 * Return value:
 * 1: Event can be overwritten.
 * 0: Event should be retained.
 *
 * Side effects:
 * None.
 */
static uint8_t UI_EventCanReplace(const UI_Event *event, uint8_t allow_direction_press)
{
    if (event->source_type == KEY_EVENT_REPEAT)
    {
        return 1U;
    }

    if (allow_direction_press == 0U)
    {
        return 0U;
    }

    return ((event->type == UI_EVENT_UP) ||
            (event->type == UI_EVENT_DOWN) ||
            (event->type == UI_EVENT_LEFT) ||
            (event->type == UI_EVENT_RIGHT) ||
            (event->type == UI_EVENT_OK)) ? 1U : 0U;
}

/*
 * Try to replace a lower-priority queued event.
 *
 * The ring queue keeps chronological order for normal operation. This helper
 * only overwrites an existing slot when the queue is already full, avoiding a
 * larger shifting implementation and keeping RAM usage fixed.
 *
 * Parameters:
 * event: New event to preserve.
 *
 * Return value:
 * 1: A queued event was replaced.
 * 0: No suitable replacement was found.
 *
 * Side effects:
 * May overwrite one queued event.
 */
static uint8_t UI_EventReplaceLowerPriority(const UI_Event *event)
{
    uint8_t index;
    uint8_t slot;
    uint8_t allow_direction_press;

    allow_direction_press = UI_EventIsHighPriority(event->type);
    for (index = 0U; index < g_ui_event_queue.count; index++)
    {
        slot = (uint8_t)(g_ui_event_queue.head + index);
        if (slot >= UI_EVENT_QUEUE_SIZE)
        {
            slot = (uint8_t)(slot - UI_EVENT_QUEUE_SIZE);
        }

        if (UI_EventCanReplace(&g_ui_event_queue.items[slot], allow_direction_press) != 0U)
        {
            g_ui_event_queue.items[slot] = *event;
            return 1U;
        }
    }

    return 0U;
}

/*
 * Reset the fixed UI event queue.
 *
 * The queue is used by the cooperative key and UI tasks, so it has no dynamic
 * allocation and no blocking behavior. Existing queued input is intentionally
 * discarded during application initialization or when the UI requests a clear.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears all queued UI events.
 */
void UI_EventQueueInit(void)
{
    g_ui_event_queue.head = 0U;
    g_ui_event_queue.tail = 0U;
    g_ui_event_queue.count = 0U;
    ui_event_drop_count = 0U;
}

/*
 * Push one logical UI event into the ring queue.
 *
 * When the queue is full, a repeat event or lower-priority direction event can
 * be replaced so BACK, HOME, SETTINGS, and reset are not lost during bursts.
 * If no replacement target exists the new event is dropped and counted.
 *
 * Parameters:
 * event: Logical event to enqueue.
 *
 * Return value:
 * 1: Event was queued.
 * 0: Event was rejected because the queue was full.
 *
 * Side effects:
 * Increments ui_event_drop_count when the queue is full.
 */
uint8_t UI_EventPush(const UI_Event *event)
{
    if ((event == 0) || (event->type == UI_EVENT_NONE))
    {
        return 0U;
    }

    if (g_ui_event_queue.count >= UI_EVENT_QUEUE_SIZE)
    {
        if (UI_EventReplaceLowerPriority(event) != 0U)
        {
            return 1U;
        }
        ui_event_drop_count++;
        return 0U;
    }

    g_ui_event_queue.items[g_ui_event_queue.tail] = *event;
    g_ui_event_queue.tail++;
    if (g_ui_event_queue.tail >= UI_EVENT_QUEUE_SIZE)
    {
        g_ui_event_queue.tail = 0U;
    }
    g_ui_event_queue.count++;

    return 1U;
}

/*
 * Pop one logical UI event from the ring queue.
 *
 * The UI task calls this function a bounded number of times per main-loop
 * pass, preventing a burst of key repeat events from starving display work.
 *
 * Parameters:
 * event: Receives the oldest queued event.
 *
 * Return value:
 * 1: An event was popped.
 * 0: The queue was empty or the output pointer was invalid.
 *
 * Side effects:
 * Removes one queued event when available.
 */
uint8_t UI_EventPop(UI_Event *event)
{
    if ((event == 0) || (g_ui_event_queue.count == 0U))
    {
        return 0U;
    }

    *event = g_ui_event_queue.items[g_ui_event_queue.head];
    g_ui_event_queue.head++;
    if (g_ui_event_queue.head >= UI_EVENT_QUEUE_SIZE)
    {
        g_ui_event_queue.head = 0U;
    }
    g_ui_event_queue.count--;

    return 1U;
}

/*
 * Convert one debounced physical key event into a logical UI event.
 *
 * Mapping is centralized here so page code never reads GPIO directly. Press
 * and repeat events drive normal navigation, a 600 ms RST long press becomes
 * HOME, and the two second RST hold becomes SYSTEM_RESET for the global UI
 * handler.
 *
 * Parameters:
 * event: Debounced physical key event from key_driver.c.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May enqueue one logical UI event.
 */
void UI_EventPushFromKey(const KeyEvent *event)
{
    UI_Event ui_event;

    if (event == 0)
    {
        return;
    }

    ui_event.type = UI_EVENT_NONE;
    ui_event.source_key = event->key;
    ui_event.source_type = event->type;
    ui_event.timestamp = event->timestamp;

    if (event->type == KEY_EVENT_SYSTEM_RESET)
    {
        ui_event.type = UI_EVENT_SYSTEM_RESET;
    }
    else if ((event->key == KEY_ID_RST) && (event->type == KEY_EVENT_LONG_PRESS))
    {
        ui_event.type = UI_EVENT_HOME;
    }
    else if ((event->type == KEY_EVENT_PRESS) || (event->type == KEY_EVENT_REPEAT))
    {
        switch (event->key)
        {
            case KEY_ID_UP:
                ui_event.type = UI_EVENT_UP;
                break;

            case KEY_ID_DOWN:
                ui_event.type = UI_EVENT_DOWN;
                break;

            case KEY_ID_LEFT:
                ui_event.type = UI_EVENT_LEFT;
                break;

            case KEY_ID_RIGHT:
                ui_event.type = UI_EVENT_RIGHT;
                break;

            case KEY_ID_MID:
                ui_event.type = UI_EVENT_OK;
                break;

            case KEY_ID_SET:
                ui_event.type = UI_EVENT_SETTINGS;
                break;

            case KEY_ID_RST:
                ui_event.type = UI_EVENT_BACK;
                break;

            default:
                break;
        }
    }

    if (ui_event.type != UI_EVENT_NONE)
    {
        (void)UI_EventPush(&ui_event);
    }
}

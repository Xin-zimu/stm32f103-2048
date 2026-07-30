#ifndef __UI_EVENT_H
#define __UI_EVENT_H

#include "app_config.h"
#include "key_driver.h"
#include "stm32f10x.h"

#define UI_EVENT_QUEUE_SIZE       APP_UI_EVENT_QUEUE_SIZE       // Fixed ring queue capacity.

typedef enum
{
    UI_EVENT_NONE = 0,                     // No UI event.
    UI_EVENT_UP,                           // Move focus up.
    UI_EVENT_DOWN,                         // Move focus down.
    UI_EVENT_LEFT,                         // Move left or back.
    UI_EVENT_RIGHT,                        // Move right or enter.
    UI_EVENT_OK,                           // Confirm current selection.
    UI_EVENT_SETTINGS,                     // Open settings page.
    UI_EVENT_BACK,                         // Return to the previous page.
    UI_EVENT_HOME,                         // Return to the home page.
    UI_EVENT_SYSTEM_RESET                  // Request NVIC_SystemReset.
} UI_EventType;

typedef struct
{
    UI_EventType type;                     // Logical UI event.
    KeyId source_key;                      // Physical key that generated it.
    KeyEventType source_type;              // Physical key event kind.
    uint32_t timestamp;                    // Timing_GetTick timestamp.
} UI_Event;

extern volatile uint32_t ui_event_drop_count;

void UI_EventQueueInit(void);
uint8_t UI_EventPush(const UI_Event *event);
uint8_t UI_EventPop(UI_Event *event);
void UI_EventPushFromKey(const KeyEvent *event);

#endif

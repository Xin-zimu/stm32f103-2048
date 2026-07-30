#ifndef __KEY_DRIVER_H
#define __KEY_DRIVER_H

#include "app_config.h"
#include "stm32f10x.h"

#define KEY_SCAN_INTERVAL_MS      APP_KEY_SCAN_INTERVAL_MS      // Key scan period.
#define KEY_DEBOUNCE_MS           APP_KEY_DEBOUNCE_MS           // Stable time before changes.
#define KEY_LONG_PRESS_MS         APP_KEY_LONG_PRESS_MS         // Long press threshold.
#define KEY_REPEAT_DELAY_MS       APP_KEY_REPEAT_DELAY_MS       // First direction repeat delay.
#define KEY_REPEAT_INTERVAL_MS    APP_KEY_REPEAT_INTERVAL_MS    // Direction repeat interval.
#define KEY_RST_RESET_MS          APP_KEY_RST_RESET_MS          // RST hold time before reset.

typedef enum
{
    KEY_ID_UP = 0,                         // Joystick UP on PA0.
    KEY_ID_DOWN,                           // Joystick DWN on PA1.
    KEY_ID_LEFT,                           // Joystick LFT on PA2.
    KEY_ID_RIGHT,                          // Joystick RHT on PA3.
    KEY_ID_MID,                            // Joystick MID on PA4.
    KEY_ID_SET,                            // Joystick SET on PA5.
    KEY_ID_RST,                            // Joystick RST on PA6.
    KEY_ID_COUNT                           // Number of physical keys.
} KeyId;

typedef enum
{
    KEY_EVENT_NONE = 0,                    // No key event.
    KEY_EVENT_PRESS,                       // Stable transition to pressed.
    KEY_EVENT_RELEASE,                     // Stable transition to released.
    KEY_EVENT_LONG_PRESS,                  // Pressed longer than KEY_LONG_PRESS_MS.
    KEY_EVENT_REPEAT,                      // Direction-key repeat while held.
    KEY_EVENT_SYSTEM_RESET                 // RST held long enough for software reset.
} KeyEventType;

typedef struct
{
    KeyId key;                             // Physical key that generated the event.
    KeyEventType type;                     // Event kind.
    uint32_t timestamp;                    // Timing_GetTick timestamp.
} KeyEvent;

void Key_Init(void);
void Key_Task(uint32_t now);

#endif

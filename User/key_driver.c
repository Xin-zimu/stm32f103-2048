#include "key_driver.h"
#include "ui_event.h"

#define KEY_GPIO_PORT             GPIOA
#define KEY_GPIO_CLK              RCC_APB2Periph_GPIOA
#define KEY_ACTIVE_STATE          Bit_RESET

typedef struct
{
    uint16_t pin;                          // GPIO pin used by this key.
    BitAction raw_state;                   // Last sampled electrical level.
    BitAction stable_state;                // Debounced electrical level.
    uint32_t raw_change_time;              // Tick when raw_state last changed.
    uint32_t press_time;                   // Tick when stable press began.
    uint32_t next_repeat_time;             // Next repeat deadline.
    uint8_t long_press_sent;               // LONG_PRESS already emitted.
    uint8_t reset_sent;                    // SYSTEM_RESET already emitted.
    uint8_t repeat_enabled;                // Direction keys repeat when held.
} KeyState;

static KeyState g_key_states[KEY_ID_COUNT];
static uint32_t g_key_last_scan_time = 0U;

/*
 * Read one active-low joystick input level directly from GPIOA.
 *
 * The standard peripheral helper adds code size for a very small operation.
 * This project is close to the STM32F103C8 64 KB Flash limit, so key scanning
 * reads IDR directly while keeping GPIO initialization in the library API.
 *
 * Parameters:
 * pin: GPIOA pin mask for the key being sampled.
 *
 * Return value:
 * Bit_SET when the input is high, Bit_RESET when the input is low.
 *
 * Side effects:
 * None.
 */
static BitAction Key_ReadPin(uint16_t pin)
{
    return ((KEY_GPIO_PORT->IDR & pin) != 0U) ? Bit_SET : Bit_RESET;
}

/*
 * Emit one debounced key event into the UI event mapper.
 *
 * Key hardware state remains private to this module. The mapper receives only
 * stable semantic events and is responsible for translating physical keys to
 * page-level actions.
 *
 * Parameters:
 * key: Physical key identifier.
 * type: Debounced key event type.
 * now: Current Timing_GetTick timestamp.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May enqueue one UI event.
 */
static void Key_EmitEvent(KeyId key, KeyEventType type, uint32_t now)
{
    KeyEvent event;

    event.key = key;
    event.type = type;
    event.timestamp = now;
    UI_EventPushFromKey(&event);
}

/*
 * Initialize one key state descriptor.
 *
 * The joystick module uses common-ground contacts and GPIO input pull-ups, so
 * released reads as Bit_SET and pressed reads as Bit_RESET. Each descriptor is
 * seeded from the current GPIO level to avoid false press events during boot.
 *
 * Parameters:
 * key: Physical key identifier.
 * pin: GPIOA pin connected to the key.
 * repeat_enabled: Nonzero when the key should generate repeat events.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Initializes one entry in g_key_states.
 */
static void Key_InitOne(KeyId key, uint16_t pin, uint8_t repeat_enabled)
{
    BitAction state;

    state = Key_ReadPin(pin);
    g_key_states[key].pin = pin;
    g_key_states[key].raw_state = state;
    g_key_states[key].stable_state = state;
    g_key_states[key].raw_change_time = 0U;
    g_key_states[key].press_time = 0U;
    g_key_states[key].next_repeat_time = 0U;
    g_key_states[key].long_press_sent = 0U;
    g_key_states[key].reset_sent = 0U;
    g_key_states[key].repeat_enabled = repeat_enabled;
}

/*
 * Configure PA0 through PA6 as common-ground joystick inputs.
 *
 * The pins use internal pull-ups and no EXTI line. Pressing a key connects the
 * corresponding GPIO to COM/GND, so active level is Bit_RESET. PA13 and PA14
 * are not touched, preserving SWD download and debug access.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Enables GPIOA clock and configures PA0..PA6 as input pull-up.
 */
void Key_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(KEY_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                    GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(KEY_GPIO_PORT, &gpio);

    Key_InitOne(KEY_ID_UP, GPIO_Pin_0, 1U);
    Key_InitOne(KEY_ID_DOWN, GPIO_Pin_1, 1U);
    Key_InitOne(KEY_ID_LEFT, GPIO_Pin_2, 1U);
    Key_InitOne(KEY_ID_RIGHT, GPIO_Pin_3, 1U);
    Key_InitOne(KEY_ID_MID, GPIO_Pin_4, 0U);
    Key_InitOne(KEY_ID_SET, GPIO_Pin_5, 0U);
    Key_InitOne(KEY_ID_RST, GPIO_Pin_6, 0U);
}

/*
 * Update one debounced key state from its current GPIO level.
 *
 * Raw changes must remain stable for KEY_DEBOUNCE_MS before the stable state
 * changes. Direction keys emit repeat events after the configured delay. RST
 * emits a normal BACK on press, HOME after KEY_LONG_PRESS_MS, and SYSTEM_RESET
 * after KEY_RST_RESET_MS while still held.
 *
 * Parameters:
 * key: Physical key identifier.
 * now: Current Timing_GetTick timestamp.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May enqueue key-derived UI events.
 */
static void Key_UpdateOne(KeyId key, uint32_t now)
{
    KeyState *state;
    BitAction raw;

    state = &g_key_states[key];
    raw = Key_ReadPin(state->pin);

    if (raw != state->raw_state)
    {
        state->raw_state = raw;
        state->raw_change_time = now;
    }

    if ((raw != state->stable_state) &&
        ((uint32_t)(now - state->raw_change_time) >= KEY_DEBOUNCE_MS))
    {
        state->stable_state = raw;
        if (raw == KEY_ACTIVE_STATE)
        {
            state->press_time = now;
            state->next_repeat_time = (uint32_t)(now + KEY_REPEAT_DELAY_MS);
            state->long_press_sent = 0U;
            state->reset_sent = 0U;
            Key_EmitEvent(key, KEY_EVENT_PRESS, now);
        }
        return;
    }

    if (state->stable_state != KEY_ACTIVE_STATE)
    {
        return;
    }

    if ((key == KEY_ID_RST) && (state->long_press_sent == 0U) &&
        ((uint32_t)(now - state->press_time) >= KEY_LONG_PRESS_MS))
    {
        state->long_press_sent = 1U;
        Key_EmitEvent(key, KEY_EVENT_LONG_PRESS, now);
    }

    if ((key == KEY_ID_RST) && (state->reset_sent == 0U) &&
        ((uint32_t)(now - state->press_time) >= KEY_RST_RESET_MS))
    {
        state->reset_sent = 1U;
        Key_EmitEvent(key, KEY_EVENT_SYSTEM_RESET, now);
    }

    if ((state->repeat_enabled != 0U) &&
        ((int32_t)(now - state->next_repeat_time) >= 0))
    {
        state->next_repeat_time = (uint32_t)(now + KEY_REPEAT_INTERVAL_MS);
        Key_EmitEvent(key, KEY_EVENT_REPEAT, now);
    }
}

/*
 * Scan all joystick keys at a fixed cooperative interval.
 *
 * The main loop may call this function as often as possible. Work is performed
 * only every KEY_SCAN_INTERVAL_MS, which keeps debounce timing deterministic
 * without using EXTI interrupts or blocking delays.
 *
 * Parameters:
 * now: Current Timing_GetTick timestamp.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May enqueue debounced UI events.
 */
void Key_Task(uint32_t now)
{
    uint8_t key;

    if ((uint32_t)(now - g_key_last_scan_time) < KEY_SCAN_INTERVAL_MS)
    {
        return;
    }
    g_key_last_scan_time = now;

    for (key = 0U; key < (uint8_t)KEY_ID_COUNT; key++)
    {
        Key_UpdateOne((KeyId)key, now);
    }
}

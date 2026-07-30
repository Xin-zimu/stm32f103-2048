#include "page_player.h"
#include "ui_draw.h"
#include "ui_feedback.h"

#define TEXT_PLAYER_TITLE       "\xB2\xA5\xB7\xC5"
#define TEXT_NO_GIF            "NO GIF"
#define TEXT_PROGRAM_ANIM      "PROC ANIM"
#define TEXT_STATE             "\xD7\xB4\xCC\xAC"
#define TEXT_PLAYING           "\xB2\xA5\xB7\xC5"
#define TEXT_PAUSED            "\xD4\xDD\xCD\xA3"
#define TEXT_STOPPED           "\xCD\xA3\xD6\xB9"
#define TEXT_FOOTER_PLAYER     "\xC8\xB7\xC8\xCF\xB2\xA5\xB7\xC5  \xD7\xF3\xBC\xFC\xB7\xB5\xBB\xD8"

#define PLAYER_ANIM_X          24       // Animation canvas left coordinate.
#define PLAYER_ANIM_Y          50       // Animation canvas top coordinate.
#define PLAYER_ANIM_W          192      // Animation canvas width.
#define PLAYER_ANIM_H          92       // Animation canvas height.
#define PLAYER_ANIM_STEP_MS    33U      // Program animation frame interval.
#define PLAYER_SCAN_W          30       // Moving scan band width.
#define PLAYER_SPRITE_SIZE     12       // Moving block size.
#define PLAYER_WAVE_COUNT      8U       // Number of generated waveform bars.
#define PLAYER_ORBIT_COUNT     8U       // Number of generated orbit dots.
#define PLAYER_DASH_COUNT      6U       // Number of generated background dashes.
#define PLAYER_TRAIL_COUNT     4U       // Number of generated trail blocks.
#define PLAYER_STATE_X         24       // State panel left coordinate.
#define PLAYER_STATE_Y        158       // State panel top coordinate.
#define PLAYER_STATE_W        192       // State panel width.
#define PLAYER_STATE_H         44       // State panel height.

typedef enum
{
    PLAYER_STATE_STOPPED = 0,             // Program animation is stopped.
    PLAYER_STATE_PLAYING,                 // Program animation is running.
    PLAYER_STATE_PAUSED                   // Program animation is paused.
} PlayerState;

static PlayerState g_player_state = PLAYER_STATE_STOPPED;
static uint8_t g_player_progress = 0U;
static uint16_t g_player_frame = 0U;
static uint32_t g_player_last_step_ms = 0U;
static uint32_t g_player_draw_now = 0U;

/*
 * Build the fixed program-animation canvas rectangle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering only the PLAYER animation canvas.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Player_GetAnimRect(void)
{
    UI_Rect rect;

    rect.x = PLAYER_ANIM_X;
    rect.y = PLAYER_ANIM_Y;
    rect.w = PLAYER_ANIM_W;
    rect.h = PLAYER_ANIM_H;

    return rect;
}

/*
 * Mark the animation canvas dirty.
 *
 * The generated animation is fully contained in one fixed canvas. Repainting
 * only this rectangle keeps the PLAYER page useful as a strip-renderer stress
 * test without forcing the status bar, footer, or state row to refresh every
 * frame.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the animation canvas.
 */
static void Page_Player_InvalidateAnim(void)
{
    UI_Rect rect;

    rect = Page_Player_GetAnimRect();
    UI_PageInvalidate(&rect);
}

/*
 * Build the dynamic state panel rectangle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering state text and the progress bar.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Player_GetStateRect(void)
{
    UI_Rect rect;

    rect.x = PLAYER_STATE_X;
    rect.y = PLAYER_STATE_Y;
    rect.w = PLAYER_STATE_W;
    rect.h = PLAYER_STATE_H;

    return rect;
}

/*
 * Mark the dynamic player state area dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint covering state text and the progress bar.
 */
static void Page_Player_InvalidateState(void)
{
    UI_Rect rect;

    rect = Page_Player_GetStateRect();
    UI_PageInvalidate(&rect);
}

/*
 * Enter the lightweight program-animation placeholder page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets the simulated animation state.
 */
static void Page_Player_OnEnter(void)
{
    g_player_state = PLAYER_STATE_STOPPED;
    g_player_progress = 0U;
    g_player_frame = 0U;
    g_player_last_step_ms = 0U;
}

/*
 * Handle lightweight player placeholder events.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the program-animation placeholder state and requests local repaint.
 */
static void Page_Player_OnEvent(const UI_Event *event)
{
    if (event->type == UI_EVENT_LEFT)
    {
        UI_PageBack();
    }
    else if (event->type == UI_EVENT_OK)
    {
        UI_Rect rect;

        if (g_player_state == PLAYER_STATE_PLAYING)
        {
            g_player_state = PLAYER_STATE_PAUSED;
            g_player_progress = 50U;
        }
        else
        {
            g_player_state = PLAYER_STATE_PLAYING;
            g_player_progress = 100U;
            g_player_last_step_ms = event->timestamp;
        }
        rect = Page_Player_GetStateRect();
        UI_FeedbackPress(&rect, event->timestamp);
        Page_Player_InvalidateState();
        Page_Player_InvalidateAnim();
    }
    else if (event->type == UI_EVENT_RIGHT)
    {
        UI_Rect rect;

        g_player_state = PLAYER_STATE_PLAYING;
        g_player_progress = 100U;
        g_player_frame = 0U;
        g_player_last_step_ms = event->timestamp;
        rect = Page_Player_GetStateRect();
        UI_FeedbackPress(&rect, event->timestamp);
        Page_Player_InvalidateState();
        Page_Player_InvalidateAnim();
    }
}

/*
 * Advance the generated PLAYER animation.
 *
 * The page manager calls page tasks only after the strip renderer becomes
 * idle, so dropping intermediate time slices is intentional. The animation is
 * generated from counters and rectangles, not stored frames, so skipped frames
 * do not accumulate memory or dirty queue work.
 *
 * Parameters:
 * now: Current scheduler tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the frame counter and queues the animation canvas for repaint.
 */
static void Page_Player_Task(uint32_t now)
{
    g_player_draw_now = now;
    if (g_player_state != PLAYER_STATE_PLAYING)
    {
        return;
    }

    if ((now - g_player_last_step_ms) < PLAYER_ANIM_STEP_MS)
    {
        return;
    }

    g_player_last_step_ms = now;
    g_player_frame++;
    Page_Player_InvalidateAnim();
}

/*
 * Convert a 16-bit value to decimal ASCII.
 *
 * The PLAYER page uses this for frame display without pulling in formatted
 * stdio. The caller supplies a small buffer, and the function always writes a
 * terminated string when the buffer has at least two bytes.
 *
 * Parameters:
 * value: Value to format.
 * buffer: Destination character buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes buffer.
 */
static void Page_Player_FormatU16(uint16_t value, char *buffer, uint8_t buffer_size)
{
    char digits[5];
    uint8_t count;
    uint8_t index;

    if ((buffer == 0) || (buffer_size < 2U))
    {
        return;
    }

    count = 0U;
    do
    {
        digits[count] = (char)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
        count++;
    } while ((value != 0U) && (count < sizeof(digits)));

    if (count >= buffer_size)
    {
        count = (uint8_t)(buffer_size - 1U);
    }

    for (index = 0U; index < count; index++)
    {
        buffer[index] = digits[count - 1U - index];
    }
    buffer[count] = '\0';
}

/*
 * Draw extra generated details for the PLAYER animation.
 *
 * This helper keeps the animation richer without storing image frames. All
 * positions are derived from g_player_frame with small integer tables and
 * modulo arithmetic, so the RAM cost stays fixed and the drawing remains safe
 * for narrow dirty-window clipping.
 *
 * Parameters:
 * rect: Animation canvas rectangle.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws dashes, orbit dots, and trail blocks inside the animation canvas.
 */
static void Page_Player_DrawGeneratedDetails(const UI_Rect *rect)
{
    static const int8_t orbit_x[PLAYER_ORBIT_COUNT] = {0, 11, 16, 11, 0, -11, -16, -11};
    static const int8_t orbit_y[PLAYER_ORBIT_COUNT] = {-16, -11, 0, 11, 16, 11, 0, -11};
    uint8_t index;
    uint8_t phase;
    int16_t center_x;
    int16_t center_y;
    int16_t x;
    int16_t y;
    uint16_t color;

    if (rect == 0)
    {
        return;
    }

    for (index = 0U; index < PLAYER_DASH_COUNT; index++)
    {
        x = (int16_t)(rect->x + 8 +
            (int16_t)(((uint32_t)g_player_frame * 2U + (uint32_t)index * 28U) %
            (uint16_t)(rect->w - 28)));
        y = (int16_t)(rect->y + 12 + ((int16_t)index * 10));
        UI_DrawRect(x, y, 14, 2, UI_COLOR_DIM);
    }

    center_x = (int16_t)(rect->x + rect->w - 42);
    center_y = (int16_t)(rect->y + 38);
    UI_DrawFrame((int16_t)(center_x - 22), (int16_t)(center_y - 22), 44, 44, UI_COLOR_DIM);
    for (index = 0U; index < PLAYER_ORBIT_COUNT; index++)
    {
        phase = (uint8_t)(((g_player_frame / 2U) + index) & 7U);
        color = ((index & 1U) == 0U) ? UI_COLOR_WARN : UI_COLOR_ACCENT;
        UI_DrawRect(
            (int16_t)(center_x + orbit_x[phase] - 1),
            (int16_t)(center_y + orbit_y[phase] - 1),
            3,
            3,
            color
        );
    }

    for (index = 0U; index < PLAYER_TRAIL_COUNT; index++)
    {
        x = (int16_t)(rect->x + 28 +
            (int16_t)(((uint32_t)g_player_frame * 5U + (uint32_t)index * 17U) %
            (uint16_t)(rect->w - 64)));
        y = (int16_t)(rect->y + 70 - ((int16_t)index * 6));
        color = (index == 0U) ? UI_COLOR_OK : UI_COLOR_SURFACE_2;
        UI_DrawRect(x, y, (int16_t)(12 - ((int16_t)index * 2)), 4, color);
    }
}

/*
 * Draw the generated animation canvas.
 *
 * PLAYING uses a moving scan band, a sprite block, waveform bars, background
 * dashes, orbit dots, and trail blocks to exercise continuous local refresh.
 * STOPPED and PAUSED keep the canvas deterministic and static, proving the
 * state machine can hold a frame without background redraw work.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the PLAYER animation canvas through clipped primitives.
 */
static void Page_Player_DrawAnim(void)
{
    UI_Rect rect;
    char frame_text[6];
    uint8_t index;
    uint8_t phase;
    int16_t scan_span;
    int16_t scan_x;
    int16_t sprite_span;
    int16_t sprite_x;
    int16_t sprite_y;
    int16_t bar_x;
    int16_t bar_h;
    int16_t base_y;

    rect = Page_Player_GetAnimRect();
    UI_DrawRect(rect.x, rect.y, rect.w, rect.h, UI_COLOR_SURFACE);
    UI_DrawFrame(rect.x, rect.y, rect.w, rect.h, UI_COLOR_MUTED);

    if (g_player_state == PLAYER_STATE_STOPPED)
    {
        UI_DrawTextCN(76, 72, TEXT_NO_GIF, UI_COLOR_WARN);
        UI_DrawTextCN(62, 98, TEXT_PROGRAM_ANIM, UI_COLOR_TEXT);
        return;
    }

    if (g_player_state == PLAYER_STATE_PAUSED)
    {
        UI_DrawTextCN(76, 84, TEXT_PAUSED, UI_COLOR_WARN);
        return;
    }

    scan_span = (int16_t)(rect.w - PLAYER_SCAN_W - 2);
    scan_x = (int16_t)(rect.x + 1 + (int16_t)(((uint32_t)g_player_frame * 4U) % (uint16_t)scan_span));
    UI_DrawRect(scan_x, (int16_t)(rect.y + 2), PLAYER_SCAN_W, (int16_t)(rect.h - 4), UI_COLOR_SURFACE_2);

    sprite_span = (int16_t)(rect.w - PLAYER_SPRITE_SIZE - 2);
    sprite_x = (int16_t)(rect.x + 1 + (int16_t)(((uint32_t)g_player_frame * 3U) % (uint16_t)sprite_span));
    sprite_y = (int16_t)(rect.y + 18 + (int16_t)((g_player_frame / 3U) % 24U));
    UI_DrawRect(sprite_x, sprite_y, PLAYER_SPRITE_SIZE, PLAYER_SPRITE_SIZE, UI_COLOR_ACCENT);

    UI_DrawText(34, 58, "F", UI_COLOR_MUTED);
    Page_Player_FormatU16(g_player_frame, frame_text, (uint8_t)sizeof(frame_text));
    UI_DrawText(50, 58, frame_text, UI_COLOR_TEXT);
    Page_Player_DrawGeneratedDetails(&rect);

    base_y = (int16_t)(rect.y + rect.h - 14);
    for (index = 0U; index < PLAYER_WAVE_COUNT; index++)
    {
        phase = (uint8_t)((g_player_frame + (uint16_t)(index * 3U)) % 24U);
        bar_h = (int16_t)(8U + ((phase < 12U) ? phase : (uint8_t)(24U - phase)));
        bar_x = (int16_t)(rect.x + 18 + ((int16_t)index * 20));
        UI_DrawRect(bar_x, (int16_t)(base_y - bar_h), 10, bar_h, UI_COLOR_OK);
    }
}

/*
 * Draw the dynamic PLAYER state panel.
 *
 * The panel uses the shared feedback helper so OK and RIGHT presses have the
 * same short visual response style as settings value changes.
 *
 * Parameters:
 * state_text: Current state text.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws state text and progress bar.
 */
static void Page_Player_DrawState(const char *state_text)
{
    UI_Rect rect;
    uint16_t fill;

    rect = Page_Player_GetStateRect();
    fill = (UI_FeedbackIsActive(&rect, g_player_draw_now) != 0U) ?
        UI_COLOR_SURFACE_2 :
        UI_COLOR_BG;

    UI_DrawRect(rect.x, rect.y, rect.w, rect.h, fill);
    UI_DrawTextCN(30, 164, TEXT_STATE, UI_COLOR_MUTED);
    UI_DrawTextCN(94, 164, state_text, UI_COLOR_TEXT);
    UI_DrawProgressBar(30, 190, 180, g_player_progress, 100U);
}

/*
 * Draw the program-animation placeholder page within the requested clip.
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
static void Page_Player_Draw(const UI_Rect *clip)
{
    const char *state_text;

    (void)clip;

    state_text = TEXT_STOPPED;
    if (g_player_state == PLAYER_STATE_PLAYING)
    {
        state_text = TEXT_PLAYING;
    }
    else if (g_player_state == PLAYER_STATE_PAUSED)
    {
        state_text = TEXT_PAUSED;
    }

    UI_DrawStatusBar(TEXT_PLAYER_TITLE, UI_COLOR_ACCENT);
    Page_Player_DrawAnim();
    Page_Player_DrawState(state_text);
    UI_DrawFooter(TEXT_FOOTER_PLAYER);
}

const UI_PageOps PAGE_PLAYER_OPS =
{
    Page_Player_OnEnter,
    Page_Player_OnEvent,
    Page_Player_Task,
    Page_Player_Draw
};

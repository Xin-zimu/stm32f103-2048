#include "app_st7789_anim.h"
#include "anim_frames.h"
#include "bsp_st7789.h"
#include "timing.h"

static uint32_t g_anim_last_tick = 0U;
static uint8_t g_anim_transition_index = 0U;
static uint8_t g_anim_paused = 1U;

/*
 * Draw the first retained GIF frame and reset animation scheduling.
 *
 * Delta frames depend on the current ST7789 RAM image matching the previous
 * retained frame. Repainting the first full frame gives both normal startup
 * and error recovery a known base image before later deltas are applied.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Replaces the full ST7789 image and resets the frame scheduler.
 */
static void App_ST7789_DrawFirstFrameAndReset(void)
{
    ST7789_ShowIndexed4Image(
        anim_first_frame,
        anim_palette,
        ANIM_FRAME_WIDTH,
        ANIM_FRAME_HEIGHT,
        ANIM_PIXEL_SCALE
    );

    g_anim_last_tick = Timing_GetTick();
    g_anim_transition_index = 0U;
}

/*
 * Finish bookkeeping for one completed ST7789 animation transition.
 *
 * The asynchronous display driver reports completion separately from the time
 * check that started the transition. Keeping the frame deadline advance and
 * transition index wrap in one helper preserves fixed-period scheduling while
 * avoiding duplicate boundary logic in App_ST7789_AnimTask.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Advances the animation deadline and transition index.
 */
static void App_ST7789_FinishTransition(void)
{
    g_anim_last_tick += ANIM_FRAME_INTERVAL_MS;
    g_anim_transition_index++;
    if (g_anim_transition_index >= ANIM_FRAME_COUNT)
    {
        g_anim_transition_index = 0U;
    }
}

/*
 * Handle the result reported by one asynchronous ST7789 delta task step.
 *
 * Only a normal DONE result advances the retained-frame index. DMA or delta
 * stream errors leave the current display contents uncertain, so recovery
 * redraws the complete first frame and restarts the delta chain from frame 0.
 *
 * Parameters:
 * result: Status returned by ST7789_AnimDeltaTask.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May advance the animation transition or redraw the full first frame.
 */
static void App_ST7789_HandleDeltaResult(ST7789_AnimDeltaResult result)
{
    if (result == ST7789_ANIM_DELTA_RESULT_DONE)
    {
        App_ST7789_FinishTransition();
        return;
    }

    if (result == ST7789_ANIM_DELTA_RESULT_ERROR)
    {
        App_ST7789_DrawFirstFrameAndReset();
    }
}

/*
 * Initialize the ST7789 GIF playback state and draw the complete first frame.
 *
 * The first frame is stored as a normal packed four-bit indexed image because
 * it establishes every display pixel. Every later transition, including the
 * last frame back to the first, retains unchanged controller RAM pixels and
 * applies only its encoded differences.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Replaces the full ST7789 image and resets the frame scheduler.
 */
void App_ST7789_AnimInit(void)
{
    g_anim_paused = 0U;
    App_ST7789_DrawFirstFrameAndReset();
}

/*
 * Restart GIF playback from the retained first frame.
 *
 * This entry point is used when the UI enters or replays the GIF page. Drawing
 * the first frame reestablishes the ST7789 RAM base image required by later
 * delta transitions, then playback resumes from transition zero.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Replaces the full ST7789 image, clears the paused flag, and resets timing.
 */
void App_ST7789_AnimRestart(void)
{
    g_anim_paused = 0U;
    App_ST7789_DrawFirstFrameAndReset();
}

/*
 * Toggle GIF playback pause state without changing the current display image.
 *
 * Resuming resets the frame deadline to the current tick so a long pause does
 * not cause a burst of overdue delta transitions. Any in-flight delta update
 * is allowed to finish before the pause takes visible effect.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates playback scheduling state.
 */
void App_ST7789_AnimTogglePaused(void)
{
    if (g_anim_paused == 0U)
    {
        g_anim_paused = 1U;
    }
    else
    {
        g_anim_paused = 0U;
        g_anim_last_tick = Timing_GetTick();
    }
}

/*
 * Advance GIF playback using the non-blocking ST7789 delta state machine.
 *
 * Each offset pair describes one transition: frame 0 to 1 through the final
 * frame back to frame 0. Once a transition starts, this task advances only one
 * DMA-sized step per call and returns while SPI2 DMA is still moving pixels.
 * The frame deadline is advanced only after the full transition completes.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the ST7789 when one animation frame becomes due.
 */
void App_ST7789_AnimTask(void)
{
    uint32_t now;
    ST7789_AnimDeltaResult result;

    if (ST7789_AnimDeltaBusy() != 0U)
    {
        result = ST7789_AnimDeltaTask();
        App_ST7789_HandleDeltaResult(result);
        return;
    }

    if (g_anim_paused != 0U)
    {
        return;
    }

    now = Timing_GetTick();
    if ((now - g_anim_last_tick) < ANIM_FRAME_INTERVAL_MS)
    {
        return;
    }

    {
        uint32_t delta_start;
        uint32_t delta_end;

        delta_start = anim_delta_offsets[g_anim_transition_index];
        delta_end = anim_delta_offsets[g_anim_transition_index + 1U];
        if (ST7789_AnimDeltaStart(
            &anim_delta_data[delta_start],
            delta_end - delta_start,
            anim_palette,
            ANIM_FRAME_WIDTH,
            ANIM_FRAME_HEIGHT,
            ANIM_PIXEL_SCALE
        ) != 0U)
        {
            result = ST7789_AnimDeltaTask();
            App_ST7789_HandleDeltaResult(result);
        }
    }
}

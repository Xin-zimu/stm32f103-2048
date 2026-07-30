#include "ui_renderer.h"
#include "app_config.h"
#include "bsp_st7789.h"
#include "lcd_dma.h"
#include "ui_dirty.h"
#include "ui_draw.h"

#define UI_RENDER_STRIP_BYTES \
    (APP_LCD_WIDTH * APP_UI_STRIP_HEIGHT * 2U)
#define UI_RENDER_TASK_STEP_LIMIT       16U      // Cooperative state steps per task call.

typedef enum
{
    UI_RENDER_IDLE = 0,                // No dirty rectangle is active.
    UI_RENDER_PREPARE_STRIP,           // Select the next strip of current dirty area.
    UI_RENDER_DRAW_STRIP,              // Render page content into a free buffer.
    UI_RENDER_SUBMIT_DMA,              // Submit the drawn strip to LCD DMA.
    UI_RENDER_WAIT_DMA,                // Wait cooperatively for DMA completion.
    UI_RENDER_NEXT_STRIP               // Advance within the dirty rectangle.
} UI_RenderState;

typedef enum
{
    UI_BUFFER_FREE = 0,                // Buffer is available for drawing.
    UI_BUFFER_DRAWING,                 // CPU is preparing pixels.
    UI_BUFFER_READY,                   // Pixels are ready to send.
    UI_BUFFER_SENDING                  // DMA owns this buffer.
} UI_BufferState;

typedef struct
{
    uint8_t *data;                     // Strip buffer storage.
    UI_BufferState state;              // Buffer ownership state.
    UI_Rect area;                      // Screen-space strip in this buffer.
    uint32_t valid_length;             // Valid byte count for DMA.
} UI_StripBuffer;

typedef struct
{
    UI_RenderState state;              // Current renderer state.
    UI_Rect dirty;                     // Dirty rectangle being consumed.
    UI_Rect strip;                     // Current x-clipped strip.
    int16_t next_y;                    // Next Y coordinate inside dirty.
    uint8_t buffer_index;              // Active buffer index.
} UI_RenderContext;

static uint8_t g_ui_strip_storage[APP_UI_STRIP_BUFFER_COUNT][UI_RENDER_STRIP_BYTES];
static UI_StripBuffer g_ui_strip_buffers[APP_UI_STRIP_BUFFER_COUNT];
static UI_RenderContext g_ui_renderer;
static UI_RendererStats g_ui_renderer_stats;

/*
 * Return the index of a free strip buffer.
 *
 * Parameters:
 * index_out: Receives the free buffer index.
 *
 * Return value:
 * 1: A free buffer was found.
 * 0: No buffer is currently free.
 *
 * Side effects:
 * None.
 */
static uint8_t UI_RendererFindFreeBuffer(uint8_t *index_out)
{
    uint8_t index;

    if (index_out == 0)
    {
        return 0U;
    }

    for (index = 0U; index < APP_UI_STRIP_BUFFER_COUNT; index++)
    {
        if (g_ui_strip_buffers[index].state == UI_BUFFER_FREE)
        {
            *index_out = index;
            return 1U;
        }
    }

    return 0U;
}

/*
 * Prepare the next x-clipped strip from the active dirty rectangle.
 *
 * The renderer slices the dirty rectangle by Y, but preserves the dirty x/w
 * window for every strip. This reduces SPI traffic for narrow controls while
 * still using the same fixed maximum strip buffer allocation.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: g_ui_renderer.strip contains a valid strip.
 * 0: The active dirty rectangle is complete.
 *
 * Side effects:
 * Updates g_ui_renderer.strip.
 */
static uint8_t UI_RendererPrepareStrip(void)
{
    int16_t dirty_bottom;
    int16_t strip_h;

    dirty_bottom = (int16_t)(g_ui_renderer.dirty.y + g_ui_renderer.dirty.h);
    if (g_ui_renderer.next_y >= dirty_bottom)
    {
        return 0U;
    }

    strip_h = (int16_t)APP_UI_STRIP_HEIGHT;
    if ((g_ui_renderer.next_y + strip_h) > dirty_bottom)
    {
        strip_h = (int16_t)(dirty_bottom - g_ui_renderer.next_y);
    }

    g_ui_renderer.strip.x = g_ui_renderer.dirty.x;
    g_ui_renderer.strip.y = g_ui_renderer.next_y;
    g_ui_renderer.strip.w = g_ui_renderer.dirty.w;
    g_ui_renderer.strip.h = strip_h;

    return 1U;
}

/*
 * Draw the active page into the selected strip buffer.
 *
 * Parameters:
 * page: Active page operations.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes one x-clipped strip buffer and marks it READY.
 */
static void UI_RendererDrawStrip(const UI_PageOps *page)
{
    UI_StripBuffer *buffer;

    buffer = &g_ui_strip_buffers[g_ui_renderer.buffer_index];
    buffer->state = UI_BUFFER_DRAWING;
    buffer->area = g_ui_renderer.strip;
    buffer->valid_length = (uint32_t)buffer->area.w * (uint32_t)buffer->area.h * 2U;

    UI_DrawBeginBuffer(buffer->data, &buffer->area, &buffer->area);
    UI_DrawClearClip(UI_COLOR_BG);
    if ((page != 0) && (page->draw != 0))
    {
        page->draw(&buffer->area);
    }
    UI_DrawEndBuffer();

    buffer->state = UI_BUFFER_READY;
    g_ui_renderer_stats.strips_drawn++;
}

/*
 * Submit the ready strip buffer to ST7789 through LCD DMA.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: DMA accepted the strip.
 * 0: DMA was busy or the buffer was invalid.
 *
 * Side effects:
 * Sets the ST7789 address window and starts SPI2 TX DMA.
 */
static uint8_t UI_RendererSubmitStrip(void)
{
    UI_StripBuffer *buffer;
    LCD_DMA_Transfer transfer;

    buffer = &g_ui_strip_buffers[g_ui_renderer.buffer_index];
    if ((buffer->state != UI_BUFFER_READY) || (buffer->valid_length == 0U))
    {
        return 0U;
    }

    ST7789_WaitWriteComplete();
    ST7789_SetAddressWindow(
        (uint16_t)buffer->area.x,
        (uint16_t)buffer->area.y,
        (uint16_t)(buffer->area.x + buffer->area.w - 1),
        (uint16_t)(buffer->area.y + buffer->area.h - 1)
    );
    ST7789_BeginDataWrite();

    transfer.x = (uint16_t)buffer->area.x;
    transfer.y = (uint16_t)buffer->area.y;
    transfer.width = (uint16_t)buffer->area.w;
    transfer.height = (uint16_t)buffer->area.h;
    transfer.data = buffer->data;
    transfer.data_length = buffer->valid_length;
    transfer.callback = 0;
    transfer.user_data = 0;

    if (LCD_DMA_Start(&transfer) == 0U)
    {
        return 0U;
    }

    buffer->state = UI_BUFFER_SENDING;
    g_ui_renderer_stats.strips_submitted++;
    g_ui_renderer_stats.bytes_submitted += buffer->valid_length;
    return 1U;
}

/*
 * Initialize the strip renderer.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears renderer state and marks all strip buffers free.
 */
void UI_RendererInit(void)
{
    uint8_t index;

    for (index = 0U; index < APP_UI_STRIP_BUFFER_COUNT; index++)
    {
        g_ui_strip_buffers[index].data = g_ui_strip_storage[index];
        g_ui_strip_buffers[index].state = UI_BUFFER_FREE;
        g_ui_strip_buffers[index].area.x = 0;
        g_ui_strip_buffers[index].area.y = 0;
        g_ui_strip_buffers[index].area.w = 0;
        g_ui_strip_buffers[index].area.h = 0;
        g_ui_strip_buffers[index].valid_length = 0U;
    }

    g_ui_renderer.state = UI_RENDER_IDLE;
    g_ui_renderer.next_y = 0;
    g_ui_renderer.buffer_index = 0U;
    UI_RendererResetStats();
}

/*
 * Service the UI strip renderer state machine.
 *
 * Each call advances through cheap CPU-only states until a strip is submitted
 * to DMA, DMA is still busy, no dirty work remains, or the cooperative step
 * limit is reached. This keeps key scanning responsive while avoiding the old
 * one-state-per-call delay that let focus movement outrun row cleanup.
 *
 * Parameters:
 * now: Current scheduler timestamp, reserved for later renderer statistics.
 * page: Active page operations used to draw strip content.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May consume dirty rectangles, draw strip buffers, and start LCD DMA.
 */
void UI_RendererTask(uint32_t now, const UI_PageOps *page)
{
    uint8_t steps;

    (void)now;

    g_ui_renderer_stats.task_calls++;
    LCD_DMA_Task();
    steps = UI_RENDER_TASK_STEP_LIMIT;
    while (steps > 0U)
    {
        steps--;
        switch (g_ui_renderer.state)
        {
            case UI_RENDER_IDLE:
                if (UI_DirtyPop(&g_ui_renderer.dirty) == 0U)
                {
                    return;
                }
                g_ui_renderer_stats.dirty_rects_started++;
                g_ui_renderer.next_y = g_ui_renderer.dirty.y;
                g_ui_renderer.state = UI_RENDER_PREPARE_STRIP;
                break;

            case UI_RENDER_PREPARE_STRIP:
                if (UI_RendererPrepareStrip() == 0U)
                {
                    g_ui_renderer.state = UI_RENDER_IDLE;
                    break;
                }
                g_ui_renderer.state = UI_RENDER_DRAW_STRIP;
                break;

            case UI_RENDER_DRAW_STRIP:
                if (UI_RendererFindFreeBuffer(&g_ui_renderer.buffer_index) == 0U)
                {
                    g_ui_renderer_stats.no_buffer_returns++;
                    g_ui_renderer_stats.busy_returns++;
                    return;
                }
                UI_RendererDrawStrip(page);
                g_ui_renderer.state = UI_RENDER_SUBMIT_DMA;
                break;

            case UI_RENDER_SUBMIT_DMA:
                if (UI_RendererSubmitStrip() == 0U)
                {
                    g_ui_renderer_stats.submit_busy_returns++;
                    g_ui_renderer_stats.busy_returns++;
                    return;
                }
                g_ui_renderer.state = UI_RENDER_WAIT_DMA;
                return;

            case UI_RENDER_WAIT_DMA:
                if (LCD_DMA_IsBusy() != 0U)
                {
                    g_ui_renderer_stats.dma_busy_returns++;
                    g_ui_renderer_stats.busy_returns++;
                    return;
                }
                ST7789_WaitWriteComplete();
                g_ui_strip_buffers[g_ui_renderer.buffer_index].state = UI_BUFFER_FREE;
                g_ui_renderer.state = UI_RENDER_NEXT_STRIP;
                break;

            case UI_RENDER_NEXT_STRIP:
                g_ui_renderer.next_y = (int16_t)(g_ui_renderer.strip.y + g_ui_renderer.strip.h);
                g_ui_renderer.state = UI_RENDER_PREPARE_STRIP;
                break;

            default:
                g_ui_renderer.state = UI_RENDER_IDLE;
                break;
        }
    }
}

/*
 * Report whether the renderer has pending work or a buffer in flight.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: Renderer or DMA is busy.
 * 0: Renderer is idle and no LCD DMA transfer is active.
 *
 * Side effects:
 * None.
 */
uint8_t UI_RendererIsBusy(void)
{
    if ((g_ui_renderer.state != UI_RENDER_IDLE) || (LCD_DMA_IsBusy() != 0U))
    {
        return 1U;
    }

    return 0U;
}

/*
 * Request a full-screen redraw through the dirty queue.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a full-screen dirty area.
 */
void UI_RendererRequestFull(void)
{
    UI_DirtyFullScreen();
}

/*
 * Read renderer service statistics.
 *
 * The counters are intended for debugger inspection and for the upcoming
 * renderer scheduler. They show whether drawing is blocked by DMA, strip
 * buffers, or submit timing without changing normal rendering behavior.
 *
 * Parameters:
 * stats: Destination structure that receives a snapshot of the counters.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes stats when the pointer is valid.
 */
void UI_RendererGetStats(UI_RendererStats *stats)
{
    if (stats == 0)
    {
        return;
    }

    *stats = g_ui_renderer_stats;
}

/*
 * Reset renderer service statistics.
 *
 * Runtime diagnostics can call this at the beginning of a manual stress test
 * so the counters describe only that test interval. Renderer state, dirty
 * queue contents, and in-flight DMA transfers are not changed.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears accumulated renderer statistics.
 */
void UI_RendererResetStats(void)
{
    g_ui_renderer_stats.task_calls = 0U;
    g_ui_renderer_stats.busy_returns = 0U;
    g_ui_renderer_stats.dma_busy_returns = 0U;
    g_ui_renderer_stats.no_buffer_returns = 0U;
    g_ui_renderer_stats.submit_busy_returns = 0U;
    g_ui_renderer_stats.dirty_rects_started = 0U;
    g_ui_renderer_stats.strips_drawn = 0U;
    g_ui_renderer_stats.strips_submitted = 0U;
    g_ui_renderer_stats.bytes_submitted = 0U;
}

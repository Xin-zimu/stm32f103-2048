#include "page_info.h"
#include "ui_dirty.h"
#include "ui_draw.h"
#include "ui_renderer.h"

#define TEXT_INFO_TITLE        "\xCF\xB5\xCD\xB3\xD0\xC5\xCF\xA2"
#define TEXT_STRIP            "STRIP"
#define TEXT_KB               "KB"
#define TEXT_BUSY             "BUSY"
#define TEXT_DMA              "DMA"
#define TEXT_DIRTY            "DIRTY"
#define TEXT_FOOTER_INFO      "L BACK OK CLR R REF"

static char g_info_strip_text[8];
static char g_info_kb_text[8];
static char g_info_busy_text[8];
static char g_info_dma_text[8];
static char g_info_dirty_text[8];

/*
 * Convert a 32-bit counter to compact decimal ASCII.
 *
 * INFO draws renderer counters without using formatted stdio. When a counter
 * exceeds the display budget, the field is saturated with 9s so the screen
 * still shows a normal numeric value instead of a misleading truncated one.
 *
 * Parameters:
 * value: Counter value to format.
 * buffer: Destination buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes buffer.
 */
static void Page_Info_FormatU32(uint32_t value, char *buffer, uint8_t buffer_size)
{
    char digits[10];
    uint8_t count;
    uint8_t index;
    uint8_t max_digits;

    if ((buffer == 0) || (buffer_size < 2U))
    {
        return;
    }

    max_digits = (uint8_t)(buffer_size - 1U);
    count = 0U;
    do
    {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count++;
    } while ((value != 0U) && (count < sizeof(digits)));

    if (count > max_digits)
    {
        for (index = 0U; index < max_digits; index++)
        {
            buffer[index] = '9';
        }
        buffer[max_digits] = '\0';
        return;
    }

    for (index = 0U; index < count; index++)
    {
        buffer[index] = digits[count - 1U - index];
    }
    buffer[count] = '\0';
}

/*
 * Format a byte counter as rounded-up kilobytes.
 *
 * The raw byte counter can grow quickly and is hard to read on a 240 pixel
 * display. Rounding nonzero byte counts up to at least 1 KB keeps the field
 * compact while still showing whether narrow-strip traffic is increasing.
 *
 * Parameters:
 * bytes: Raw byte counter.
 * buffer: Destination buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes buffer.
 */
static void Page_Info_FormatKB(uint32_t bytes, char *buffer, uint8_t buffer_size)
{
    uint32_t kb;

    kb = bytes / 1024U;
    if ((bytes % 1024U) != 0U)
    {
        kb++;
    }
    Page_Info_FormatU32(kb, buffer, buffer_size);
}

/*
 * Format dirty queue health as max/current/overflow.
 *
 * The INFO page has only a narrow value column. The first two digits show the
 * maximum and current pending dirty count; the last digit shows the low digit
 * of overflow_count so an on-board stress test can still reveal promotion to
 * full-screen repaint without formatted stdio.
 *
 * Parameters:
 * stats: Dirty queue statistics snapshot.
 * buffer: Destination buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes buffer.
 */
static void Page_Info_FormatDirty(const UI_DirtyStats *stats, char *buffer, uint8_t buffer_size)
{
    if ((stats == 0) || (buffer == 0) || (buffer_size < 6U))
    {
        return;
    }

    buffer[0] = (char)('0' + stats->max_pending_rects);
    buffer[1] = '/';
    buffer[2] = (char)('0' + stats->pending_rects);
    buffer[3] = '/';
    buffer[4] = (char)('0' + (uint8_t)(stats->overflow_count % 10U));
    buffer[5] = '\0';
}

/*
 * Capture one stable diagnostics snapshot for the INFO page.
 *
 * The strip renderer calls page draw functions once per transmitted strip.
 * Reading live counters inside Page_Info_Draw would let different horizontal
 * slices of the same number come from different counter values, producing
 * visually broken digits. This function formats all values once before a
 * redraw so every strip uses the same text.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the cached INFO text buffers.
 */
static void Page_Info_CaptureStats(void)
{
    UI_RendererStats renderer_stats;
    UI_DirtyStats dirty_stats;

    UI_RendererGetStats(&renderer_stats);
    UI_DirtyGetStats(&dirty_stats);
    Page_Info_FormatU32(renderer_stats.strips_submitted, g_info_strip_text, (uint8_t)sizeof(g_info_strip_text));
    Page_Info_FormatKB(renderer_stats.bytes_submitted, g_info_kb_text, (uint8_t)sizeof(g_info_kb_text));
    Page_Info_FormatU32(renderer_stats.busy_returns, g_info_busy_text, (uint8_t)sizeof(g_info_busy_text));
    Page_Info_FormatU32(renderer_stats.dma_busy_returns, g_info_dma_text, (uint8_t)sizeof(g_info_dma_text));
    Page_Info_FormatDirty(&dirty_stats, g_info_dirty_text, (uint8_t)sizeof(g_info_dirty_text));
}

/*
 * Reset renderer and dirty counters, then display the zero snapshot.
 *
 * The redraw that shows the zero snapshot will itself add later renderer
 * traffic. That cost is intentionally not folded into the just-cleared display;
 * pressing RIGHT after the redraw shows the post-reset rendering cost.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears renderer and dirty statistics and updates cached INFO text buffers.
 */
static void Page_Info_ResetStats(void)
{
    UI_RendererResetStats();
    UI_DirtyResetStats();
    Page_Info_CaptureStats();
}

/*
 * Enter the information page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Captures a stable renderer and dirty statistics snapshot for drawing.
 */
static void Page_Info_OnEnter(void)
{
    Page_Info_CaptureStats();
}

/*
 * Handle information page events.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * LEFT returns to HOME. OK clears the displayed diagnostics window, while
 * RIGHT refreshes the snapshot without clearing counters.
 */
static void Page_Info_OnEvent(const UI_Event *event)
{
    if (event->type == UI_EVENT_LEFT)
    {
        UI_PageBack();
        return;
    }

    if (event->type == UI_EVENT_OK)
    {
        Page_Info_ResetStats();
        UI_PageRequestRedraw();
        return;
    }

    if (event->type == UI_EVENT_RIGHT)
    {
        Page_Info_CaptureStats();
        UI_PageRequestRedraw();
        return;
    }
}

/*
 * Draw one labeled information row.
 *
 * Parameters:
 * y: Top coordinate.
 * label: Left label.
 * value: Right value.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws one row.
 */
static void Page_Info_DrawRow(int16_t y, const char *label, const char *value)
{
    UI_DrawInfoRowCN(y, label, value);
}

/*
 * Draw the system information page within the requested clip.
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
static void Page_Info_Draw(const UI_Rect *clip)
{
    (void)clip;

    UI_DrawStatusBar(TEXT_INFO_TITLE, UI_COLOR_OK);
    Page_Info_DrawRow(42, TEXT_STRIP, g_info_strip_text);
    Page_Info_DrawRow(76, TEXT_KB, g_info_kb_text);
    Page_Info_DrawRow(110, TEXT_BUSY, g_info_busy_text);
    Page_Info_DrawRow(144, TEXT_DMA, g_info_dma_text);
    Page_Info_DrawRow(178, TEXT_DIRTY, g_info_dirty_text);
    UI_DrawFooter(TEXT_FOOTER_INFO);
}

const UI_PageOps PAGE_INFO_OPS =
{
    Page_Info_OnEnter,
    Page_Info_OnEvent,
    0,
    Page_Info_Draw
};

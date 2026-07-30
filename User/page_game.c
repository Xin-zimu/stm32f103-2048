#include "page_game.h"
#include "game_2048.h"
#include "timing.h"
#include "ui_draw.h"
#include "ui_feedback.h"
#include "ui_renderer.h"

#define PAGE_GAME_BOARD_X          32      // Board left coordinate.
#define PAGE_GAME_BOARD_Y          34      // Board top coordinate.
#define PAGE_GAME_CELL             39      // Tile square size.
#define PAGE_GAME_GAP               4      // Gap between tiles.
#define PAGE_GAME_BOARD_SIZE      176      // Full board area including gaps.
#define PAGE_GAME_INPUT_THROTTLE   90U     // Minimum time between direction attempts.
#define PAGE_GAME_RESTART_GUARD   300U     // Minimum time between OK restarts.
#define PAGE_GAME_NEW_FLASH_MS    220U     // New tile highlight duration.
#define TEXT_GAME_TITLE          "2048"
#define TEXT_FOOTER_GAME         "JOY MOVE  OK NEW  RST BACK"

static uint8_t g_page_game_initialized = 0U;
static uint8_t g_page_game_input_locked = 0U;
static uint16_t g_page_game_new_flash_mask = 0U;
static uint32_t g_page_game_new_flash_until_ms = 0U;
static uint32_t g_page_game_last_input_ms = 0U;
static uint32_t g_page_game_last_restart_ms = 0U;
static uint32_t g_page_game_draw_now = 0U;

/*
 * Build the repaint rectangle for the whole 2048 board.
 *
 * The first playable version repaints the full board after accepted moves.
 * That is still much smaller than a full-screen refresh and keeps dirty area
 * management straightforward until cell-level invalidation is added later.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering the visible board.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Game_GetBoardRect(void)
{
    UI_Rect rect;

    rect.x = PAGE_GAME_BOARD_X;
    rect.y = PAGE_GAME_BOARD_Y;
    rect.w = PAGE_GAME_BOARD_SIZE;
    rect.h = PAGE_GAME_BOARD_SIZE;

    return rect;
}

/*
 * Build the repaint rectangle for one board cell.
 *
 * The rectangle includes only the tile square, not the surrounding board gap.
 * Neighboring changed cells are expanded into row spans before invalidation so
 * the renderer normally sees at most four board dirty rectangles.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * Rectangle covering the tile square.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Game_GetCellRect(uint8_t row, uint8_t col)
{
    UI_Rect rect;

    rect.x = (int16_t)(PAGE_GAME_BOARD_X + PAGE_GAME_GAP +
        ((int16_t)col * (PAGE_GAME_CELL + PAGE_GAME_GAP)));
    rect.y = (int16_t)(PAGE_GAME_BOARD_Y + PAGE_GAME_GAP +
        ((int16_t)row * (PAGE_GAME_CELL + PAGE_GAME_GAP)));
    rect.w = PAGE_GAME_CELL;
    rect.h = PAGE_GAME_CELL;

    return rect;
}

/*
 * Mark the score bar dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the top status area.
 */
static void Page_Game_InvalidateScore(void)
{
    UI_PageInvalidateXYWH(0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_STATUS_H);
}

/*
 * Mark the full board dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the board.
 */
static void Page_Game_InvalidateBoard(void)
{
    UI_Rect rect;

    rect = Page_Game_GetBoardRect();
    UI_PageInvalidate(&rect);
}

/*
 * Mark the end-state overlay area dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local repaint for the WIN or GAME OVER overlay.
 */
static void Page_Game_InvalidateOverlay(void)
{
    UI_PageInvalidateXYWH(34, 102, 172, 38);
}

/*
 * Mark changed board cells dirty using row spans.
 *
 * The dirty queue can hold only a small fixed number of rectangles. A changed
 * mask may include many cells, so each row is collapsed to the minimal
 * horizontal span that covers its changed cells. Invalid masks fall back to a
 * full board repaint.
 *
 * Parameters:
 * mask: Sixteen-bit changed-cell mask from the game logic.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues up to four local board repaint rectangles.
 */
static void Page_Game_InvalidateChangedCells(uint16_t mask)
{
    uint8_t row;
    uint8_t col;
    uint8_t first_col;
    uint8_t last_col;
    uint8_t row_has_change;
    UI_Rect first_rect;
    UI_Rect last_rect;
    UI_Rect span;

    if (mask == 0U)
    {
        Page_Game_InvalidateBoard();
        return;
    }

    for (row = 0U; row < 4U; row++)
    {
        first_col = 0U;
        last_col = 0U;
        row_has_change = 0U;
        for (col = 0U; col < 4U; col++)
        {
            if ((mask & (uint16_t)(1U << ((row * 4U) + col))) != 0U)
            {
                if (row_has_change == 0U)
                {
                    first_col = col;
                    row_has_change = 1U;
                }
                last_col = col;
            }
        }

        if (row_has_change != 0U)
        {
            first_rect = Page_Game_GetCellRect(row, first_col);
            last_rect = Page_Game_GetCellRect(row, last_col);
            span.x = first_rect.x;
            span.y = first_rect.y;
            span.w = (int16_t)(last_rect.x + last_rect.w - first_rect.x);
            span.h = first_rect.h;
            UI_PageInvalidate(&span);
        }
    }
}

/*
 * Start the short visual feedback for a generated tile.
 *
 * If a previous new-tile highlight is still visible, its mask is returned so
 * the caller can merge that cleanup with the normal changed-cell redraw. This
 * keeps board dirty work bounded to row spans instead of adding separate
 * cleanup rectangles.
 *
 * Parameters:
 * mask: Sixteen-bit mask containing the generated tile.
 * now: Timestamp of the move that generated the tile.
 *
 * Return value:
 * Previous active new-tile mask that should be redrawn normally.
 *
 * Side effects:
 * Updates flash state.
 */
static uint16_t Page_Game_StartNewTileFeedback(uint16_t mask, uint32_t now)
{
    uint16_t old_mask;

    old_mask = g_page_game_new_flash_mask;
    g_page_game_new_flash_mask = mask;
    if (mask != 0U)
    {
        g_page_game_new_flash_until_ms = now + PAGE_GAME_NEW_FLASH_MS;
    }
    else
    {
        g_page_game_new_flash_until_ms = 0U;
    }

    return old_mask;
}

/*
 * Convert an unsigned 32-bit value to decimal ASCII.
 *
 * The firmware avoids formatted stdio to keep code size predictable. Values
 * wider than the output buffer are clamped by truncating the most significant
 * decimal digits that do not fit.
 *
 * Parameters:
 * value: Value to format.
 * buffer: Destination buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes a terminated string when buffer_size is at least two.
 */
static void Page_Game_FormatU32(uint32_t value, char *buffer, uint8_t buffer_size)
{
    char digits[10];
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
        value /= 10U;
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
 * Convert a tile exponent into display text.
 *
 * Empty cells return an empty string. Non-empty cells are converted by shifting
 * from 1 so exponent 1 becomes 2 and exponent 11 becomes 2048.
 *
 * Parameters:
 * exponent: Board cell exponent.
 * buffer: Destination buffer.
 * buffer_size: Number of bytes in buffer.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes buffer.
 */
static void Page_Game_FormatTile(uint8_t exponent, char *buffer, uint8_t buffer_size)
{
    uint32_t value;

    if ((buffer == 0) || (buffer_size == 0U))
    {
        return;
    }

    if (exponent == 0U)
    {
        buffer[0] = '\0';
        return;
    }

    value = 1UL << exponent;
    Page_Game_FormatU32(value, buffer, buffer_size);
}

/*
 * Measure an ASCII string for the available game fonts.
 *
 * Parameters:
 * text: Null-terminated ASCII string.
 * large: Nonzero to measure large text cells, zero for normal cells.
 *
 * Return value:
 * Pixel width used by the text.
 *
 * Side effects:
 * None.
 */
static int16_t Page_Game_TextWidth(const char *text, uint8_t large)
{
    int16_t width;
    int16_t step;

    width = 0;
    step = (large != 0U) ? 12 : 8;
    while ((text != 0) && (*text != '\0'))
    {
        width = (int16_t)(width + step);
        text++;
    }

    return width;
}

/*
 * Draw ASCII text centered inside a rectangle.
 *
 * Large text is used for short tile numbers, while normal text keeps four
 * digit values readable inside the 39-pixel cell without overflowing.
 *
 * Parameters:
 * rect: Rectangle that owns the centered text.
 * text: Null-terminated ASCII text.
 * color: RGB565 text color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws clipped text.
 */
static void Page_Game_DrawTextCentered(const UI_Rect *rect, const char *text, uint16_t color)
{
    uint8_t large;
    int16_t text_w;
    int16_t text_h;
    int16_t x;
    int16_t y;

    if ((rect == 0) || (text == 0) || (*text == '\0'))
    {
        return;
    }

    large = (Page_Game_TextWidth(text, 1U) <= (rect->w - 6)) ? 1U : 0U;
    text_w = Page_Game_TextWidth(text, large);
    text_h = (large != 0U) ? 18 : 12;
    x = (int16_t)(rect->x + ((rect->w - text_w) / 2));
    y = (int16_t)(rect->y + ((rect->h - text_h) / 2));

    if (large != 0U)
    {
        UI_DrawTextLarge(x, y, text, color);
    }
    else
    {
        UI_DrawText(x, y, text, color);
    }
}

/*
 * Select an RGB565 fill color for a tile exponent.
 *
 * The table uses a compact high-contrast palette that remains readable on the
 * 240x240 ST7789. Exponents above the table reuse the final color.
 *
 * Parameters:
 * exponent: Board cell exponent.
 *
 * Return value:
 * RGB565 tile fill color.
 *
 * Side effects:
 * None.
 */
static uint16_t Page_Game_GetTileColor(uint8_t exponent)
{
    static const uint16_t colors[] =
    {
        0x2104U,
        0xCE79U,
        0xE71CU,
        0xFD20U,
        0xFB00U,
        0xF9E0U,
        0xF800U,
        0xFFE0U,
        0xFEA0U,
        0x07FFU,
        0x07E0U,
        0xFFFFU
    };

    if (exponent >= (uint8_t)(sizeof(colors) / sizeof(colors[0])))
    {
        exponent = (uint8_t)((sizeof(colors) / sizeof(colors[0])) - 1U);
    }

    return colors[exponent];
}

/*
 * Draw the score and best-score header.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the status bar and score text.
 */
static void Page_Game_DrawHeader(void)
{
    char score_text[11];
    char best_text[11];

    Page_Game_FormatU32(Game2048_GetScore(), score_text, (uint8_t)sizeof(score_text));
    Page_Game_FormatU32(Game2048_GetBestScore(), best_text, (uint8_t)sizeof(best_text));

    UI_DrawStatusBar(TEXT_GAME_TITLE, UI_COLOR_ACCENT);
    UI_DrawText(74, 6, "S", UI_COLOR_BG);
    UI_DrawText(90, 6, score_text, UI_COLOR_BG);
    UI_DrawText(150, 6, "B", UI_COLOR_BG);
    UI_DrawText(166, 6, best_text, UI_COLOR_BG);
}

/*
 * Draw one tile cell.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints one tile background and number.
 */
static void Page_Game_DrawCell(uint8_t row, uint8_t col)
{
    UI_Rect rect;
    char tile_text[6];
    uint8_t exponent;
    uint8_t flash_active;
    uint16_t fill;
    uint16_t text_color;

    rect = Page_Game_GetCellRect(row, col);
    exponent = Game2048_GetCell(row, col);
    flash_active = ((g_page_game_new_flash_mask &
        (uint16_t)(1U << ((row * 4U) + col))) != 0U) ? 1U : 0U;
    if (flash_active != 0U)
    {
        fill = UI_COLOR_WARN;
        text_color = UI_COLOR_BG;
    }
    else
    {
        fill = Page_Game_GetTileColor(exponent);
        text_color = (exponent <= 2U) ? UI_COLOR_BG : UI_COLOR_TEXT;
    }

    UI_DrawRect(rect.x, rect.y, rect.w, rect.h, fill);
    UI_DrawFrame(rect.x, rect.y, rect.w, rect.h,
        (flash_active != 0U) ? UI_COLOR_ACCENT : UI_COLOR_DIM);
    if (flash_active != 0U)
    {
        UI_DrawFrame(
            (int16_t)(rect.x + 1),
            (int16_t)(rect.y + 1),
            (int16_t)(rect.w - 2),
            (int16_t)(rect.h - 2),
            UI_COLOR_BG
        );
    }
    Page_Game_FormatTile(exponent, tile_text, (uint8_t)sizeof(tile_text));
    Page_Game_DrawTextCentered(&rect, tile_text, text_color);
}

/*
 * Draw the full 2048 board.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints board background and all 16 cells.
 */
static void Page_Game_DrawBoard(void)
{
    uint8_t row;
    uint8_t col;

    UI_DrawRect(
        PAGE_GAME_BOARD_X,
        PAGE_GAME_BOARD_Y,
        PAGE_GAME_BOARD_SIZE,
        PAGE_GAME_BOARD_SIZE,
        UI_COLOR_DIM
    );

    for (row = 0U; row < 4U; row++)
    {
        for (col = 0U; col < 4U; col++)
        {
            Page_Game_DrawCell(row, col);
        }
    }
}

/*
 * Draw the end-state overlay.
 *
 * WIN and GAME OVER are drawn over the board with a compact dark band so the
 * current final board remains partly visible around it. The footer still shows
 * the restart and back controls.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May draw a status overlay on top of the board.
 */
static void Page_Game_DrawStateOverlay(void)
{
    Game2048_State state;

    state = Game2048_GetState();
    if (state == GAME2048_STATE_PLAYING)
    {
        return;
    }

    UI_DrawRect(34, 102, 172, 38, UI_COLOR_BG);
    UI_DrawFrame(34, 102, 172, 38, UI_COLOR_ACCENT);
    if (state == GAME2048_STATE_WIN)
    {
        UI_DrawTextCN(82, 112, "WIN", UI_COLOR_WARN);
    }
    else
    {
        UI_DrawTextCN(58, 112, "GAME OVER", UI_COLOR_DANGER);
    }
}

/*
 * Restart the GAME page.
 *
 * Parameters:
 * seed: Entropy used by the game RNG.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets game state and requests a full page redraw.
 */
static void Page_Game_Restart(uint32_t seed)
{
    Game2048_Restart(seed);
    g_page_game_new_flash_mask = 0U;
    g_page_game_new_flash_until_ms = 0U;
    g_page_game_last_input_ms = seed;
    g_page_game_last_restart_ms = seed;
    g_page_game_input_locked = 1U;
    UI_PageRequestRedraw();
}

/*
 * Restart the GAME page from an external menu.
 *
 * The pause page uses this helper before returning to GAME so the board reset
 * and GAME input guards are kept in the same state as an in-game OK restart.
 *
 * Parameters:
 * seed: Entropy used by the game RNG.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets the board, refresh timing guards, and queues a GAME redraw.
 */
void Page_Game_RestartFromMenu(uint32_t seed)
{
    Page_Game_Restart(seed);
}

/*
 * Enter the GAME page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Initializes the game once per power cycle and holds game input until the
 * first GAME-page redraw has drained.
 */
static void Page_Game_OnEnter(void)
{
    uint32_t now;

    now = Timing_GetTick();
    if (g_page_game_initialized == 0U)
    {
        Game2048_Init(now);
        g_page_game_initialized = 1U;
    }
    if ((g_page_game_new_flash_mask != 0U) &&
        ((int32_t)(now - g_page_game_new_flash_until_ms) >= 0))
    {
        g_page_game_new_flash_mask = 0U;
        g_page_game_new_flash_until_ms = 0U;
    }
    g_page_game_input_locked = 1U;
}

/*
 * Handle one GAME page event.
 *
 * Direction keys slide the board with a small throttle so key repeats cannot
 * flood the dirty queue, including attempts that do not change the board. OK
 * accepts only the first press and locks further input until the restart redraw
 * drains. RST/BACK remains handled by the global page manager before this
 * function is called.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May update game state and queue local redraws.
 */
static void Page_Game_OnEvent(const UI_Event *event)
{
    Game2048_Direction dir;
    uint32_t score_before;
    uint32_t best_before;
    uint16_t old_flash_mask;
    uint16_t redraw_mask;
    uint8_t has_dir;

    if (event == 0)
    {
        return;
    }

    if (event->type == UI_EVENT_OK)
    {
        if (event->source_type != KEY_EVENT_PRESS)
        {
            return;
        }
        if ((g_page_game_input_locked != 0U) || (UI_RendererIsBusy() != 0U))
        {
            return;
        }
        if ((g_page_game_last_restart_ms != 0U) &&
            ((event->timestamp - g_page_game_last_restart_ms) < PAGE_GAME_RESTART_GUARD))
        {
            return;
        }
        Page_Game_Restart(event->timestamp);
        return;
    }

    has_dir = 1U;
    if (event->type == UI_EVENT_UP)
    {
        dir = GAME2048_DIR_UP;
    }
    else if (event->type == UI_EVENT_DOWN)
    {
        dir = GAME2048_DIR_DOWN;
    }
    else if (event->type == UI_EVENT_LEFT)
    {
        dir = GAME2048_DIR_LEFT;
    }
    else if (event->type == UI_EVENT_RIGHT)
    {
        dir = GAME2048_DIR_RIGHT;
    }
    else
    {
        has_dir = 0U;
        dir = GAME2048_DIR_UP;
    }

    if (has_dir == 0U)
    {
        return;
    }

    if ((g_page_game_input_locked != 0U) || (UI_RendererIsBusy() != 0U))
    {
        return;
    }

    if ((g_page_game_last_input_ms != 0U) &&
        ((event->timestamp - g_page_game_last_input_ms) < PAGE_GAME_INPUT_THROTTLE))
    {
        return;
    }

    g_page_game_last_input_ms = event->timestamp;
    score_before = Game2048_GetScore();
    best_before = Game2048_GetBestScore();
    if (Game2048_Move(dir) != 0U)
    {
        g_page_game_input_locked = 1U;
        old_flash_mask = Page_Game_StartNewTileFeedback(
            Game2048_GetLastNewTileMask(),
            event->timestamp
        );
        if ((Game2048_GetScore() != score_before) ||
            (Game2048_GetBestScore() != best_before))
        {
            Page_Game_InvalidateScore();
        }
        redraw_mask = (uint16_t)(Game2048_GetLastChangeMask() | old_flash_mask);
        Page_Game_InvalidateChangedCells(redraw_mask);
        if (Game2048_GetState() != GAME2048_STATE_PLAYING)
        {
            Page_Game_InvalidateOverlay();
        }
    }
}

/*
 * Run periodic GAME page work.
 *
 * New-tile feedback is cleared here after its visible window expires. Reaching
 * this task means the page manager has already drained pending renderer work,
 * so the game can safely accept the next move or restart when no cleanup
 * repaint needs to be queued.
 *
 * Parameters:
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the cached draw timestamp.
 */
static void Page_Game_Task(uint32_t now)
{
    g_page_game_draw_now = now;
    if ((g_page_game_new_flash_mask != 0U) &&
        ((int32_t)(now - g_page_game_new_flash_until_ms) >= 0))
    {
        Page_Game_InvalidateChangedCells(g_page_game_new_flash_mask);
        g_page_game_new_flash_mask = 0U;
        g_page_game_new_flash_until_ms = 0U;
        g_page_game_input_locked = 1U;
        return;
    }
    g_page_game_input_locked = 0U;
}

/*
 * Draw the GAME page within the requested clip.
 *
 * Parameters:
 * clip: Dirty rectangle currently being repainted.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the intersecting GAME page content.
 */
static void Page_Game_Draw(const UI_Rect *clip)
{
    (void)clip;
    (void)g_page_game_draw_now;

    UI_DrawClearClip(UI_COLOR_BG);
    Page_Game_DrawHeader();
    Page_Game_DrawBoard();
    Page_Game_DrawStateOverlay();
    UI_DrawFooter(TEXT_FOOTER_GAME);
}

const UI_PageOps PAGE_GAME_OPS =
{
    Page_Game_OnEnter,
    Page_Game_OnEvent,
    Page_Game_Task,
    Page_Game_Draw
};

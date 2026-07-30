#include "page_game.h"
#include "game_2048.h"
#include "timing.h"
#include "ui_draw.h"
#include "ui_feedback.h"

#define PAGE_GAME_BOARD_X          32      // Board left coordinate.
#define PAGE_GAME_BOARD_Y          34      // Board top coordinate.
#define PAGE_GAME_CELL             39      // Tile square size.
#define PAGE_GAME_GAP               4      // Gap between tiles.
#define PAGE_GAME_BOARD_SIZE      176      // Full board area including gaps.
#define PAGE_GAME_MOVE_THROTTLE    90U     // Minimum time between accepted moves.
#define TEXT_GAME_TITLE          "2048"
#define TEXT_FOOTER_GAME         "JOY MOVE  OK NEW  RST BACK"

static uint8_t g_page_game_initialized = 0U;
static uint32_t g_page_game_last_move_ms = 0U;
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
    uint16_t fill;
    uint16_t text_color;

    rect.x = (int16_t)(PAGE_GAME_BOARD_X + PAGE_GAME_GAP +
        ((int16_t)col * (PAGE_GAME_CELL + PAGE_GAME_GAP)));
    rect.y = (int16_t)(PAGE_GAME_BOARD_Y + PAGE_GAME_GAP +
        ((int16_t)row * (PAGE_GAME_CELL + PAGE_GAME_GAP)));
    rect.w = PAGE_GAME_CELL;
    rect.h = PAGE_GAME_CELL;

    exponent = Game2048_GetCell(row, col);
    fill = Page_Game_GetTileColor(exponent);
    text_color = (exponent <= 2U) ? UI_COLOR_BG : UI_COLOR_TEXT;

    UI_DrawRect(rect.x, rect.y, rect.w, rect.h, fill);
    UI_DrawFrame(rect.x, rect.y, rect.w, rect.h, UI_COLOR_DIM);
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
    g_page_game_last_move_ms = 0U;
    UI_PageRequestRedraw();
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
 * Initializes the game once per power cycle and requests a redraw through the
 * page manager.
 */
static void Page_Game_OnEnter(void)
{
    if (g_page_game_initialized == 0U)
    {
        Game2048_Init(Timing_GetTick());
        g_page_game_initialized = 1U;
    }
}

/*
 * Handle one GAME page event.
 *
 * Direction keys slide the board with a small throttle so key repeats cannot
 * flood the dirty queue. OK restarts the game. RST/BACK remains handled by the
 * global page manager before this function is called.
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
    uint8_t has_dir;

    if (event == 0)
    {
        return;
    }

    if (event->type == UI_EVENT_OK)
    {
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

    if ((g_page_game_last_move_ms != 0U) &&
        ((event->timestamp - g_page_game_last_move_ms) < PAGE_GAME_MOVE_THROTTLE))
    {
        return;
    }

    if (Game2048_Move(dir) != 0U)
    {
        g_page_game_last_move_ms = event->timestamp;
        Page_Game_InvalidateScore();
        Page_Game_InvalidateBoard();
    }
}

/*
 * Run periodic GAME page work.
 *
 * The first version has no animation. The timestamp is cached so feedback and
 * later animation additions have a stable draw-time value available.
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

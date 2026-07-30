#include "page_snake.h"
#include "game_snake.h"
#include "timing.h"
#include "ui_draw.h"
#include "ui_renderer.h"

#define PAGE_SNAKE_BOARD_X       32       // Board left coordinate.
#define PAGE_SNAKE_BOARD_Y       34       // Board top coordinate.
#define PAGE_SNAKE_BOARD_SIZE   176       // Full 16x16 board area.
#define PAGE_SNAKE_CELL          10       // Snake cell square size.
#define PAGE_SNAKE_STEP          11       // Cell pitch including one-pixel gap.
#define PAGE_SNAKE_TICK_MS      180U      // Snake movement interval.
#define PAGE_SNAKE_RESTART_GUARD 300U     // Minimum time between OK restarts.
#define TEXT_SNAKE_TITLE       "SNAKE"
#define TEXT_SNAKE_FOOTER_RUN  "JOY TURN  OK NEW  SET PAUSE"
#define TEXT_SNAKE_FOOTER_STOP "OK NEW  SET RUN  RST BACK"

static uint8_t g_page_snake_initialized = 0U;
static uint32_t g_page_snake_last_step_ms = 0U;
static uint32_t g_page_snake_last_restart_ms = 0U;

/*
 * Build the repaint rectangle for the whole Snake board.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Rectangle covering the visible Snake board.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Snake_GetBoardRect(void)
{
    UI_Rect rect;

    rect.x = PAGE_SNAKE_BOARD_X;
    rect.y = PAGE_SNAKE_BOARD_Y;
    rect.w = PAGE_SNAKE_BOARD_SIZE;
    rect.h = PAGE_SNAKE_BOARD_SIZE;

    return rect;
}

/*
 * Build the repaint rectangle for one Snake board cell.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * Rectangle covering the visible cell square.
 *
 * Side effects:
 * None.
 */
static UI_Rect Page_Snake_GetCellRect(uint8_t row, uint8_t col)
{
    UI_Rect rect;

    rect.x = (int16_t)(PAGE_SNAKE_BOARD_X + ((int16_t)col * PAGE_SNAKE_STEP));
    rect.y = (int16_t)(PAGE_SNAKE_BOARD_Y + ((int16_t)row * PAGE_SNAKE_STEP));
    rect.w = PAGE_SNAKE_CELL;
    rect.h = PAGE_SNAKE_CELL;

    return rect;
}

/*
 * Mark Snake cells dirty using row spans.
 *
 * The rule layer exposes one 16-bit mask per Snake row. Each non-empty row is
 * collapsed to the shortest horizontal span covering its dirty cells so normal
 * movement queues only a few local rectangles.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues local Snake board repaint rectangles.
 */
static void Page_Snake_InvalidateChangedCells(void)
{
    uint8_t row;
    uint8_t col;
    uint8_t first_col;
    uint8_t last_col;
    uint8_t row_has_change;
    uint16_t row_mask;
    UI_Rect first_rect;
    UI_Rect last_rect;
    UI_Rect span;

    for (row = 0U; row < SNAKE_GRID_SIZE; row++)
    {
        row_mask = Snake_GetLastDirtyRowMask(row);
        if (row_mask == 0U)
        {
            continue;
        }

        first_col = 0U;
        last_col = 0U;
        row_has_change = 0U;
        for (col = 0U; col < SNAKE_GRID_SIZE; col++)
        {
            if ((row_mask & (uint16_t)(1U << col)) != 0U)
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
            first_rect = Page_Snake_GetCellRect(row, first_col);
            last_rect = Page_Snake_GetCellRect(row, last_col);
            span.x = first_rect.x;
            span.y = first_rect.y;
            span.w = (int16_t)(last_rect.x + last_rect.w - first_rect.x);
            span.h = first_rect.h;
            UI_PageInvalidate(&span);
        }
    }
}

/*
 * Mark the Snake header dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local status-bar repaint.
 */
static void Page_Snake_InvalidateHeader(void)
{
    UI_PageInvalidateXYWH(0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_STATUS_H);
}

/*
 * Mark the Snake footer dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local footer repaint.
 */
static void Page_Snake_InvalidateFooter(void)
{
    UI_PageInvalidateXYWH(
        0,
        (int16_t)(UI_SCREEN_H - UI_FOOTER_H),
        (int16_t)UI_SCREEN_W,
        (int16_t)UI_FOOTER_H
    );
}

/*
 * Mark the Snake pause or game-over overlay dirty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Queues a local overlay repaint.
 */
static void Page_Snake_InvalidateOverlay(void)
{
    UI_PageInvalidateXYWH(42, 102, 156, 38);
}

/*
 * Convert an unsigned 32-bit value to decimal ASCII.
 *
 * Snake avoids formatted stdio for the same code-size reason as the 2048 page.
 * Values wider than the output buffer lose the most significant digits.
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
static void Page_Snake_FormatU32(uint32_t value, char *buffer, uint8_t buffer_size)
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
 * Draw the Snake status header.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints score, best score, and length counters.
 */
static void Page_Snake_DrawHeader(void)
{
    char score_text[11];
    char best_text[11];
    char length_text[6];

    Page_Snake_FormatU32(Snake_GetScore(), score_text, (uint8_t)sizeof(score_text));
    Page_Snake_FormatU32(Snake_GetBestScore(), best_text, (uint8_t)sizeof(best_text));
    Page_Snake_FormatU32(Snake_GetLength(), length_text, (uint8_t)sizeof(length_text));

    UI_DrawStatusBar(TEXT_SNAKE_TITLE, UI_COLOR_OK);
    UI_DrawText(74, 6, "S", UI_COLOR_BG);
    UI_DrawText(88, 6, score_text, UI_COLOR_BG);
    UI_DrawText(130, 6, "B", UI_COLOR_BG);
    UI_DrawText(144, 6, best_text, UI_COLOR_BG);
    UI_DrawText(184, 6, "L", UI_COLOR_BG);
    UI_DrawText(198, 6, length_text, UI_COLOR_BG);
}

/*
 * Select the fill color for a Snake cell.
 *
 * Parameters:
 * cell: Cell content from the Snake rule layer.
 *
 * Return value:
 * RGB565 fill color.
 *
 * Side effects:
 * None.
 */
static uint16_t Page_Snake_GetCellColor(Snake_Cell cell)
{
    if (cell == SNAKE_CELL_HEAD)
    {
        return UI_COLOR_WARN;
    }
    if (cell == SNAKE_CELL_BODY)
    {
        return UI_COLOR_OK;
    }
    if (cell == SNAKE_CELL_FOOD)
    {
        return UI_COLOR_DANGER;
    }

    return UI_COLOR_SURFACE;
}

/*
 * Draw one Snake board cell.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints one grid cell.
 */
static void Page_Snake_DrawCell(uint8_t row, uint8_t col)
{
    Snake_Cell cell;
    UI_Rect rect;

    cell = Snake_GetCell(row, col);
    rect = Page_Snake_GetCellRect(row, col);
    UI_DrawRect(rect.x, rect.y, rect.w, rect.h, Page_Snake_GetCellColor(cell));
}

/*
 * Draw the full Snake board grid.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints board background and all 16x16 cells.
 */
static void Page_Snake_DrawBoard(void)
{
    uint8_t row;
    uint8_t col;
    UI_Rect rect;

    rect = Page_Snake_GetBoardRect();
    UI_DrawRect(
        rect.x,
        rect.y,
        rect.w,
        rect.h,
        UI_COLOR_DIM
    );

    for (row = 0U; row < SNAKE_GRID_SIZE; row++)
    {
        for (col = 0U; col < SNAKE_GRID_SIZE; col++)
        {
            Page_Snake_DrawCell(row, col);
        }
    }
}

/*
 * Draw Snake pause and game-over overlays.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May draw a compact state band over the board.
 */
static void Page_Snake_DrawOverlay(void)
{
    Snake_State state;

    state = Snake_GetState();
    if (state == SNAKE_STATE_RUNNING)
    {
        return;
    }

    UI_DrawRect(42, 102, 156, 38, UI_COLOR_BG);
    UI_DrawFrame(42, 102, 156, 38,
        (state == SNAKE_STATE_OVER) ? UI_COLOR_DANGER : UI_COLOR_WARN);
    if (state == SNAKE_STATE_OVER)
    {
        UI_DrawTextCN(50, 112, "GAME OVER", UI_COLOR_DANGER);
    }
    else
    {
        UI_DrawTextCN(74, 112, "PAUSED", UI_COLOR_WARN);
    }
}

/*
 * Restart the Snake page.
 *
 * Parameters:
 * seed: Entropy used by the Snake RNG.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets the Snake board and requests a full redraw.
 */
static void Page_Snake_Restart(uint32_t seed)
{
    Snake_Restart(seed);
    g_page_snake_last_step_ms = seed;
    g_page_snake_last_restart_ms = seed;
    UI_PageRequestRedraw();
}

/*
 * Enter the Snake page.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Initializes Snake once per power cycle and resets the movement timer.
 */
static void Page_Snake_OnEnter(void)
{
    uint32_t now;

    now = Timing_GetTick();
    if (g_page_snake_initialized == 0U)
    {
        Snake_Init(now);
        g_page_snake_initialized = 1U;
    }
    g_page_snake_last_step_ms = now;
}

/*
 * Handle one Snake page event.
 *
 * Direction keys steer the snake. OK restarts the current Snake board. SET is
 * delivered as UI_EVENT_SETTINGS by the global dispatcher and toggles pause
 * while the Snake page is active.
 *
 * Parameters:
 * event: UI event after global routing.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May update direction, pause state, or restart the board.
 */
static void Page_Snake_OnEvent(const UI_Event *event)
{
    Snake_State state_before;

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
        if ((g_page_snake_last_restart_ms != 0U) &&
            ((event->timestamp - g_page_snake_last_restart_ms) < PAGE_SNAKE_RESTART_GUARD))
        {
            return;
        }
        Page_Snake_Restart(event->timestamp);
        return;
    }

    if (event->type == UI_EVENT_SETTINGS)
    {
        state_before = Snake_GetState();
        Snake_TogglePause();
        if (Snake_GetState() != state_before)
        {
            Page_Snake_InvalidateOverlay();
            Page_Snake_InvalidateFooter();
        }
        return;
    }

    if (event->type == UI_EVENT_UP)
    {
        Snake_SetDirection(SNAKE_DIR_UP);
        return;
    }
    if (event->type == UI_EVENT_DOWN)
    {
        Snake_SetDirection(SNAKE_DIR_DOWN);
        return;
    }
    if (event->type == UI_EVENT_LEFT)
    {
        Snake_SetDirection(SNAKE_DIR_LEFT);
        return;
    }
    if (event->type == UI_EVENT_RIGHT)
    {
        Snake_SetDirection(SNAKE_DIR_RIGHT);
    }
}

/*
 * Run periodic Snake movement.
 *
 * The page manager calls this only when the renderer is idle, so a step cannot
 * queue new dirty work while the previous board repaint is still draining.
 *
 * Parameters:
 * now: Current system tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May advance the snake and queue changed-cell, header, or footer repaints.
 */
static void Page_Snake_Task(uint32_t now)
{
    uint32_t score_before;
    Snake_State state_before;

    if ((uint32_t)(now - g_page_snake_last_step_ms) < PAGE_SNAKE_TICK_MS)
    {
        return;
    }

    g_page_snake_last_step_ms = now;
    score_before = Snake_GetScore();
    state_before = Snake_GetState();
    if (Snake_Step() == 0U)
    {
        return;
    }

    Page_Snake_InvalidateChangedCells();
    if (Snake_GetScore() != score_before)
    {
        Page_Snake_InvalidateHeader();
    }
    if (Snake_GetState() != state_before)
    {
        Page_Snake_InvalidateOverlay();
        Page_Snake_InvalidateFooter();
    }
}

/*
 * Draw the Snake page within the requested clip.
 *
 * Parameters:
 * clip: Dirty rectangle currently being repainted.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the intersecting Snake page content.
 */
static void Page_Snake_Draw(const UI_Rect *clip)
{
    (void)clip;

    UI_DrawClearClip(UI_COLOR_BG);
    Page_Snake_DrawHeader();
    Page_Snake_DrawBoard();
    Page_Snake_DrawOverlay();
    UI_DrawFooter(
        (Snake_GetState() == SNAKE_STATE_RUNNING) ?
        TEXT_SNAKE_FOOTER_RUN :
        TEXT_SNAKE_FOOTER_STOP
    );
}

const UI_PageOps PAGE_SNAKE_OPS =
{
    Page_Snake_OnEnter,
    Page_Snake_OnEvent,
    Page_Snake_Task,
    Page_Snake_Draw
};

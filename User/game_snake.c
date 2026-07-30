#include "game_snake.h"

#define SNAKE_START_LENGTH      4U        // Initial body length.
#define SNAKE_SCORE_PER_FOOD   10U        // Score awarded for one food.

static uint8_t g_snake_body_row[SNAKE_MAX_CELLS];
static uint8_t g_snake_body_col[SNAKE_MAX_CELLS];
static uint16_t g_snake_length = 0U;
static uint8_t g_snake_food_row = 0U;
static uint8_t g_snake_food_col = 0U;
static Snake_Direction g_snake_dir = SNAKE_DIR_RIGHT;
static Snake_Direction g_snake_pending_dir = SNAKE_DIR_RIGHT;
static Snake_State g_snake_state = SNAKE_STATE_RUNNING;
static uint32_t g_snake_score = 0U;
static uint32_t g_snake_best_score = 0U;
static uint32_t g_snake_rng_state = 1U;
static uint16_t g_snake_last_dirty_rows[SNAKE_GRID_SIZE];

/*
 * Advance the Snake pseudo-random generator.
 *
 * The generator is local to Snake so food placement does not disturb other
 * games. A compact LCG is enough because it only selects among empty cells.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Next pseudo-random 32-bit value.
 *
 * Side effects:
 * Updates the module RNG state.
 */
static uint32_t Snake_Rand(void)
{
    g_snake_rng_state = (g_snake_rng_state * 1664525UL) + 1013904223UL;
    return g_snake_rng_state;
}

/*
 * Clear all recorded Snake dirty cells.
 *
 * The dirty map contains one 16-bit mask per board row. It is cleared before a
 * step records the cells that changed during that movement.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears g_snake_last_dirty_rows.
 */
static void Snake_ClearDirtyRows(void)
{
    uint8_t row;

    for (row = 0U; row < SNAKE_GRID_SIZE; row++)
    {
        g_snake_last_dirty_rows[row] = 0U;
    }
}

/*
 * Mark one Snake board cell as visually changed.
 *
 * The page layer later turns row masks into row-span dirty rectangles so a
 * timer step normally queues only the cells touched by head, tail, and food.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the matching row mask when the coordinate is in range.
 */
static void Snake_MarkDirtyCell(uint8_t row, uint8_t col)
{
    if ((row >= SNAKE_GRID_SIZE) || (col >= SNAKE_GRID_SIZE))
    {
        return;
    }

    g_snake_last_dirty_rows[row] |= (uint16_t)(1U << col);
}

/*
 * Test whether a coordinate matches one snake body entry.
 *
 * The caller provides the number of entries to test so movement collision can
 * ignore the tail cell when the snake is not growing and that tail will move
 * away during the same step.
 *
 * Parameters:
 * row: Candidate row.
 * col: Candidate column.
 * count: Number of body entries to test from the head.
 *
 * Return value:
 * 1: The coordinate is occupied by the tested snake body range.
 * 0: The coordinate is free within that range.
 *
 * Side effects:
 * None.
 */
static uint8_t Snake_IsBodyCell(uint8_t row, uint8_t col, uint16_t count)
{
    uint16_t index;

    if (count > g_snake_length)
    {
        count = g_snake_length;
    }

    for (index = 0U; index < count; index++)
    {
        if ((g_snake_body_row[index] == row) &&
            (g_snake_body_col[index] == col))
        {
            return 1U;
        }
    }

    return 0U;
}

/*
 * Count free cells that can receive new food.
 *
 * Food placement scans the fixed grid directly instead of allocating a list of
 * empty cells. This keeps RAM usage deterministic on STM32F103C8T6.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Number of empty cells not occupied by the snake.
 *
 * Side effects:
 * None.
 */
static uint16_t Snake_CountEmptyCells(void)
{
    uint8_t row;
    uint8_t col;
    uint16_t count;

    count = 0U;
    for (row = 0U; row < SNAKE_GRID_SIZE; row++)
    {
        for (col = 0U; col < SNAKE_GRID_SIZE; col++)
        {
            if (Snake_IsBodyCell(row, col, g_snake_length) == 0U)
            {
                count++;
            }
        }
    }

    return count;
}

/*
 * Place food on one random empty board cell.
 *
 * A random empty-cell ordinal is selected first and then resolved by scanning
 * the board. This avoids a 256-entry temporary coordinate list.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: Food was placed.
 * 0: No empty cell exists.
 *
 * Side effects:
 * Updates food coordinates and advances the RNG.
 */
static uint8_t Snake_PlaceFood(void)
{
    uint8_t row;
    uint8_t col;
    uint16_t empty_count;
    uint16_t target;
    uint16_t seen;

    empty_count = Snake_CountEmptyCells();
    if (empty_count == 0U)
    {
        return 0U;
    }

    target = (uint16_t)(Snake_Rand() % empty_count);
    seen = 0U;
    for (row = 0U; row < SNAKE_GRID_SIZE; row++)
    {
        for (col = 0U; col < SNAKE_GRID_SIZE; col++)
        {
            if (Snake_IsBodyCell(row, col, g_snake_length) == 0U)
            {
                if (seen == target)
                {
                    g_snake_food_row = row;
                    g_snake_food_col = col;
                    Snake_MarkDirtyCell(row, col);
                    return 1U;
                }
                seen++;
            }
        }
    }

    return 0U;
}

/*
 * Test whether a direction is the reverse of the current direction.
 *
 * Direct reversal would make the head collide with the first body segment, so
 * it is ignored to match normal Snake controls.
 *
 * Parameters:
 * dir: Candidate direction from input.
 *
 * Return value:
 * 1: Direction is opposite to current movement.
 * 0: Direction can be accepted.
 *
 * Side effects:
 * None.
 */
static uint8_t Snake_IsReverseDirection(Snake_Direction dir)
{
    if ((g_snake_dir == SNAKE_DIR_UP) && (dir == SNAKE_DIR_DOWN))
    {
        return 1U;
    }
    if ((g_snake_dir == SNAKE_DIR_DOWN) && (dir == SNAKE_DIR_UP))
    {
        return 1U;
    }
    if ((g_snake_dir == SNAKE_DIR_LEFT) && (dir == SNAKE_DIR_RIGHT))
    {
        return 1U;
    }
    if ((g_snake_dir == SNAKE_DIR_RIGHT) && (dir == SNAKE_DIR_LEFT))
    {
        return 1U;
    }

    return 0U;
}

/*
 * Initialize the Snake game module.
 *
 * Best score is cleared once at module initialization. Later restarts keep it
 * for the current power cycle, matching the 2048 behavior.
 *
 * Parameters:
 * seed: Startup entropy from the caller.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets best score and starts a fresh Snake board.
 */
void Snake_Init(uint32_t seed)
{
    g_snake_best_score = 0U;
    Snake_Restart(seed);
}

/*
 * Restart the current Snake board.
 *
 * The initial snake is centered and points right. Food is placed after the body
 * exists so it cannot spawn inside the starting snake.
 *
 * Parameters:
 * seed: Caller-provided entropy, usually a millisecond tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets snake body, score, direction, state, RNG, and food.
 */
void Snake_Restart(uint32_t seed)
{
    uint16_t index;

    g_snake_rng_state = (seed == 0U) ? 1U : seed;
    g_snake_rng_state ^= 0x3C6EF35FUL;
    g_snake_length = SNAKE_START_LENGTH;
    g_snake_score = 0U;
    g_snake_dir = SNAKE_DIR_RIGHT;
    g_snake_pending_dir = SNAKE_DIR_RIGHT;
    g_snake_state = SNAKE_STATE_RUNNING;
    Snake_ClearDirtyRows();

    for (index = 0U; index < SNAKE_START_LENGTH; index++)
    {
        g_snake_body_row[index] = (uint8_t)(SNAKE_GRID_SIZE / 2U);
        g_snake_body_col[index] = (uint8_t)((SNAKE_GRID_SIZE / 2U) - index);
    }

    (void)Snake_PlaceFood();
}

/*
 * Set the pending movement direction.
 *
 * Direction changes are applied on the next step. Opposite directions are
 * ignored so rapid key input cannot force an immediate self-collision.
 *
 * Parameters:
 * dir: Requested direction.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May update the pending direction.
 */
void Snake_SetDirection(Snake_Direction dir)
{
    if (g_snake_state == SNAKE_STATE_OVER)
    {
        return;
    }
    if (Snake_IsReverseDirection(dir) != 0U)
    {
        return;
    }

    g_snake_pending_dir = dir;
}

/*
 * Toggle between running and paused states.
 *
 * Game over cannot be resumed through pause; OK restart is the only way to
 * clear an ended Snake board.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May update the game state.
 */
void Snake_TogglePause(void)
{
    if (g_snake_state == SNAKE_STATE_RUNNING)
    {
        g_snake_state = SNAKE_STATE_PAUSED;
        return;
    }
    if (g_snake_state == SNAKE_STATE_PAUSED)
    {
        g_snake_state = SNAKE_STATE_RUNNING;
    }
}

/*
 * Advance the snake by one timer step.
 *
 * The head moves first, then collision and growth are resolved. When food is
 * eaten, the tail is retained and a new food cell is placed if space remains.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: The visible board or state changed.
 * 0: The snake was not running.
 *
 * Side effects:
 * May move the snake, update score, place food, or enter game over.
 */
uint8_t Snake_Step(void)
{
    uint16_t index;
    uint16_t collision_count;
    uint8_t new_row;
    uint8_t new_col;
    uint8_t old_head_row;
    uint8_t old_head_col;
    uint8_t old_tail_row;
    uint8_t old_tail_col;
    uint8_t grow;

    Snake_ClearDirtyRows();
    if (g_snake_state != SNAKE_STATE_RUNNING)
    {
        return 0U;
    }

    g_snake_dir = g_snake_pending_dir;
    new_row = g_snake_body_row[0];
    new_col = g_snake_body_col[0];
    old_head_row = g_snake_body_row[0];
    old_head_col = g_snake_body_col[0];
    old_tail_row = g_snake_body_row[g_snake_length - 1U];
    old_tail_col = g_snake_body_col[g_snake_length - 1U];
    switch (g_snake_dir)
    {
        case SNAKE_DIR_UP:
            if (new_row == 0U)
            {
                g_snake_state = SNAKE_STATE_OVER;
                return 1U;
            }
            new_row--;
            break;

        case SNAKE_DIR_DOWN:
            new_row++;
            if (new_row >= SNAKE_GRID_SIZE)
            {
                g_snake_state = SNAKE_STATE_OVER;
                return 1U;
            }
            break;

        case SNAKE_DIR_LEFT:
            if (new_col == 0U)
            {
                g_snake_state = SNAKE_STATE_OVER;
                return 1U;
            }
            new_col--;
            break;

        default:
            new_col++;
            if (new_col >= SNAKE_GRID_SIZE)
            {
                g_snake_state = SNAKE_STATE_OVER;
                return 1U;
            }
            break;
    }

    grow = ((new_row == g_snake_food_row) && (new_col == g_snake_food_col)) ? 1U : 0U;
    collision_count = (grow != 0U) ? g_snake_length : (uint16_t)(g_snake_length - 1U);
    if (Snake_IsBodyCell(new_row, new_col, collision_count) != 0U)
    {
        g_snake_state = SNAKE_STATE_OVER;
        return 1U;
    }

    if ((grow != 0U) && (g_snake_length < SNAKE_MAX_CELLS))
    {
        g_snake_length++;
    }

    index = g_snake_length - 1U;
    while (index > 0U)
    {
        g_snake_body_row[index] = g_snake_body_row[index - 1U];
        g_snake_body_col[index] = g_snake_body_col[index - 1U];
        index--;
    }
    g_snake_body_row[0] = new_row;
    g_snake_body_col[0] = new_col;
    Snake_MarkDirtyCell(old_head_row, old_head_col);
    Snake_MarkDirtyCell(new_row, new_col);
    if (grow == 0U)
    {
        Snake_MarkDirtyCell(old_tail_row, old_tail_col);
    }

    if (grow != 0U)
    {
        g_snake_score += SNAKE_SCORE_PER_FOOD;
        if (g_snake_score > g_snake_best_score)
        {
            g_snake_best_score = g_snake_score;
        }
        if (Snake_PlaceFood() == 0U)
        {
            g_snake_state = SNAKE_STATE_OVER;
        }
    }

    return 1U;
}

/*
 * Read the current Snake state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current Snake state.
 *
 * Side effects:
 * None.
 */
Snake_State Snake_GetState(void)
{
    return g_snake_state;
}

/*
 * Read the current Snake score.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current score for this board.
 *
 * Side effects:
 * None.
 */
uint32_t Snake_GetScore(void)
{
    return g_snake_score;
}

/*
 * Read the best Snake score for this power cycle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Highest Snake score since Snake_Init.
 *
 * Side effects:
 * None.
 */
uint32_t Snake_GetBestScore(void)
{
    return g_snake_best_score;
}

/*
 * Read the current snake length.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Number of body cells occupied by the snake.
 *
 * Side effects:
 * None.
 */
uint16_t Snake_GetLength(void)
{
    return g_snake_length;
}

/*
 * Read one row of the dirty-cell map from the last Snake step.
 *
 * Bit 0 maps to column 0 and bit 15 maps to column 15. The mask is retained
 * until the next Snake_Step call so the page can schedule local repaint spans.
 *
 * Parameters:
 * row: Board row index.
 *
 * Return value:
 * Sixteen-bit dirty-cell mask for the requested row, or zero.
 *
 * Side effects:
 * None.
 */
uint16_t Snake_GetLastDirtyRowMask(uint8_t row)
{
    if (row >= SNAKE_GRID_SIZE)
    {
        return 0U;
    }

    return g_snake_last_dirty_rows[row];
}

/*
 * Read the visual content of one Snake board cell.
 *
 * Out-of-range coordinates are treated as empty so drawing code cannot read
 * outside the fixed board arrays.
 *
 * Parameters:
 * row: Board row index.
 * col: Board column index.
 *
 * Return value:
 * Cell content for rendering.
 *
 * Side effects:
 * None.
 */
Snake_Cell Snake_GetCell(uint8_t row, uint8_t col)
{
    if ((row >= SNAKE_GRID_SIZE) || (col >= SNAKE_GRID_SIZE))
    {
        return SNAKE_CELL_EMPTY;
    }
    if ((g_snake_body_row[0] == row) && (g_snake_body_col[0] == col))
    {
        return SNAKE_CELL_HEAD;
    }
    if ((g_snake_food_row == row) && (g_snake_food_col == col))
    {
        return SNAKE_CELL_FOOD;
    }
    if (Snake_IsBodyCell(row, col, g_snake_length) != 0U)
    {
        return SNAKE_CELL_BODY;
    }

    return SNAKE_CELL_EMPTY;
}

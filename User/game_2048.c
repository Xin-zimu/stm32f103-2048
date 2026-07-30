#include "game_2048.h"

#define GAME2048_SIZE             4U      // Board edge length in cells.
#define GAME2048_START_TILES      2U      // Tiles placed on a fresh board.
#define GAME2048_WIN_EXP         11U      // 2^11 is 2048.

static uint8_t g_game2048_board[GAME2048_SIZE][GAME2048_SIZE];
static uint32_t g_game2048_score = 0U;
static uint32_t g_game2048_best_score = 0U;
static uint32_t g_game2048_rng_state = 1U;
static uint16_t g_game2048_last_change_mask = 0U;
static uint16_t g_game2048_last_new_tile_mask = 0U;
static Game2048_State g_game2048_state = GAME2048_STATE_PLAYING;

/*
 * Advance the fixed LCG random generator.
 *
 * The game only needs a small deterministic source for choosing empty cells
 * and deciding whether a new tile is 2 or 4. Keeping the generator local avoids
 * libc dependencies and keeps the firmware free of dynamic allocation.
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
static uint32_t Game2048_Rand(void)
{
    g_game2048_rng_state = (g_game2048_rng_state * 1103515245UL) + 12345UL;
    return g_game2048_rng_state;
}

/*
 * Clear all cells and runtime status for a fresh game.
 *
 * The best score is intentionally not reset here so repeated restarts from the
 * GAME page keep the best value visible during the current power cycle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears the board, score, and state.
 */
static void Game2048_ClearBoard(void)
{
    uint8_t row;
    uint8_t col;

    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            g_game2048_board[row][col] = 0U;
        }
    }

    g_game2048_score = 0U;
    g_game2048_state = GAME2048_STATE_PLAYING;
}

/*
 * Count empty board cells.
 *
 * Empty cells are stored as exponent zero. The count is used before adding a
 * new tile so the generator never has to loop indefinitely on a full board.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Number of empty cells.
 *
 * Side effects:
 * None.
 */
static uint8_t Game2048_CountEmpty(void)
{
    uint8_t row;
    uint8_t col;
    uint8_t count;

    count = 0U;
    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            if (g_game2048_board[row][col] == 0U)
            {
                count++;
            }
        }
    }

    return count;
}

/*
 * Add one random tile to an empty cell.
 *
 * A target empty-cell ordinal is chosen first, then the board is scanned in a
 * stable order until that empty slot is reached. This avoids allocating a list
 * of empty coordinates. New tiles follow normal 2048 odds. When requested, the
 * tile location is recorded for short visual feedback on the GAME page.
 *
 * Parameters:
 * track_new_tile: Nonzero to record the generated tile in the new-tile mask.
 *
 * Return value:
 * 1: A tile was added.
 * 0: The board was already full.
 *
 * Side effects:
 * Writes one board cell, advances the RNG, and may update the new-tile mask.
 */
static uint8_t Game2048_AddRandomTile(uint8_t track_new_tile)
{
    uint8_t empty_count;
    uint8_t target;
    uint8_t seen;
    uint8_t row;
    uint8_t col;
    uint8_t value;

    empty_count = Game2048_CountEmpty();
    if (empty_count == 0U)
    {
        return 0U;
    }

    target = (uint8_t)(Game2048_Rand() % empty_count);
    value = ((Game2048_Rand() % 10U) == 0U) ? 2U : 1U;
    seen = 0U;

    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            if (g_game2048_board[row][col] == 0U)
            {
                if (seen == target)
                {
                    g_game2048_board[row][col] = value;
                    if (track_new_tile != 0U)
                    {
                        g_game2048_last_new_tile_mask =
                            (uint16_t)(1U << ((row * GAME2048_SIZE) + col));
                    }
                    return 1U;
                }
                seen++;
            }
        }
    }

    return 0U;
}

/*
 * Test whether any board cell has reached 2048.
 *
 * The board stores exponents, so the win condition is exponent 11 instead of
 * comparing against the literal value 2048.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: At least one 2048 tile is present.
 * 0: No winning tile exists.
 *
 * Side effects:
 * None.
 */
static uint8_t Game2048_CheckWin(void)
{
    uint8_t row;
    uint8_t col;

    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            if (g_game2048_board[row][col] >= GAME2048_WIN_EXP)
            {
                return 1U;
            }
        }
    }

    return 0U;
}

/*
 * Test whether the board has no legal move left.
 *
 * A board is over only when no empty cell exists and no horizontal or vertical
 * neighbor pair can merge. Direction probing is avoided so this check does not
 * disturb score, RNG state, or the board contents.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: No legal move remains.
 * 0: The player can still move or merge.
 *
 * Side effects:
 * None.
 */
static uint8_t Game2048_CheckOver(void)
{
    uint8_t row;
    uint8_t col;

    if (Game2048_CountEmpty() != 0U)
    {
        return 0U;
    }

    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            if ((col < (GAME2048_SIZE - 1U)) &&
                (g_game2048_board[row][col] == g_game2048_board[row][col + 1U]))
            {
                return 0U;
            }
            if ((row < (GAME2048_SIZE - 1U)) &&
                (g_game2048_board[row][col] == g_game2048_board[row + 1U][col]))
            {
                return 0U;
            }
        }
    }

    return 1U;
}

/*
 * Build a bit mask from cells that changed since a saved snapshot.
 *
 * Bit 0 maps to row 0 column 0, bit 1 maps to row 0 column 1, and bit 15 maps
 * to row 3 column 3. The page layer uses this compact mask to request local
 * redraws without knowing how a move was resolved internally.
 *
 * Parameters:
 * previous: Board snapshot captured before a move.
 *
 * Return value:
 * Sixteen-bit mask of cells whose exponent changed.
 *
 * Side effects:
 * None.
 */
static uint16_t Game2048_BuildChangeMask(const uint8_t previous[GAME2048_SIZE][GAME2048_SIZE])
{
    uint8_t row;
    uint8_t col;
    uint8_t bit;
    uint16_t mask;

    mask = 0U;
    bit = 0U;
    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            if (previous[row][col] != g_game2048_board[row][col])
            {
                mask |= (uint16_t)(1U << bit);
            }
            bit++;
        }
    }

    return mask;
}

/*
 * Apply one 2048 merge pass to a four-cell line.
 *
 * The line is already ordered from the movement edge outward. Nonzero cells
 * are compacted, equal neighbors merge once, and the tail is filled with zero.
 * The caller writes the transformed line back using the direction-specific
 * board order.
 *
 * Parameters:
 * line: Four exponents ordered toward the move direction.
 * score_delta: Receives the score added by merges in this line.
 *
 * Return value:
 * 1: The line content changed.
 * 0: The line stayed identical.
 *
 * Side effects:
 * Rewrites line.
 */
static uint8_t Game2048_ApplyLine(uint8_t line[GAME2048_SIZE], uint32_t *score_delta)
{
    uint8_t compact[GAME2048_SIZE];
    uint8_t merged[GAME2048_SIZE];
    uint8_t original[GAME2048_SIZE];
    uint8_t read_index;
    uint8_t write_index;
    uint8_t changed;

    for (read_index = 0U; read_index < GAME2048_SIZE; read_index++)
    {
        original[read_index] = line[read_index];
        compact[read_index] = 0U;
        merged[read_index] = 0U;
    }

    write_index = 0U;
    for (read_index = 0U; read_index < GAME2048_SIZE; read_index++)
    {
        if (line[read_index] != 0U)
        {
            compact[write_index] = line[read_index];
            write_index++;
        }
    }

    write_index = 0U;
    read_index = 0U;
    while (read_index < GAME2048_SIZE)
    {
        if ((compact[read_index] != 0U) &&
            ((read_index + 1U) < GAME2048_SIZE) &&
            (compact[read_index] == compact[read_index + 1U]))
        {
            merged[write_index] = (uint8_t)(compact[read_index] + 1U);
            if (score_delta != 0)
            {
                *score_delta += (1UL << merged[write_index]);
            }
            read_index = (uint8_t)(read_index + 2U);
        }
        else
        {
            merged[write_index] = compact[read_index];
            read_index++;
        }
        write_index++;
    }

    changed = 0U;
    for (read_index = 0U; read_index < GAME2048_SIZE; read_index++)
    {
        line[read_index] = merged[read_index];
        if (line[read_index] != original[read_index])
        {
            changed = 1U;
        }
    }

    return changed;
}

/*
 * Extract one row or column into move-direction order.
 *
 * The merge helper only understands a line whose index zero is closest to the
 * movement edge. This function maps the 2D board into that canonical line for
 * all four directions.
 *
 * Parameters:
 * dir: Movement direction.
 * index: Row or column index selected by dir.
 * line: Receives four exponents in movement order.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes line.
 */
static void Game2048_ReadLine(Game2048_Direction dir, uint8_t index, uint8_t line[GAME2048_SIZE])
{
    uint8_t cell;

    for (cell = 0U; cell < GAME2048_SIZE; cell++)
    {
        if (dir == GAME2048_DIR_UP)
        {
            line[cell] = g_game2048_board[cell][index];
        }
        else if (dir == GAME2048_DIR_DOWN)
        {
            line[cell] = g_game2048_board[GAME2048_SIZE - 1U - cell][index];
        }
        else if (dir == GAME2048_DIR_LEFT)
        {
            line[cell] = g_game2048_board[index][cell];
        }
        else
        {
            line[cell] = g_game2048_board[index][GAME2048_SIZE - 1U - cell];
        }
    }
}

/*
 * Write one transformed line back to the board.
 *
 * The line remains in movement-edge order, so the mapping mirrors
 * Game2048_ReadLine. Keeping this mapping in one place avoids duplicated
 * direction-specific loops inside the main move routine.
 *
 * Parameters:
 * dir: Movement direction.
 * index: Row or column index selected by dir.
 * line: Four exponents in movement order.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes four board cells.
 */
static void Game2048_WriteLine(Game2048_Direction dir, uint8_t index, const uint8_t line[GAME2048_SIZE])
{
    uint8_t cell;

    for (cell = 0U; cell < GAME2048_SIZE; cell++)
    {
        if (dir == GAME2048_DIR_UP)
        {
            g_game2048_board[cell][index] = line[cell];
        }
        else if (dir == GAME2048_DIR_DOWN)
        {
            g_game2048_board[GAME2048_SIZE - 1U - cell][index] = line[cell];
        }
        else if (dir == GAME2048_DIR_LEFT)
        {
            g_game2048_board[index][cell] = line[cell];
        }
        else
        {
            g_game2048_board[index][GAME2048_SIZE - 1U - cell] = line[cell];
        }
    }
}

/*
 * Initialize the 2048 game module.
 *
 * The seed is normalized away from zero because the LCG is used immediately to
 * select the starting tiles. The best score is cleared only at full module
 * initialization, matching a RAM-only best score for this first firmware
 * version.
 *
 * Parameters:
 * seed: Startup entropy from the caller.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets board, score, best score, state, and RNG state.
 */
void Game2048_Init(uint32_t seed)
{
    g_game2048_best_score = 0U;
    Game2048_Restart(seed);
}

/*
 * Restart the current game while preserving the best score.
 *
 * A fresh board always receives two start tiles. The seed is mixed with a fixed
 * nonzero value so repeated restarts from different key timestamps produce
 * different boards while a zero timestamp still works.
 *
 * Parameters:
 * seed: Caller-provided entropy, usually a millisecond tick.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Resets board, score, state, and RNG state.
 */
void Game2048_Restart(uint32_t seed)
{
    uint8_t count;

    g_game2048_rng_state = (seed == 0U) ? 1U : seed;
    g_game2048_rng_state ^= 0xA5A55A5AUL;
    g_game2048_last_new_tile_mask = 0U;
    Game2048_ClearBoard();

    for (count = 0U; count < GAME2048_START_TILES; count++)
    {
        (void)Game2048_AddRandomTile(0U);
    }
    g_game2048_last_change_mask = 0xFFFFU;
}

/*
 * Move the board once in the requested direction.
 *
 * All four rows or columns are transformed before a random tile is added. A
 * new tile appears only when at least one line changed, matching normal 2048
 * behavior for invalid moves.
 *
 * Parameters:
 * dir: Direction to slide and merge tiles.
 *
 * Return value:
 * 1: The board changed and a new tile was generated.
 * 0: The board did not change or the game is not accepting moves.
 *
 * Side effects:
 * May update board cells, score, best score, state, and RNG state.
 */
uint8_t Game2048_Move(Game2048_Direction dir)
{
    uint8_t index;
    uint8_t line[GAME2048_SIZE];
    uint8_t previous[GAME2048_SIZE][GAME2048_SIZE];
    uint8_t row;
    uint8_t col;
    uint8_t changed;
    uint32_t score_delta;

    g_game2048_last_change_mask = 0U;
    g_game2048_last_new_tile_mask = 0U;
    if (g_game2048_state != GAME2048_STATE_PLAYING)
    {
        return 0U;
    }

    for (row = 0U; row < GAME2048_SIZE; row++)
    {
        for (col = 0U; col < GAME2048_SIZE; col++)
        {
            previous[row][col] = g_game2048_board[row][col];
        }
    }

    changed = 0U;
    score_delta = 0U;
    for (index = 0U; index < GAME2048_SIZE; index++)
    {
        Game2048_ReadLine(dir, index, line);
        if (Game2048_ApplyLine(line, &score_delta) != 0U)
        {
            changed = 1U;
        }
        Game2048_WriteLine(dir, index, line);
    }

    if (changed == 0U)
    {
        return 0U;
    }

    g_game2048_score += score_delta;
    if (g_game2048_score > g_game2048_best_score)
    {
        g_game2048_best_score = g_game2048_score;
    }

    (void)Game2048_AddRandomTile(1U);
    if (Game2048_CheckWin() != 0U)
    {
        g_game2048_state = GAME2048_STATE_WIN;
    }
    else if (Game2048_CheckOver() != 0U)
    {
        g_game2048_state = GAME2048_STATE_OVER;
    }

    g_game2048_last_change_mask = Game2048_BuildChangeMask(previous);
    return 1U;
}

/*
 * Read the current game state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current game state.
 *
 * Side effects:
 * None.
 */
Game2048_State Game2048_GetState(void)
{
    return g_game2048_state;
}

/*
 * Read whether the game is in the win state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: Game state is WIN.
 * 0: Game state is not WIN.
 *
 * Side effects:
 * None.
 */
uint8_t Game2048_IsWin(void)
{
    return (g_game2048_state == GAME2048_STATE_WIN) ? 1U : 0U;
}

/*
 * Read whether the game is in the over state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: Game state is OVER.
 * 0: Game state is not OVER.
 *
 * Side effects:
 * None.
 */
uint8_t Game2048_IsOver(void)
{
    return (g_game2048_state == GAME2048_STATE_OVER) ? 1U : 0U;
}

/*
 * Read the current score.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current score accumulated from merges.
 *
 * Side effects:
 * None.
 */
uint32_t Game2048_GetScore(void)
{
    return g_game2048_score;
}

/*
 * Read the best score for this power cycle.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Highest score reached since Game2048_Init.
 *
 * Side effects:
 * None.
 */
uint32_t Game2048_GetBestScore(void)
{
    return g_game2048_best_score;
}

/*
 * Read the cell-change mask produced by the last move or restart.
 *
 * The mask is retained until the next call to Game2048_Move or
 * Game2048_Restart. It lets the page layer repaint only the rows and cells that
 * visibly changed after the game logic has added the new random tile.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Sixteen-bit changed-cell mask.
 *
 * Side effects:
 * None.
 */
uint16_t Game2048_GetLastChangeMask(void)
{
    return g_game2048_last_change_mask;
}

/*
 * Read the new-tile mask produced by the last successful move.
 *
 * Only the random tile generated after a valid move is reported. Initial
 * restart tiles are not reported so entering or restarting the game does not
 * play a misleading new-tile flash on a full-page redraw.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Sixteen-bit mask containing the most recently generated tile, or zero.
 *
 * Side effects:
 * None.
 */
uint16_t Game2048_GetLastNewTileMask(void)
{
    return g_game2048_last_new_tile_mask;
}

/*
 * Read one board cell.
 *
 * Cells are returned as exponents: 0 is empty, 1 is tile 2, 2 is tile 4, and
 * so on. Out-of-range coordinates return empty so drawing code cannot read
 * outside the fixed 4x4 board.
 *
 * Parameters:
 * row: Board row index, 0 to 3.
 * col: Board column index, 0 to 3.
 *
 * Return value:
 * Cell exponent, or 0 for invalid coordinates.
 *
 * Side effects:
 * None.
 */
uint8_t Game2048_GetCell(uint8_t row, uint8_t col)
{
    if ((row >= GAME2048_SIZE) || (col >= GAME2048_SIZE))
    {
        return 0U;
    }

    return g_game2048_board[row][col];
}

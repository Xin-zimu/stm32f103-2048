#ifndef __GAME_SNAKE_H
#define __GAME_SNAKE_H

#include "stm32f10x.h"

#define SNAKE_GRID_SIZE        16U        // Snake board edge length in cells.
#define SNAKE_MAX_CELLS       256U       // Maximum snake length on a 16x16 board.

typedef enum
{
    SNAKE_DIR_UP = 0,                    // Move the snake toward the top edge.
    SNAKE_DIR_DOWN,                      // Move the snake toward the bottom edge.
    SNAKE_DIR_LEFT,                      // Move the snake toward the left edge.
    SNAKE_DIR_RIGHT                      // Move the snake toward the right edge.
} Snake_Direction;

typedef enum
{
    SNAKE_STATE_RUNNING = 0,             // Snake advances on each tick.
    SNAKE_STATE_PAUSED,                  // Board is frozen until resumed.
    SNAKE_STATE_OVER                     // Collision ended the current board.
} Snake_State;

typedef enum
{
    SNAKE_CELL_EMPTY = 0,                // Empty board cell.
    SNAKE_CELL_BODY,                     // Snake body segment.
    SNAKE_CELL_HEAD,                     // Snake head.
    SNAKE_CELL_FOOD                      // Food cell.
} Snake_Cell;

void Snake_Init(uint32_t seed);
void Snake_Restart(uint32_t seed);
void Snake_SetDirection(Snake_Direction dir);
void Snake_TogglePause(void);
uint8_t Snake_Step(void);
Snake_State Snake_GetState(void);
uint32_t Snake_GetScore(void);
uint32_t Snake_GetBestScore(void);
uint16_t Snake_GetLength(void);
uint16_t Snake_GetLastDirtyRowMask(uint8_t row);
Snake_Cell Snake_GetCell(uint8_t row, uint8_t col);

#endif

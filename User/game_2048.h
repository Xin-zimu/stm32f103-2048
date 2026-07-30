#ifndef __GAME_2048_H
#define __GAME_2048_H

#include "stm32f10x.h"

typedef enum
{
    GAME2048_DIR_UP = 0,                 // Slide all tiles toward the top edge.
    GAME2048_DIR_DOWN,                   // Slide all tiles toward the bottom edge.
    GAME2048_DIR_LEFT,                   // Slide all tiles toward the left edge.
    GAME2048_DIR_RIGHT                   // Slide all tiles toward the right edge.
} Game2048_Direction;

typedef enum
{
    GAME2048_STATE_PLAYING = 0,          // Board accepts direction moves.
    GAME2048_STATE_WIN,                  // A 2048 tile has been reached.
    GAME2048_STATE_OVER                  // No empty cells or legal merges remain.
} Game2048_State;

void Game2048_Init(uint32_t seed);
void Game2048_Restart(uint32_t seed);
uint8_t Game2048_Move(Game2048_Direction dir);
Game2048_State Game2048_GetState(void);
uint8_t Game2048_IsWin(void);
uint8_t Game2048_IsOver(void);
uint32_t Game2048_GetScore(void);
uint32_t Game2048_GetBestScore(void);
uint16_t Game2048_GetLastChangeMask(void);
uint16_t Game2048_GetLastNewTileMask(void);
uint8_t Game2048_GetCell(uint8_t row, uint8_t col);

#endif

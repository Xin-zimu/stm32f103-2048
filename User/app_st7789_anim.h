#ifndef __APP_ST7789_ANIM_H
#define __APP_ST7789_ANIM_H

#include "stm32f10x.h"

void App_ST7789_AnimInit(void);
void App_ST7789_AnimTask(void);
void App_ST7789_AnimRestart(void);
void App_ST7789_AnimTogglePaused(void);

#endif

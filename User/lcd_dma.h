#ifndef __LCD_DMA_H
#define __LCD_DMA_H

#include "stm32f10x.h"

typedef enum
{
    LCD_DMA_STATE_IDLE = 0,             // DMA channel can accept a transfer.
    LCD_DMA_STATE_STARTING,             // Transfer registers are being prepared.
    LCD_DMA_STATE_SENDING,              // SPI2 TX DMA is moving bytes.
    LCD_DMA_STATE_COMPLETE_PENDING,     // ISR saw completion; task must release state.
    LCD_DMA_STATE_ERROR                 // ISR saw an error; task must recover state.
} LCD_DMA_State;

typedef void (*LCD_DMA_Callback)(void *user_data);

typedef struct
{
    uint16_t x;                         // Reserved target X for future renderer users.
    uint16_t y;                         // Reserved target Y for future renderer users.
    uint16_t width;                     // Reserved target width.
    uint16_t height;                    // Reserved target height.
    const uint8_t *data;                // First byte to send through SPI2.
    uint32_t data_length;               // Number of bytes to send.
    LCD_DMA_Callback callback;          // Optional completion callback in main context.
    void *user_data;                    // Optional callback context.
} LCD_DMA_Transfer;

void LCD_DMA_Init(void);
uint8_t LCD_DMA_IsBusy(void);
LCD_DMA_State LCD_DMA_GetState(void);
uint8_t LCD_DMA_Start(const LCD_DMA_Transfer *transfer);
void LCD_DMA_Task(void);
void LCD_DMA_WaitReady(void);
void LCD_DMA_OnSpiCompleteISR(void);
void LCD_DMA_OnSpiErrorISR(void);

#endif

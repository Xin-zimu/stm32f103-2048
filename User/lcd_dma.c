#include "lcd_dma.h"
#include "app_config.h"

#define LCD_DMA_CHANNEL          DMA1_Channel5       // SPI2 TX DMA channel.
#define LCD_DMA_IRQ              DMA1_Channel5_IRQn   // SPI2 TX DMA interrupt.
#define LCD_DMA_CLEAR_IT         DMA1_IT_GL5          // Clears all channel 5 interrupt bits.
#define LCD_DMA_TC_IT            DMA1_IT_TC5          // Transfer complete interrupt bit.
#define LCD_DMA_TE_IT            DMA1_IT_TE5          // Transfer error interrupt bit.

typedef struct
{
    volatile LCD_DMA_State state;        // Current DMA state shared with ISR.
    volatile uint8_t complete_flag;      // Set by ISR when a transfer completes.
    volatile uint8_t error_flag;         // Set by ISR when a transfer error occurs.
    LCD_DMA_Callback callback;           // Completion callback executed in task context.
    void *user_data;                     // Callback context.
} LCD_DMA_Context;

static LCD_DMA_Context g_lcd_dma;

/*
 * Recover the SPI2 TX DMA request path after a transfer error.
 *
 * The recovery is intentionally local to the DMA module: it disables the
 * channel, clears pending channel interrupt bits, toggles the SPI2 TX DMA
 * request, and returns the state machine to IDLE so the next producer can
 * retry without permanently blocking the display pipeline.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Reinitializes DMA1 Channel5 software and hardware request state.
 */
static void LCD_DMA_Recover(void)
{
    DMA_Cmd(LCD_DMA_CHANNEL, DISABLE);
    DMA_ClearITPendingBit(LCD_DMA_CLEAR_IT);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, DISABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    g_lcd_dma.complete_flag = 0U;
    g_lcd_dma.error_flag = 0U;
    g_lcd_dma.callback = 0;
    g_lcd_dma.user_data = 0;
    g_lcd_dma.state = LCD_DMA_STATE_IDLE;
}

/*
 * Configure DMA1 Channel5 for SPI2 TX transfers.
 *
 * SPI2 itself is configured by the ST7789 driver before this function is
 * called. This module owns the DMA channel, its interrupt priority, pending
 * flags, and the SPI2 TX DMA request enable so display producers do not touch
 * channel registers directly.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Enables DMA1 clock, configures DMA1 Channel5 and its IRQ, and resets state.
 */
void LCD_DMA_Init(void)
{
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(LCD_DMA_CHANNEL);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
    dma.DMA_MemoryBaseAddr = 0U;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = 1U;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(LCD_DMA_CHANNEL, &dma);
    DMA_ITConfig(LCD_DMA_CHANNEL, DMA_IT_TC | DMA_IT_TE, ENABLE);
    DMA_ClearITPendingBit(LCD_DMA_CLEAR_IT);

    nvic.NVIC_IRQChannel = LCD_DMA_IRQ;
    nvic.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic.NVIC_IRQChannelSubPriority = 1U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    g_lcd_dma.state = LCD_DMA_STATE_IDLE;
    g_lcd_dma.complete_flag = 0U;
    g_lcd_dma.error_flag = 0U;
    g_lcd_dma.callback = 0;
    g_lcd_dma.user_data = 0;
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
}

/*
 * Report whether the SPI2 TX DMA state machine can accept work.
 *
 * COMPLETE_PENDING and ERROR are still considered busy because the cooperative
 * task must release or recover the channel before another transfer can be
 * submitted. Call LCD_DMA_Task before polling when running in a synchronous
 * compatibility path.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: DMA is not IDLE.
 * 0: DMA is IDLE.
 *
 * Side effects:
 * None.
 */
uint8_t LCD_DMA_IsBusy(void)
{
    return (g_lcd_dma.state == LCD_DMA_STATE_IDLE) ? 0U : 1U;
}

/*
 * Read the current LCD DMA state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current DMA state.
 *
 * Side effects:
 * None.
 */
LCD_DMA_State LCD_DMA_GetState(void)
{
    return g_lcd_dma.state;
}

/*
 * Start one SPI2 TX DMA transfer.
 *
 * The caller must already have selected the ST7789 address window and data
 * mode. This function only owns the byte transfer. Invalid buffers, zero
 * lengths, lengths above the DMA counter limit, or a busy channel are rejected
 * immediately so callers can return to the main loop instead of waiting.
 *
 * Parameters:
 * transfer: Descriptor containing the data pointer and byte length.
 *
 * Return value:
 * 1: Transfer was accepted and started.
 * 0: Parameters were invalid or DMA was not idle.
 *
 * Side effects:
 * Reprograms DMA1 Channel5 and marks the state SENDING.
 */
uint8_t LCD_DMA_Start(const LCD_DMA_Transfer *transfer)
{
    if ((transfer == 0) || (transfer->data == 0) ||
        (transfer->data_length == 0U) ||
        (transfer->data_length > APP_LCD_DMA_MAX_BYTES))
    {
        return 0U;
    }

    LCD_DMA_Task();
    if (g_lcd_dma.state != LCD_DMA_STATE_IDLE)
    {
        return 0U;
    }

    g_lcd_dma.state = LCD_DMA_STATE_STARTING;
    g_lcd_dma.complete_flag = 0U;
    g_lcd_dma.error_flag = 0U;
    g_lcd_dma.callback = transfer->callback;
    g_lcd_dma.user_data = transfer->user_data;

    DMA_Cmd(LCD_DMA_CHANNEL, DISABLE);
    DMA_ClearITPendingBit(LCD_DMA_CLEAR_IT);
    LCD_DMA_CHANNEL->CMAR = (uint32_t)transfer->data;
    LCD_DMA_CHANNEL->CNDTR = (uint16_t)transfer->data_length;
    g_lcd_dma.state = LCD_DMA_STATE_SENDING;
    DMA_Cmd(LCD_DMA_CHANNEL, ENABLE);

    return 1U;
}

/*
 * Advance completion and error handling outside the interrupt.
 *
 * The interrupt only records the hardware result and disables the channel.
 * This task releases COMPLETE_PENDING transfers, executes any completion
 * callback in main context, and performs recovery for ERROR without doing
 * display drawing or data decoding in the ISR.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May change the DMA state and execute a registered callback.
 */
void LCD_DMA_Task(void)
{
    LCD_DMA_Callback callback;
    void *user_data;

    if (g_lcd_dma.state == LCD_DMA_STATE_ERROR)
    {
        LCD_DMA_Recover();
        return;
    }

    if ((g_lcd_dma.state == LCD_DMA_STATE_COMPLETE_PENDING) ||
        (g_lcd_dma.complete_flag != 0U))
    {
        callback = g_lcd_dma.callback;
        user_data = g_lcd_dma.user_data;
        g_lcd_dma.complete_flag = 0U;
        g_lcd_dma.callback = 0;
        g_lcd_dma.user_data = 0;
        g_lcd_dma.state = LCD_DMA_STATE_IDLE;
        if (callback != 0)
        {
            callback(user_data);
        }
    }
}

/*
 * Wait until the DMA module is idle for legacy synchronous draw calls.
 *
 * Current ST7789 rectangle and image helpers still expose a blocking API. They
 * use this compatibility function while the UI renderer is being prepared.
 * The loop always runs LCD_DMA_Task before sleeping so a COMPLETE_PENDING
 * state cannot deadlock after its interrupt has already fired.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May sleep with WFI until DMA1 Channel5 raises an interrupt.
 */
void LCD_DMA_WaitReady(void)
{
    while (1)
    {
        LCD_DMA_Task();
        if (LCD_DMA_IsBusy() == 0U)
        {
            return;
        }
        __WFI();
    }
}

/*
 * Record a SPI2 TX DMA completion from the interrupt context.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Disables DMA1 Channel5, clears pending bits, and marks completion pending.
 */
void LCD_DMA_OnSpiCompleteISR(void)
{
    DMA_Cmd(LCD_DMA_CHANNEL, DISABLE);
    DMA_ClearITPendingBit(LCD_DMA_CLEAR_IT);
    g_lcd_dma.complete_flag = 1U;
    g_lcd_dma.state = LCD_DMA_STATE_COMPLETE_PENDING;
}

/*
 * Record a SPI2 TX DMA error from the interrupt context.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Disables DMA1 Channel5, clears pending bits, and marks recovery required.
 */
void LCD_DMA_OnSpiErrorISR(void)
{
    DMA_Cmd(LCD_DMA_CHANNEL, DISABLE);
    DMA_ClearITPendingBit(LCD_DMA_CLEAR_IT);
    g_lcd_dma.error_flag = 1U;
    g_lcd_dma.state = LCD_DMA_STATE_ERROR;
}

/*
 * Handle the SPI2 TX DMA interrupt vector.
 *
 * STM32F103 maps SPI2_TX to DMA1 Channel5. The vector separates transfer
 * errors from normal completions, but both paths remain intentionally short
 * and defer recovery or callbacks to LCD_DMA_Task.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates the LCD DMA state seen by the main loop.
 */
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA_GetITStatus(LCD_DMA_TE_IT) != RESET)
    {
        LCD_DMA_OnSpiErrorISR();
    }
    else if (DMA_GetITStatus(LCD_DMA_TC_IT) != RESET)
    {
        LCD_DMA_OnSpiCompleteISR();
    }
}

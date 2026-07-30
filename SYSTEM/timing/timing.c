#include "timing.h"

static volatile uint32_t g_ms_tick = 0U;

/*
 * Configure TIM3 as a free-running one millisecond application time base.
 *
 * APB1 timer clock is 72 MHz in this project. A prescaler of 72 and a period
 * of 1000 produce a 1 kHz update interrupt without changing the SysTick
 * peripheral used by the existing blocking delay driver.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Enables the TIM3 clock, interrupt, NVIC channel, and counter.
 */
void Timing_Init(void)
{
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef interrupt;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    timer.TIM_Period = 1000U - 1U;
    timer.TIM_Prescaler = 72U - 1U;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    interrupt.NVIC_IRQChannel = TIM3_IRQn;
    interrupt.NVIC_IRQChannelPreemptionPriority = 1U;
    interrupt.NVIC_IRQChannelSubPriority = 1U;
    interrupt.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&interrupt);

    TIM_Cmd(TIM3, ENABLE);
}

/*
 * Increment the millisecond counter from the TIM3 update interrupt.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Advances the volatile 32-bit application time base by one millisecond.
 */
void Timing_IncTick(void)
{
    g_ms_tick++;
}

/*
 * Read the current millisecond application time.
 *
 * A naturally aligned 32-bit load is atomic on Cortex-M3. Callers should use
 * unsigned subtraction so scheduling remains valid across counter wraparound.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Milliseconds elapsed since Timing_Init enabled TIM3.
 *
 * Side effects:
 * None.
 */
uint32_t Timing_GetTick(void)
{
    return g_ms_tick;
}

/*
 * Handle the TIM3 update interrupt and advance the application time base.
 *
 * The pending flag is cleared before incrementing the counter so a delayed
 * handler cannot immediately retrigger from the same update event.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Clears TIM3 update status and increments the millisecond tick.
 */
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        Timing_IncTick();
    }
}

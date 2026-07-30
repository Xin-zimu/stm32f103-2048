#include "stm32f10x.h"

/*
 * Reset the MCU after an unrecoverable Cortex-M3 fault.
 *
 * Continuing after a hard, memory, bus, or usage fault can leave SPI and GPIO
 * in an undefined state. A system reset gives the driver a clean recovery
 * path instead of using the startup file's default infinite fault loop.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * This function does not return when the reset request succeeds.
 *
 * Side effects:
 * Requests a full Cortex-M3 system reset.
 */
static void Fault_RequestReset(void)
{
    NVIC_SystemReset();
}

/*
 * Recover from a HardFault by resetting the MCU.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Does not return when reset succeeds.
 *
 * Side effects:
 * Requests a system reset.
 */
void HardFault_Handler(void)
{
    Fault_RequestReset();
}

/*
 * Recover from an MPU memory fault by resetting the MCU.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Does not return when reset succeeds.
 *
 * Side effects:
 * Requests a system reset.
 */
void MemManage_Handler(void)
{
    Fault_RequestReset();
}

/*
 * Recover from a bus fault by resetting the MCU.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Does not return when reset succeeds.
 *
 * Side effects:
 * Requests a system reset.
 */
void BusFault_Handler(void)
{
    Fault_RequestReset();
}

/*
 * Recover from an instruction usage fault by resetting the MCU.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Does not return when reset succeeds.
 *
 * Side effects:
 * Requests a system reset.
 */
void UsageFault_Handler(void)
{
    Fault_RequestReset();
}

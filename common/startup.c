#include <stdint.h>
#include "image.h"

/* Vector table and reset code for the bootloader and the application. The
 * build defines IMAGE_MAGIC and IMAGE_VERSION; the image length in slot 8 is
 * filled in by tools/mkimage.py. */

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);

void Default_Handler(void)
{
    /* Stuck here, the watchdog (where started) resets the unit. */
    for (;;) {
    }
}

void Reset_Handler(void)
{
    const uint32_t *source = &_sidata;
    for (uint32_t *target = &_sdata; target < &_edata;) {
        *target++ = *source++;
    }
    for (uint32_t *target = &_sbss; target < &_ebss;) {
        *target++ = 0u;
    }
    (void)main();
    for (;;) {
    }
}

#define WEAK_HANDLER(name) void name(void) __attribute__((weak, alias("Default_Handler")))

WEAK_HANDLER(NMI_Handler);
WEAK_HANDLER(HardFault_Handler);
WEAK_HANDLER(SVC_Handler);
WEAK_HANDLER(PendSV_Handler);
WEAK_HANDLER(SysTick_Handler);
WEAK_HANDLER(USB_Handler);
WEAK_HANDLER(SERCOM0_Handler);
WEAK_HANDLER(SERCOM1_Handler);
WEAK_HANDLER(SERCOM2_Handler);
WEAK_HANDLER(SERCOM3_Handler);
WEAK_HANDLER(SERCOM4_Handler);

#define VECTOR(function)   ((uint32_t)(uintptr_t)(function))

__attribute__((section(".vectors"), used))
const uint32_t vector_table[16 + 28] = {
    [0]  = VECTOR(&_estack),
    [1]  = VECTOR(Reset_Handler),
    [2]  = VECTOR(NMI_Handler),
    [3]  = VECTOR(HardFault_Handler),
    [IMAGE_VECTOR_MAGIC]   = IMAGE_MAGIC,
    [IMAGE_VECTOR_LENGTH]  = 0u,
    [IMAGE_VECTOR_VERSION] = IMAGE_VERSION,
    [11] = VECTOR(SVC_Handler),
    [14] = VECTOR(PendSV_Handler),
    [15] = VECTOR(SysTick_Handler),
    /* Peripheral interrupts, in IRQn order. */
    [16 + 0] = VECTOR(Default_Handler),
    [16 + 1] = VECTOR(Default_Handler),
    [16 + 2] = VECTOR(Default_Handler),
    [16 + 3] = VECTOR(Default_Handler),
    [16 + 4] = VECTOR(Default_Handler),
    [16 + 5] = VECTOR(Default_Handler),
    [16 + 6] = VECTOR(Default_Handler),
    [16 + 7] = VECTOR(USB_Handler),
    [16 + 8] = VECTOR(Default_Handler),
    [16 + 9] = VECTOR(SERCOM0_Handler),
    [16 + 10] = VECTOR(SERCOM1_Handler),
    [16 + 11] = VECTOR(SERCOM2_Handler),
    [16 + 12] = VECTOR(SERCOM3_Handler),
    [16 + 13] = VECTOR(SERCOM4_Handler),
    [16 + 14] = VECTOR(Default_Handler),
    [16 + 15] = VECTOR(Default_Handler),
    [16 + 16] = VECTOR(Default_Handler),
    [16 + 17] = VECTOR(Default_Handler),
    [16 + 18] = VECTOR(Default_Handler),
    [16 + 19] = VECTOR(Default_Handler),
    [16 + 20] = VECTOR(Default_Handler),
    [16 + 21] = VECTOR(Default_Handler),
    [16 + 22] = VECTOR(Default_Handler),
    [16 + 23] = VECTOR(Default_Handler),
    [16 + 24] = VECTOR(Default_Handler),
    [16 + 25] = VECTOR(Default_Handler),
    [16 + 26] = VECTOR(Default_Handler),
    [16 + 27] = VECTOR(Default_Handler),
};

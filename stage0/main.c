#include <stdint.h>
#include <stdbool.h>
#include "sam.h"
#include "hal.h"
#include "image.h"

/* Stage 0: the only code that is never updated in the field, so it does as
 * little as possible.
 *
 *   1. A valid bootloader image waiting in the staging slot that differs from
 *      the installed one is copied over it. A power cut during the copy leaves
 *      the installed copy invalid and the staged one intact, so the copy is
 *      simply made again on the next reset.
 *   2. Start the bootloader if it is valid, else the application if it is.
 *
 * No interrupts, no .data/.bss, the 8 MHz internal oscillator. */

extern uint32_t _estack;
void Reset_Handler(void);

static void hang(void)
{
    for (;;) {
    }
}

__attribute__((section(".vectors"), used))
const uint32_t vector_table[16] = {
    [0] = (uint32_t)(uintptr_t)&_estack,
    [1] = (uint32_t)(uintptr_t)Reset_Handler,
    [2 ... 15] = (uint32_t)(uintptr_t)hang,
};

static void start(uint32_t base) __attribute__((noreturn));
static void start(uint32_t base)
{
    const uint32_t *vectors = (const uint32_t *)(uintptr_t)base;
    SCB->VTOR = base;
    __asm__ volatile("msr msp, %0\n bx %1" : : "r"(vectors[0]), "r"(vectors[1]));
    __builtin_unreachable();
}

static void install_staged_bootloader(void)
{
    const uint8_t *staged = (const uint8_t *)(uintptr_t)STAGING_BASE;
    const uint8_t *installed = (const uint8_t *)(uintptr_t)BOOT_BASE;

    if (!image_valid(staged, STAGING_SIZE, IMAGE_MAGIC_BOOT)
        || image_length(staged) > BOOT_SIZE - 4u) {
        return;
    }
    if (image_valid(installed, BOOT_SIZE, IMAGE_MAGIC_BOOT)
        && image_length(installed) == image_length(staged)
        && image_stored_crc(installed) == image_stored_crc(staged)) {
        return;
    }
    nvm_write(BOOT_BASE, staged, image_length(staged) + 4u);
}

void Reset_Handler(void)
{
    SYSCTRL->OSC8M.bit.PRESC = 0;    /* 1 MHz -> 8 MHz: the CRCs take a fraction of a second */

    install_staged_bootloader();

    if (image_valid((const uint8_t *)(uintptr_t)BOOT_BASE, BOOT_SIZE, IMAGE_MAGIC_BOOT)) {
        start(BOOT_BASE);
    }
    if (image_valid((const uint8_t *)(uintptr_t)APP_BASE, APP_SIZE, IMAGE_MAGIC_APP)) {
        start(APP_BASE);
    }
    hang();
}

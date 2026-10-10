#include "board.h"
#include "hal.h"

#define PAGE_BYTES  FLASH_PAGE_SIZE   /* 64; a row is four pages */

static void command(uint32_t code)
{
    NVMCTRL->STATUS.reg = NVMCTRL_STATUS_MASK;   /* clear stale error flags */
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | code;
    while (!(NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY)) {
    }
}

void nvm_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    NVMCTRL->CTRLB.bit.MANW = 1;     /* pages are written only on command */

    for (uint32_t offset = 0u; offset < length; offset += PAGE_BYTES) {
        const uint32_t page = address + offset;
        if ((page % NVM_ROW_BYTES) == 0u) {
            NVMCTRL->ADDR.reg = page / 2u;
            command(NVMCTRL_CTRLA_CMD_ER);
        }
        command(NVMCTRL_CTRLA_CMD_PBC);
        /* Loading the page buffer: 16-bit writes to the page's own address. */
        volatile uint16_t *target = (volatile uint16_t *)page;
        for (uint32_t index = 0u; index < PAGE_BYTES; index += 2u) {
            const uint32_t at = offset + index;
            const uint16_t low  = (at < length)      ? data[at]      : 0xFFu;
            const uint16_t high = (at + 1u < length) ? data[at + 1u] : 0xFFu;
            *target++ = (uint16_t)(low | (high << 8));
        }
        command(NVMCTRL_CTRLA_CMD_WP);
    }
}

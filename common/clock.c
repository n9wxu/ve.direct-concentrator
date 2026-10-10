#include "board.h"
#include "hal.h"

static volatile uint32_t milliseconds;

void SysTick_Handler(void)
{
    milliseconds++;
}

uint32_t hal_millis(void)
{
    return milliseconds;
}

void hal_reset(void)
{
    NVIC_SystemReset();
    for (;;) {
    }
}

void delay_us(uint32_t microseconds)
{
    /* About 4 cycles a turn at 48 MHz; precision does not matter here. */
    for (volatile uint32_t turns = microseconds * 6u; turns != 0u; turns--) {
    }
}

static void gclk_sync(void)
{
    while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) {
    }
}

static void dfll_ready(void)
{
    while (!(SYSCTRL->PCLKSR.reg & SYSCTRL_PCLKSR_DFLLRDY)) {
    }
}

static bool crystal_started(void)
{
    SYSCTRL->XOSC32K.reg = SYSCTRL_XOSC32K_STARTUP(0x6u) | SYSCTRL_XOSC32K_XTALEN
                         | SYSCTRL_XOSC32K_EN32K;
    SYSCTRL->XOSC32K.bit.ENABLE = 1;
    /* Start-up is about 2 s at worst; this runs on the 8 MHz reset clock or slower. */
    for (uint32_t tries = 0u; tries < 4000000u; tries++) {
        if (SYSCTRL->PCLKSR.reg & SYSCTRL_PCLKSR_XOSC32KRDY) {
            return true;
        }
    }
    return false;
}

static void dfll_closed_loop(void)
{
    GCLK->GENDIV.reg = GCLK_GENDIV_ID(1);
    gclk_sync();
    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(1) | GCLK_GENCTRL_SRC_XOSC32K | GCLK_GENCTRL_GENEN;
    gclk_sync();
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_DFLL48 | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_CLKEN;
    gclk_sync();

    /* Order as in the Arduino Zero core and ASF (errata 9905: ONDEMAND must be
     * off before the DFLL is configured). */
    SYSCTRL->DFLLCTRL.reg = 0;
    dfll_ready();
    SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_CSTEP(31) | SYSCTRL_DFLLMUL_FSTEP(511)
                         | SYSCTRL_DFLLMUL_MUL(CPU_HZ / 32768ul);
    dfll_ready();
    SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_MODE | SYSCTRL_DFLLCTRL_WAITLOCK
                          | SYSCTRL_DFLLCTRL_QLDIS;
    dfll_ready();
    SYSCTRL->DFLLCTRL.reg |= SYSCTRL_DFLLCTRL_ENABLE;
    while ((SYSCTRL->PCLKSR.reg & (SYSCTRL_PCLKSR_DFLLLCKC | SYSCTRL_PCLKSR_DFLLLCKF))
           != (SYSCTRL_PCLKSR_DFLLLCKC | SYSCTRL_PCLKSR_DFLLLCKF)) {
    }
    dfll_ready();
}

/* No crystal: the factory-calibrated DFLL, steered by USB start-of-frames when
 * a host is attached. Free-running accuracy may not be enough for 500 kbaud;
 * this exists so a board with a dead crystal still enumerates on USB. */
static void dfll_open_loop(void)
{
    uint32_t coarse = (*(const uint32_t *)FUSES_DFLL48M_COARSE_CAL_ADDR
                       & FUSES_DFLL48M_COARSE_CAL_Msk) >> FUSES_DFLL48M_COARSE_CAL_Pos;
    if (coarse == 0x3Fu) {
        coarse = 0x1Fu;
    }
    SYSCTRL->DFLLCTRL.reg = 0;
    dfll_ready();
    SYSCTRL->DFLLVAL.reg = SYSCTRL_DFLLVAL_COARSE(coarse) | SYSCTRL_DFLLVAL_FINE(512);
    SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_CSTEP(1) | SYSCTRL_DFLLMUL_FSTEP(1)
                         | SYSCTRL_DFLLMUL_MUL(48000);
    dfll_ready();
    SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_MODE | SYSCTRL_DFLLCTRL_USBCRM
                          | SYSCTRL_DFLLCTRL_CCDIS;
    dfll_ready();
    SYSCTRL->DFLLCTRL.reg |= SYSCTRL_DFLLCTRL_ENABLE;
    dfll_ready();
}

void clock_init(void)
{
    NVMCTRL->CTRLB.bit.RWS = 1;        /* one wait state at 48 MHz */
    PM->APBAMASK.reg |= PM_APBAMASK_GCLK;

    const bool crystal = crystal_started();

    GCLK->CTRL.reg = GCLK_CTRL_SWRST;
    while ((GCLK->CTRL.reg & GCLK_CTRL_SWRST) && (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY)) {
    }

    if (crystal) {
        dfll_closed_loop();
    } else {
        dfll_open_loop();
    }

    GCLK->GENDIV.reg = GCLK_GENDIV_ID(0);
    gclk_sync();
    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_IDC
                      | GCLK_GENCTRL_GENEN;
    gclk_sync();

    PM->CPUSEL.reg = PM_CPUSEL_CPUDIV_DIV1;
    PM->APBASEL.reg = PM_APBASEL_APBADIV_DIV1_Val;
    PM->APBBSEL.reg = PM_APBBSEL_APBBDIV_DIV1_Val;
    PM->APBCSEL.reg = PM_APBCSEL_APBCDIV_DIV1_Val;

    SysTick_Config(CPU_HZ / 1000ul);
}

void watchdog_start(void)
{
    /* GCLK2 = 32 kHz ULP oscillator / 32 = 1024 Hz; 8192 cycles = 8 s. */
    GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4);
    gclk_sync();
    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_SRC_OSCULP32K | GCLK_GENCTRL_DIVSEL
                      | GCLK_GENCTRL_GENEN;
    gclk_sync();
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT | GCLK_CLKCTRL_GEN_GCLK2 | GCLK_CLKCTRL_CLKEN;
    gclk_sync();

    WDT->CONFIG.reg = WDT_CONFIG_PER_8K;
    while (WDT->STATUS.reg & WDT_STATUS_SYNCBUSY) {
    }
    WDT->CTRL.reg = WDT_CTRL_ENABLE;
    while (WDT->STATUS.reg & WDT_STATUS_SYNCBUSY) {
    }
}

void watchdog_kick(void)
{
    if (!(WDT->STATUS.reg & WDT_STATUS_SYNCBUSY)) {
        WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
    }
}

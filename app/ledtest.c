#include "ledtest.h"
#include "board.h"
#include "hal.h"

#define MARK_MS     120u     /* the three fast blinks that start each round */
#define STEP_MS     500u     /* each output alone */
#define GAP_MS      250u
#define ROUND_MS    1000u    /* pause before the next round */

static void wait_ms(uint32_t milliseconds)
{
    const uint32_t start = hal_millis();
    while (hal_millis() - start < milliseconds) {
        watchdog_kick();
    }
}

static void show(uint8_t pin, bool lit)
{
    if (pin == PIN_DRIVE_1 || pin == PIN_DRIVE_2) {
        pin_write(pin, lit);
    } else {
        led_write(pin, lit);
    }
}

void ledtest_run(void)
{
    static const uint8_t ve[] = { PIN_LED_VE1, PIN_LED_VE2, PIN_LED_VE3, PIN_LED_VE4 };
    static const uint8_t order[] = {
        PIN_LED_VE1, PIN_LED_VE2, PIN_LED_VE3, PIN_LED_VE4, PIN_LED_BUS,
        PIN_DRIVE_1, PIN_DRIVE_2, PIN_LED_USB, PIN_LED_HEALTH,
    };

    for (uint8_t index = 0u; index < sizeof order; index++) {
        if (order[index] == PIN_DRIVE_1 || order[index] == PIN_DRIVE_2) {
            continue;   /* loads_init() has them, off */
        }
        led_init(order[index]);
    }
    for (;;) {
        for (uint8_t blink = 0u; blink < 3u; blink++) {
            for (uint8_t index = 0u; index < sizeof ve; index++) {
                show(ve[index], true);
            }
            wait_ms(MARK_MS);
            for (uint8_t index = 0u; index < sizeof ve; index++) {
                show(ve[index], false);
            }
            wait_ms(MARK_MS);
        }
        wait_ms(GAP_MS * 2u);
        for (uint8_t index = 0u; index < sizeof order; index++) {
            show(order[index], true);
            wait_ms(STEP_MS);
            show(order[index], false);
            wait_ms(GAP_MS);
        }
        wait_ms(ROUND_MS);
    }
}

#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "sam.h"

/* VE.Direct concentrator board: SAMD21G18A on the Arduino Zero pin map.
 *
 *   Signal      Zero pin  Port   SERCOM / pad
 *   485 TX      D1        PA10   SERCOM0 pad 2
 *   485 RX      D0        PA11   SERCOM0 pad 3
 *   485 DE      D3        PA09
 *   485 LED     D7        PA21
 *   VE1 TX/RX   D2/D5     PA14/PA15   SERCOM2 pad 2/3         LED A4  PA05
 *   VE2 TX/RX   SDA/SCL   PA22/PA23   SERCOM3 pad 0/1         LED D11 PA16
 *   VE3 TX/RX   D10/D12   PA18/PA19   SERCOM1 pad 2/3         LED D13 PA17
 *   VE4 TX/RX   A1/A2     PB08/PB09   SERCOM4 pad 0/1 (mux D) LED D6  PA20
 *   Load 1      D8 drive  PA06, A0 current sense PA02 (AIN0)
 *   Load 2      D9 drive  PA07, A3 current sense PA04 (AIN4)
 *   Health LED  TX LED    PA27   lit when low, as on the Zero
 *   USB LED     RX LED    PB03   lit when low, as on the Zero
 *   USB D-/D+             PA24/PA25
 *
 * Ports are numbered as on the board's connectors, which is not the SERCOM
 * order: connector 1 is SERCOM2, 2 is SERCOM3, 3 is SERCOM1 (common/uart.c). */

#define PIN(port, number)   ((uint8_t)(((port) << 5) | (number)))
#define PORT_A  0u
#define PORT_B  1u

#define PIN_NONE        0xFFu             /* not wired, or not known yet */

#define PIN_485_DE      PIN(PORT_A, 9)
#define PIN_DRIVE_1     PIN(PORT_A, 6)    /* D8 */
#define PIN_DRIVE_2     PIN(PORT_A, 7)    /* D9 */
#define PIN_SENSE_1     PIN(PORT_A, 2)    /* A0, load 1 current, analog */
#define PIN_SENSE_2     PIN(PORT_A, 4)    /* A3, load 2 current, analog */
#define AIN_SENSE_1     0u                /* ADC input of PA02 */
#define AIN_SENSE_2     4u                /* ADC input of PA04 */
#define VDDANA_MV       3300u             /* the ADC's full scale */
#define PIN_LED_VE1     PIN(PORT_A, 5)    /* A4 */
#define PIN_LED_VE2     PIN(PORT_A, 16)   /* D11 */
#define PIN_LED_VE3     PIN(PORT_A, 17)   /* D13 */
#define PIN_LED_VE4     PIN(PORT_A, 20)   /* D6 */
#define PIN_LED_BUS     PIN(PORT_A, 21)   /* D7 */
#define PIN_LED_HEALTH  PIN(PORT_A, 27)   /* Zero TX LED */
#define PIN_LED_USB     PIN(PORT_B, 3)    /* Zero RX LED */

static inline void pin_output(uint8_t pin)
{
    PORT->Group[pin >> 5].DIRSET.reg = 1ul << (pin & 31u);
}

static inline void pin_input(uint8_t pin)
{
    PORT->Group[pin >> 5].DIRCLR.reg = 1ul << (pin & 31u);
    PORT->Group[pin >> 5].PINCFG[pin & 31u].reg = PORT_PINCFG_INEN;
}

static inline void pin_write(uint8_t pin, bool high)
{
    if (high) {
        PORT->Group[pin >> 5].OUTSET.reg = 1ul << (pin & 31u);
    } else {
        PORT->Group[pin >> 5].OUTCLR.reg = 1ul << (pin & 31u);
    }
}

static inline bool pin_read(uint8_t pin)
{
    return (PORT->Group[pin >> 5].IN.reg >> (pin & 31u)) & 1u;
}

/* Hand a pin to the ADC (function B), with its digital input buffer off. */
static inline void pin_analog(uint8_t pin)
{
    PortGroup *group = &PORT->Group[pin >> 5];
    const uint8_t number = pin & 31u;
    if (number & 1u) {
        group->PMUX[number >> 1].bit.PMUXO = 1u;
    } else {
        group->PMUX[number >> 1].bit.PMUXE = 1u;
    }
    group->PINCFG[number].reg = PORT_PINCFG_PMUXEN;
}

/* Hand a pin to peripheral function A..H (0..7). */
static inline void pin_peripheral(uint8_t pin, uint8_t function)
{
    PortGroup *group = &PORT->Group[pin >> 5];
    const uint8_t number = pin & 31u;
    if (number & 1u) {
        group->PMUX[number >> 1].bit.PMUXO = function;
    } else {
        group->PMUX[number >> 1].bit.PMUXE = function;
    }
    group->PINCFG[number].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
}

/* Light or darken an LED. The Zero-style TX/RX LEDs (health, USB) light when
 * their pin is low, the others when it is high. */
static inline void led_write(uint8_t pin, bool lit)
{
    if (pin == PIN_NONE) {
        return;
    }
    const bool active_low = (pin == PIN_LED_HEALTH) || (pin == PIN_LED_USB);
    pin_write(pin, lit != active_low);
}

/* An LED as an output, dark. */
static inline void led_init(uint8_t pin)
{
    if (pin != PIN_NONE) {
        led_write(pin, false);
        pin_output(pin);
    }
}

/* A traffic LED: lit for LED_PULSE_MS after its counter last moved, so a
 * stream of bytes or frames shows as blinks and an idle line as dark. */
#define LED_PULSE_MS    50u

struct traffic_led {
    uint8_t  pin;
    bool     lit;
    uint32_t count;
    uint32_t moved_ms;
};

static inline void traffic_led_update(struct traffic_led *led, uint32_t count, uint32_t now_ms)
{
    if (count != led->count) {
        led->count = count;
        led->moved_ms = now_ms;
        led->lit = true;
    } else if (led->lit && now_ms - led->moved_ms >= LED_PULSE_MS) {
        led->lit = false;
    }
    led_write(led->pin, led->lit);
}

#define CPU_HZ  48000000ul

/* clock.c */
void clock_init(void);          /* 48 MHz DFLL, 1 ms SysTick */
void delay_us(uint32_t microseconds);
void watchdog_start(void);      /* 8 s */
void watchdog_kick(void);

#endif

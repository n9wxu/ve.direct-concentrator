#ifndef LEDTEST_H
#define LEDTEST_H

/* Bench lamp test, built with -DCFG_LED_TEST=1 in place of the application:
 * the four VE.Direct LEDs blink three times fast, then each output lights
 * alone in turn — VE1, VE2, VE3, VE4, 485, drive 1, drive 2, USB, health —
 * and the round repeats. The drives are the real load switch outputs. */

void ledtest_run(void) __attribute__((noreturn));

#endif

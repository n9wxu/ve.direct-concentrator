#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* What the portable modules (link485, fota, loadctl) need from the hardware.
 * The SAMD21 implementations are in clock.c, uart.c, nvm.c and app/loads.c;
 * the host tests supply fakes. */

uint32_t hal_millis(void);
void     hal_reset(void);

/* One received 485 byte, if any. */
bool rs485_getc(uint8_t *byte);

/* Drive the bus, send, release, and discard our own echo. Blocks until sent. */
void rs485_send(const uint8_t *data, size_t length);

#define NVM_ROW_BYTES   256u

/* Erase then program the rows covering [address, address + length). The address
 * is row-aligned; a partial last page is padded with 0xFF. */
void nvm_write(uint32_t address, const uint8_t *data, uint32_t length);

/* The two load switches, bit 0 = drive 1 and bit 1 = drive 2; off at reset.
 * Each has an analog current-sense input, read in millivolts at the pin
 * (load 0 = switch 1 on A0, load 1 = switch 2 on A3). */
void     loads_init(void);
void     loads_set(uint8_t outputs);
uint8_t  loads_outputs(void);
uint16_t loads_sense_mv(uint8_t load);

#endif

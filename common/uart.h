#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* The five SERCOM USARTs: interrupt-driven receive rings everywhere, and a
 * transmit ring on the VE.Direct ports. The 485 port transmits by blocking
 * (rs485_send() in hal.h), because it owns the bus for exactly that long. */

enum uart_id { UART_485, UART_VE1, UART_VE2, UART_VE3, UART_VE4, UART_COUNT };

void   uart_init(enum uart_id id, uint32_t baud);
size_t uart_rx_available(enum uart_id id);
size_t uart_read(enum uart_id id, uint8_t *data, size_t limit);
void   uart_rx_flush(enum uart_id id);

/* VE.Direct ports only. */
size_t uart_tx_free(enum uart_id id);
size_t uart_write(enum uart_id id, const uint8_t *data, size_t length);

/* Receive overruns and ring overflows since reset. */
uint32_t uart_rx_lost(enum uart_id id);

/* Put bytes in a port's receive ring as if they had arrived on the line (the
 * USB mock of a VE.Direct device). Returns how many fit; the rest count as lost. */
size_t uart_inject(enum uart_id id, const uint8_t *data, size_t length);

#endif

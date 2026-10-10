#ifndef VEPORTS_H
#define VEPORTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net.h"
#include "tcp.h"

/* Each VE.Direct serial port is a TCP listener (CFG_VE_PORT_BASE + n): the
 * bytes the device sends go to the client untouched, and what the client
 * writes goes to the device (HEX protocol commands). One client per port. */

#define VE_PORT_COUNT 4u

void veports_init(net_t *net);
void veports_service(void);

/* For tcp_set_connections(). */
tcp_conn_t *veports_connection(uint8_t port);

/* Every byte a client sends toward a device (port 0-3), for the USB mock. */
void veports_set_tap(void (*tap)(uint8_t port, const uint8_t *data, size_t length));

/* Bytes received from the device since reset. */
uint32_t veports_rx_bytes(uint8_t port);
bool     veports_connected(uint8_t port);

#endif

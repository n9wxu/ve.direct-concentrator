#ifndef VEMOCK_H
#define VEMOCK_H

#include <stdint.h>
#include <stddef.h>

/* The VE.Direct mock: the second USB serial port stands in for devices on the
 * VE.Direct ports, for a bench without them (tools/vemock.py).
 *
 * Both directions carry packets
 *
 *   A5 <port 1-4> <length 1-255> <length bytes>
 *
 * Host to unit: bytes "from the device" on that port, put in its receive
 * ring as if they had come off the line — LED, TCP stream and all. Unit to
 * host, while the host has the port open: bytes a TCP client sent toward the
 * device (HEX commands), so the mock can see and answer them. */

void vemock_service(void);
void vemock_tap(uint8_t port, const uint8_t *data, size_t length);   /* veports_set_tap() */

/* Bytes moved either way since reset, for the USB LED. */
uint32_t vemock_bytes(void);

#endif

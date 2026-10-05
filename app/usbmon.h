#ifndef USBMON_H
#define USBMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* USB test mode: a CDC serial port that shows every byte on the 485 bus as a
 * hex dump, one line per run of received ("R") or transmitted ("T") bytes.
 * Active while a host has the port open; never slows the link — when USB
 * cannot keep up, bytes are dropped and the count reported ("! lost n"). */

void usbmon_init(void);
void usbmon_service(void);
void usbmon_tap(bool transmitted, const uint8_t *data, size_t length);

/* Bytes sent to the host since reset, for the USB LED. */
uint32_t usbmon_bytes(void);

#endif

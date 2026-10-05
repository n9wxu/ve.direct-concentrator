#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include "net.h"

/* ASCII commands over UDP (CFG_CONTROL_PORT), one per datagram, answered to
 * the sender:
 *
 *   status        versions, uptime, link and port counters
 *   out <0-3>     the two switched outputs: bit 0 = drive 1, bit 1 = drive 2
 *                 (the load control stream, loadctl.h, is the main way in)
 *   app           reset into the bootloader, which fetches CFG_APP_FILE
 *   bootloader    fetch CFG_BOOT_FILE into the staging slot, then reset so
 *                 stage0 installs it
 */

void control_init(net_t *net);
void control_input(net_t *net, uint32_t source_ip, uint16_t source_port,
                   const uint8_t *source_mac, const uint8_t *payload, uint16_t length);
void control_tick(uint32_t elapsed_ms);

#endif

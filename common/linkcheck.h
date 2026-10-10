#ifndef LINKCHECK_H
#define LINKCHECK_H

#include <stdint.h>
#include "net.h"

/* Proof that recon hears us, and a way back if it stops.
 *
 * Polls arriving only show that we hear the master. The round trip shows the
 * reverse: every LINKCHECK_PROBE_MS we ARP for recon (the gateway, 10.55.0.1),
 * whose kernel answers on tap0 whatever else is or is not running. With no
 * answer for CFG_LINK_LOSS_RESET_S, the unit resets — through the
 * bootloader's window, where "vedc.py rescue" can catch it — so an
 * application whose link is broken in any way (receive, transmit, turnaround,
 * IP) cannot strand a unit that has no debugger attached. */

#define LINKCHECK_PROBE_MS  10000u

void linkcheck_init(net_t *net);
void linkcheck_tick(uint32_t elapsed_ms);

/* Milliseconds since recon last answered (since start, if it never has). */
uint32_t linkcheck_silent_ms(void);

#endif

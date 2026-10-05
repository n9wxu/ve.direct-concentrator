#ifndef LOADCTL_H
#define LOADCTL_H

#include <stdbool.h>
#include "net.h"
#include "tcp.h"

/* The load switches as a TCP stream (CFG_LOAD_PORT), one client at a time.
 * ASCII lines, LF-terminated (a CR before it is ignored):
 *
 *   status        the state line below
 *   out <0-3>     both switches: bit 0 = drive 1, bit 1 = drive 2
 *   on <1|2>      one switch on
 *   off <1|2>     one switch off
 *
 * Every line the unit sends is either the current state,
 *
 *   out <0-3> sense <mV on A0> <mV on A3>
 *
 * (the switches' current-sense voltages, at the pin), or "error ..." answering
 * a command. A state line answers each good command, greets each new client,
 * follows any output change (also one made over UDP), and repeats every second
 * while nothing else is sent, so a client always holds the latest state and
 * currents, and a dead one is noticed. */

void        loadctl_init(net_t *net);
tcp_conn_t *loadctl_connection(void);
void        loadctl_service(void);
bool        loadctl_connected(void);

#endif

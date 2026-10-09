#ifndef TCPLINK_H
#define TCPLINK_H

#include <stdint.h>
#include "net.h"

/* TCP over a polled link: the poll is the clock. (The same as
 * halfduplex-tcpip's slave/tcplink.)
 *
 * Every frame we send answers a poll, and the master hands each frame to its
 * host and passes back the host's answer before it polls again (the master's
 * side of this, in tmptun, is to wait for that answer; the host's is to
 * acknowledge at once: a quickack route on recon's tap0). So an idle poll, one with nothing
 * more for us while nothing of ours waits to go, means that a segment TCP has
 * sent to a peer on the link and that is still unacknowledged was lost on the
 * way. tcplink sends it again at once, in the reply to that poll, with
 * tcp_retransmit(), instead of letting TCP wait out its timeout. TCP's own
 * acknowledgements decide everything; the link adds no signalling.
 *
 * A peer beyond the master, reached through a router, answers when it
 * answers: its connections keep TCP's own timing. "On the link" is the
 * unit's own subnet. */

/* Take TCP's retransmissions on the link over: hooks link485's idle polls. */
void tcplink_init(net_t *net);

/* The idle-poll hook itself (link485_set_on_idle_poll()), for a firmware
 * that has its own hook and calls this from it. */
void tcplink_idle_poll(void);

/* Segments sent again on an idle poll since tcplink_init(). */
uint32_t tcplink_retransmissions(void);

#endif

#ifndef LINK485_H
#define LINK485_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net_mac.h"

/* The RS-485 side: a TMP-v2 slave that the master polls. Each POLL may carry
 * one Ethernet frame for us; each reply carries one of ours, or none. This is
 * the stack's MAC driver. Best-effort: a frame that cannot be queued is
 * dropped, and a frame lost on the wire is gone; TCP recovers it, and with
 * app/tcplink.h it does so at the next idle poll instead of after a timeout. */

extern const net_mac_t link485_mac;

void link485_init(void);

/* Drain the receiver and answer at most one poll. Call continuously, with
 * net_poll() after it. */
void link485_service(void);

/* Called with a valid poll in hand, before the reply is sent: the place for
 * work that stalls the CPU (flash writes), while the master is still waiting
 * for us rather than talking. */
void link485_set_before_reply(void (*hook)(void));

/* Called for an idle poll: one that carries no frame for us, arriving while
 * nothing of ours waits to go. The master has nothing more for us, and has
 * had the chance to deliver everything we sent and pass back its host's
 * answer. Called before the reply is built, so a frame the hook sends (a TCP
 * retransmission, say: app/tcplink.h) leaves in this reply. */
void link485_set_on_idle_poll(void (*hook)(void));

/* Every byte on the wire, as received and as sent, for the USB monitor. */
void link485_set_tap(void (*tap)(bool transmitted, const uint8_t *data, size_t length));

/* Milliseconds since the last poll addressed to us; UINT32_MAX if never. */
uint32_t link485_poll_age_ms(void);

/* Ethernet frames carried either way since reset, for the bus LED. Empty
 * polls and replies do not count: the master polls without pause. */
uint32_t link485_frames(void);

/* Idle polls since reset. */
uint32_t link485_idle_polls(void);

#endif

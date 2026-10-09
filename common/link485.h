#ifndef LINK485_H
#define LINK485_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net_mac.h"

/* The RS-485 side: a TMP-v2 slave that the master polls. Each POLL may carry
 * one Ethernet frame for us; each reply carries one of ours, or none. This is
 * the stack's MAC driver.
 *
 * A master that sets TMP_FLAG_ARQ acknowledges every frame: our frame stays at
 * the head of the queue, and goes again with each reply, until a poll's ACK
 * moves past it; a frame of the master's whose SEQ we have already taken is
 * acknowledged again but not taken twice. No timer: the next poll is the
 * clock. Without ARQ a frame leaves the queue once sent (best effort). A
 * frame the queue has no room for is dropped, and TCP or the application
 * retries. */

extern const net_mac_t link485_mac;

void link485_init(void);

/* Drain the receiver and answer at most one poll. Call continuously, with
 * net_poll() after it. */
void link485_service(void);

/* Called with a valid poll in hand, before the reply is sent: the place for
 * work that stalls the CPU (flash writes), while the master is still waiting
 * for us rather than talking. */
void link485_set_before_reply(void (*hook)(void));

/* Every byte on the wire, as received and as sent, for the USB monitor. */
void link485_set_tap(void (*tap)(bool transmitted, const uint8_t *data, size_t length));

/* Milliseconds since the last poll addressed to us; UINT32_MAX if never. */
uint32_t link485_poll_age_ms(void);

/* Ethernet frames carried either way since reset, for the bus LED. Empty
 * polls and replies do not count: the master polls without pause. */
uint32_t link485_frames(void);

/* Our frames sent again because a poll did not acknowledge them, and frames
 * of the master's received twice (our acknowledgement was lost), since reset. */
uint32_t link485_resends(void);
uint32_t link485_duplicates(void);

#endif

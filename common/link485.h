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
 * Frames damaged on the wire are retried by the MAC: the master sends a poll
 * whose answer it did not get intact again, flagged TMP_FLAG_RETRY, and we
 * answer it with the same frame; a poll we did not hear, it sends again. A
 * frame that cannot be queued is dropped and TCP or the application retries,
 * as on Ethernet. */

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

/* Polls answered again since reset: MAC retries by recon, after our answer
 * reached it damaged or not at all. */
uint32_t link485_retries(void);

#endif

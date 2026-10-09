#ifndef MASTER_H
#define MASTER_H

/* The test harness: fake hardware under the unit, and the TMP-v2 master on
 * the other end of the 485, building the Ethernet frames it carries. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "net.h"
#include "tmp_v2.h"

#define CHECK(condition) \
    do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)

#define MASTER_IP NET_IPV4(10, 55, 0, 1)

extern const uint8_t unit_mac[6];
extern const uint8_t master_mac[6];

/* The unit's stack, on the link485 MAC driver; master_unit_init() gives it
 * the configured addresses. */
extern net_t net;
net_err_t master_unit_init(void);

/* Fake hardware. */
extern uint32_t now_ms;
extern int      resets;          /* hal_reset() calls */
extern uint8_t  wire_out[4096];
extern size_t   wire_out_length;

/* Run after net_poll() on every exchange: the unit's main-loop work. */
extern void (*unit_service)(void);

/* The master: send raw bytes or a poll, and keep the unit's reply. */
extern uint8_t          sequence;
extern struct tmp_frame answer;
extern bool             answered;
void exchange_raw(const uint8_t *bytes, size_t length);
void poll(uint8_t address, const uint8_t *frame, size_t length);
/* A poll with a given SEQ and FLAGS (poll() counts SEQ up and sends none). */
void poll_as(uint8_t address, uint8_t poll_sequence, uint8_t flags, const uint8_t *frame,
             size_t length);

/* A master with acknowledged delivery (TMP_FLAG_ARQ), as recon's tmptun runs
 * it: the SEQ of its frame and the SEQ it expects next from the unit. `lost`
 * pretends the reply arrived corrupted: the master takes nothing from it, so
 * it acknowledges nothing. arq_new: the reply carried a frame the master took. */
extern bool arq_down_bit, arq_up_expected, arq_new;
void arq_poll(const uint8_t *frame, size_t length, bool lost);

/* Frame builders; each returns the header length. */
uint16_t checksum(const uint8_t *data, size_t length, uint32_t sum);
void     put16(uint8_t *at, uint16_t value);
void     put32(uint8_t *at, uint32_t value);
size_t   ethernet(uint8_t *frame, const uint8_t *destination, uint16_t type);
size_t   ipv4(uint8_t *frame, uint8_t protocol, size_t payload_length);
uint32_t pseudo_header(uint8_t protocol, size_t length);

#endif

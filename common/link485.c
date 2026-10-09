#include "link485.h"
#include <string.h>
#include "config.h"
#include "hal.h"
#include "tmp_v2.h"

static struct tmp_scanner scanner;
static struct tmp_frame   inbound;        /* the last poll; payload = frame for us */
static uint16_t           inbound_length; /* 0: the stack has nothing to read      */

/* Our frames waiting for a poll to carry them. */
#define OUTBOUND_SLOTS  6u
struct outbound_slot {
    uint16_t length;
    uint8_t  bytes[CFG_NET_FRAME];
};
static struct outbound_slot outbound[OUTBOUND_SLOTS];
static uint8_t outbound_head;
static uint8_t outbound_count;

/* Acknowledged delivery (TMP_FLAG_ARQ), one frame in flight each way. */
static bool     up_bit;          /* SEQ of the frame at the head of the queue      */
static bool     up_in_flight;    /* the head went out, not yet acknowledged        */
static bool     down_expected;   /* SEQ of the next new frame from the master      */

static void (*before_reply)(void);
static void (*tap)(bool transmitted, const uint8_t *data, size_t length);
static uint32_t last_poll_ms;
static bool     polled;
static uint32_t frames;
static uint32_t resends;
static uint32_t duplicates;

void link485_init(void)
{
    tmp_scanner_reset(&scanner);
    inbound_length = 0u;
    outbound_head = 0u;
    outbound_count = 0u;
    up_bit = up_in_flight = down_expected = false;
    polled = false;
}

void link485_set_before_reply(void (*hook)(void))
{
    before_reply = hook;
}

void link485_set_tap(void (*function)(bool, const uint8_t *, size_t))
{
    tap = function;
}

uint32_t link485_poll_age_ms(void)
{
    return polled ? hal_millis() - last_poll_ms : UINT32_MAX;
}

uint32_t link485_frames(void)
{
    return frames;
}

uint32_t link485_resends(void)
{
    return resends;
}

uint32_t link485_duplicates(void)
{
    return duplicates;
}

static void dequeue(void)
{
    outbound_head = (uint8_t)((outbound_head + 1u) % OUTBOUND_SLOTS);
    outbound_count--;
    frames++;
}

/* The answer to a poll: the head of the queue, or nothing. Under ARQ the head
 * stays queued until the master acknowledges it, so an answer the master
 * missed goes again with the next poll; without ARQ it is gone once sent. */
static void reply(uint8_t sequence, bool arq)
{
    static uint8_t wire[1u + TMP_FRAME_OVERHEAD + CFG_NET_FRAME];
    const uint8_t *payload = NULL;
    uint16_t length = 0u;
    if (outbound_count != 0u) {
        payload = outbound[outbound_head].bytes;
        length = outbound[outbound_head].length;
    }
    uint8_t flags = tmp_flags(false, 0u);
    if (arq) {
        flags |= TMP_FLAG_ARQ | (up_bit ? TMP_FLAG_SEQ : 0u) | (down_expected ? TMP_FLAG_ACK : 0u);
        if (length != 0u) {
            resends += up_in_flight;
            up_in_flight = true;
        }
    }
    wire[0] = 0xFFu;   /* preamble: absorbs the transceivers' turnaround */
    const size_t wire_length = 1u + tmp_frame_build(&wire[1], TMP_NET, TMP_ADDR_COLLECTOR, sequence,
                                                    flags, payload, length);
    rs485_send(wire, wire_length);
    if (tap != NULL) {
        tap(true, wire, wire_length);
    }
    if (!arq && length != 0u) {
        dequeue();
    }
    /* Whatever the scanner held came before our reply: start the next frame clean.
     * Bytes of the next poll may already be in the ring; they are kept. */
    tmp_scanner_reset(&scanner);
}

static bool addressed_to_us(uint8_t address)
{
    return address == TMP_ADDR_COLLECTOR || address == TMP_ADDR_BROADCAST;
}

void link485_service(void)
{
    uint8_t chunk[64];
    size_t  filled = 0u;
    bool    answered = false;
    uint8_t byte;

    while (!answered && rs485_getc(&byte)) {
        chunk[filled++] = byte;
        if (tmp_scanner_push(&scanner, byte, &inbound) == SCAN_FRAME_READY
            && inbound.type == TMP_POLL && addressed_to_us(inbound.address)) {
            answered = true;
        }
        if (filled == sizeof chunk || answered) {
            if (tap != NULL) {
                tap(false, chunk, filled);
            }
            filled = 0u;
        }
    }
    if (filled != 0u && tap != NULL) {
        tap(false, chunk, filled);
    }
    if (!answered) {
        return;
    }

    polled = true;
    last_poll_ms = hal_millis();
    const bool arq = (inbound.flags & TMP_FLAG_ARQ) != 0u;
    if (arq) {
        /* The master's ACK moved past our frame: it arrived. If not, it goes
         * again in this reply. */
        const bool ack = (inbound.flags & TMP_FLAG_ACK) != 0u;
        if (up_in_flight && ack != up_bit) {
            dequeue();
            up_bit = !up_bit;
            up_in_flight = false;
        }
        /* The master's frame: new if its SEQ is the one we expect; otherwise
         * we took it already and only our acknowledgement was lost. */
        if (inbound.length != 0u) {
            const bool seq = (inbound.flags & TMP_FLAG_SEQ) != 0u;
            if (seq == down_expected) {
                inbound_length = inbound.length;
                down_expected = !down_expected;
                frames++;
            } else {
                duplicates++;
            }
        }
    } else {
        /* A master without ARQ: best effort, and nothing of ours in flight. */
        up_in_flight = false;
        inbound_length = inbound.length;
        if (inbound_length != 0u) {
            frames++;
        }
    }
    if (before_reply != NULL) {
        before_reply();
    }
    reply(inbound.sequence, arq);
}

/* ── MAC driver ─────────────────────────────────────────────────────── */

static int mac_init(void *context)
{
    (void)context;
    return 0;
}

static int mac_send(void *context, const uint8_t *frame, uint16_t length)
{
    (void)context;
    if (length > CFG_NET_FRAME) {
        return -1;
    }
    if (outbound_count >= OUTBOUND_SLOTS) {
        return 0;
    }
    struct outbound_slot *slot = &outbound[(outbound_head + outbound_count) % OUTBOUND_SLOTS];
    memcpy(slot->bytes, frame, length);
    slot->length = length;
    outbound_count++;
    return (int)length;
}

static int mac_poll(void *context)
{
    (void)context;
    return (int)inbound_length;
}

static int mac_peek(void *context, uint16_t offset, uint8_t *buffer, uint16_t length)
{
    (void)context;
    if (inbound_length == 0u) {
        return -1;
    }
    if (offset >= inbound_length) {
        return 0;
    }
    if (length > inbound_length - offset) {
        length = (uint16_t)(inbound_length - offset);
    }
    memcpy(buffer, &inbound.payload[offset], length);
    return (int)length;
}

static void mac_discard(void *context)
{
    (void)context;
    inbound_length = 0u;
}

static void mac_close(void *context)
{
    (void)context;
}

const net_mac_t link485_mac = {
    mac_init, mac_send, mac_poll, mac_peek, mac_discard, mac_close
};

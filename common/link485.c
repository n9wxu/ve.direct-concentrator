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

/* MAC retry: the frame at the head of the queue stays there after it has
 * gone, until the master moves on to a new poll. A poll flagged
 * TMP_FLAG_RETRY with the SEQ we last answered means our answer did not reach
 * the master intact, and it gets the same frame again. */
static bool    head_sent;           /* outbound[outbound_head] went in our last answer */
static bool    answered_any;        /* last_sequence is valid */
static uint8_t last_sequence;       /* the SEQ of the last poll we answered */

static void (*before_reply)(void);
static void (*tap)(bool transmitted, const uint8_t *data, size_t length);
static uint32_t last_poll_ms;
static bool     polled;
static uint32_t frames;
static uint32_t retries;

void link485_init(void)
{
    tmp_scanner_reset(&scanner);
    inbound_length = 0u;
    outbound_head = 0u;
    outbound_count = 0u;
    head_sent = false;
    answered_any = false;
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

uint32_t link485_retries(void)
{
    return retries;
}

static void reply(uint8_t sequence)
{
    static uint8_t wire[1u + TMP_FRAME_OVERHEAD + CFG_NET_FRAME];
    const uint8_t *payload = NULL;
    uint16_t length = 0u;
    if (outbound_count != 0u) {
        payload = outbound[outbound_head].bytes;
        length = outbound[outbound_head].length;
        if (!head_sent) {
            frames++;
        }
    }
    head_sent = outbound_count != 0u;
    wire[0] = 0xFFu;   /* preamble: absorbs the transceivers' turnaround */
    const size_t wire_length = tmp_frame_build(&wire[1], TMP_NET, TMP_ADDR_COLLECTOR, sequence,
                                               tmp_flags(false, 0u), payload, length);
    rs485_send(wire, wire_length + 1u);
    if (tap != NULL) {
        tap(true, wire, wire_length + 1u);
    }
    /* rs485_send() dropped the echo, and with it anything half-parsed. */
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
    if ((inbound.flags & TMP_FLAG_RETRY) != 0u && answered_any
        && inbound.sequence == last_sequence) {
        /* The master did not get our answer: the same again. We took this
         * poll's frame the first time. */
        retries++;
    } else {
        if (head_sent) {
            /* The master has moved on, so our last frame arrived (or the
             * master gave up on it). */
            outbound_head = (uint8_t)((outbound_head + 1u) % OUTBOUND_SLOTS);
            outbound_count--;
            head_sent = false;
        }
        inbound_length = inbound.length;
        if (inbound_length != 0u) {
            frames++;
        }
    }
    answered_any = true;
    last_sequence = inbound.sequence;
    if (before_reply != NULL) {
        before_reply();
    }
    reply(inbound.sequence);
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

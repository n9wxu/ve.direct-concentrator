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

/* The last reply, for a poll that asks for it again (TMP_FLAG_RETRY): its
 * SEQ, and whether it carried the head of the queue, which then stays there
 * until a new poll shows the reply arrived. */
static bool    replied;
static uint8_t replied_sequence;
static bool    head_sent;

/* A poll for us that went bad, to be answered with a NAK once the bus is
 * quiet: until then the rest of it may still be coming (its length field may
 * be what was damaged). */
static bool     nak_due;
static uint8_t  nak_sequence;
static uint32_t last_heard_ms;      /* when the last byte was drained */

static void (*before_reply)(void);
static void (*tap)(bool transmitted, const uint8_t *data, size_t length);
static uint32_t last_poll_ms;
static bool     polled;
static uint32_t frames;
static uint32_t retries;
static uint32_t naks;

void link485_init(void)
{
    tmp_scanner_reset(&scanner);
    inbound_length = 0u;
    outbound_head = 0u;
    outbound_count = 0u;
    replied = false;
    head_sent = false;
    nak_due = false;
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

uint32_t link485_naks(void)
{
    return naks;
}

/* The head of the queue arrived: it leaves. */
static void head_delivered(void)
{
    if (head_sent) {
        outbound_head = (uint8_t)((outbound_head + 1u) % OUTBOUND_SLOTS);
        outbound_count--;
        frames++;
        head_sent = false;
    }
}

/* One frame of ours on the wire. */
static void transmit(enum tmp_type type, uint8_t sequence, const uint8_t *payload,
                     uint16_t length)
{
    static uint8_t wire[1u + TMP_FRAME_OVERHEAD + CFG_NET_FRAME];
    wire[0] = 0xFFu;   /* preamble: absorbs the transceivers' turnaround */
    const size_t wire_length = 1u + tmp_frame_build(&wire[1], type, TMP_ADDR_COLLECTOR, sequence,
                                                    tmp_flags(false, 0u), payload, length);
    rs485_send(wire, wire_length);
    if (tap != NULL) {
        tap(true, wire, wire_length);
    }
    /* Whatever the scanner held came before our frame: start the next frame clean.
     * Bytes of the next poll may already be in the ring; they are kept. */
    tmp_scanner_reset(&scanner);
}

/* The answer to a poll: the head of the queue, or nothing. The head stays
 * queued, so a poll that asks again gets the same frame. */
static void reply(uint8_t sequence)
{
    const uint8_t *payload = NULL;
    uint16_t length = 0u;
    if (outbound_count != 0u) {
        payload = outbound[outbound_head].bytes;
        length = outbound[outbound_head].length;
    }
    transmit(TMP_NET, sequence, payload, length);
    replied = true;
    replied_sequence = sequence;
    head_sent = length != 0u;
}

/* The header in the scanner is a poll's for us. By our own address only: a
 * damaged byte can make any frame look like a broadcast, which every slave
 * would answer at once. */
static bool header_is_poll_for_us(void)
{
    return tmp_version_of(scanner.version_type) == TMP_V2_VERSION
        && tmp_type_of(scanner.version_type) == TMP_POLL
        && scanner.address == TMP_ADDR_COLLECTOR;
}

/* The bus has gone quiet. A frame still coming in was cut short: it is
 * dropped, and if it is a poll for us, it went bad like one that failed its
 * CRC. A poll that went bad gets its NAK now, when the master is listening;
 * the poll's frame is not taken and our last reply stays as it was, so the
 * master's next try is answered as if this one had never come. */
static void bus_quiet(void)
{
    if ((scanner.state == SCAN_PAYLOAD || scanner.state == SCAN_CRC) && header_is_poll_for_us()) {
        nak_due = true;
        nak_sequence = scanner.sequence;
    }
    tmp_scanner_reset(&scanner);
    if (nak_due) {
        nak_due = false;
        naks++;
        transmit(TMP_NAK, nak_sequence, NULL, 0u);
    }
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
    bool    heard = false;
    uint8_t byte;

    while (!answered && rs485_getc(&byte)) {
        chunk[filled++] = byte;
        heard = true;
        const enum tmp_scan_result scanned = tmp_scanner_push(&scanner, byte, &inbound);
        if (scanned == SCAN_FRAME_READY && inbound.type == TMP_POLL
            && addressed_to_us(inbound.address)) {
            answered = true;
        } else if (scanned == SCAN_CRC_FAILED && header_is_poll_for_us()) {
            nak_due = true;
            nak_sequence = scanner.sequence;
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
    const uint32_t now_ms = hal_millis();
    if (heard) {
        last_heard_ms = now_ms;
    }
    if (!answered) {
        if (!heard && now_ms - last_heard_ms >= CFG_LINK_QUIET_MS) {
            bus_quiet();
        }
        return;
    }

    nak_due = false;    /* a good poll since: it is the one to answer */
    polled = true;
    last_poll_ms = now_ms;
    if (tmp_is_retry(inbound.flags) && replied && inbound.sequence == replied_sequence) {
        /* Our reply did not reach the master intact: the same again. The
         * poll's frame is the one we took the first time; it is not taken
         * twice (the stack may still be reading that copy). */
        retries++;
    } else {
        /* A new exchange: the master has our last reply. A poll asking again
         * for a reply we never sent (we did not hear the first, or we reset
         * since) is new to us too. */
        head_delivered();
        inbound_length = inbound.length;
        if (inbound_length != 0u) {
            frames++;
        }
    }
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

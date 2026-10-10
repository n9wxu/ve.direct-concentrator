/* The link check: the unit ARPs for recon through the 485, and resets once
 * recon has not answered for CFG_LINK_LOSS_RESET_S — but never while it does. */

#include <string.h>
#include "config.h"
#include "linkcheck.h"
#include "master.h"

static size_t arp_reply(uint8_t *frame)
{
    const size_t length = ethernet(frame, unit_mac, 0x0806u);
    uint8_t *arp = frame + length;
    put16(arp, 1u);
    put16(arp + 2, 0x0800u);
    arp[4] = 6u;
    arp[5] = 4u;
    put16(arp + 6, 2u);
    memcpy(arp + 8, master_mac, 6);
    put32(arp + 14, MASTER_IP);
    memcpy(arp + 18, unit_mac, 6);
    put32(arp + 24, CFG_IP_ADDR);
    return length + 28u;
}

static bool asks_for_master(const struct tmp_frame *frame)
{
    const uint8_t *eth = frame->payload;
    return frame->length >= 42u && get_be16(eth + 12) == 0x0806u && get_be16(eth + 20) == 1u
        && get_be32(eth + 38) == MASTER_IP;
}

static int requests;

/* One second of the unit's main loop, the master polling it throughout;
 * recon answers ARP when `answering`. */
static void second(bool answering, bool polled)
{
    now_ms += 1000u;
    net_tick(&net, 1000u);
    linkcheck_tick(1000u);
    if (!polled) {
        return;
    }
    for (int count = 0; count < 8; count++) {
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        if (!answered || answer.length == 0u) {
            break;
        }
        if (asks_for_master(&answer)) {
            requests++;
            if (answering) {
                uint8_t frame[64];
                poll(TMP_ADDR_COLLECTOR, frame, arp_reply(frame));
            }
        }
    }
}

int main(void)
{
    CHECK(master_unit_init() == NET_OK);
    linkcheck_init(&net);
    const uint32_t limit_s = CFG_LINK_LOSS_RESET_S;

    /* Half an hour with recon answering: probes every 10 s, never a reset. */
    for (uint32_t s = 0u; s < 1800u; s++) {
        second(true, true);
    }
    CHECK(resets == 0);
    CHECK(requests >= (int)(1800u / (LINKCHECK_PROBE_MS / 1000u)) - 1);
    CHECK(linkcheck_silent_ms() <= LINKCHECK_PROBE_MS);

    /* Polled, but recon never hears us (a broken transmit, a turnaround bug):
     * the reset comes at the limit, not before. */
    const uint32_t from = linkcheck_silent_ms() / 1000u;
    for (uint32_t s = from; s < limit_s - 2u; s++) {
        second(false, true);
    }
    CHECK(resets == 0);
    for (int s = 0; s < 4; s++) {
        second(false, true);
    }
    CHECK(resets > 0);

    /* recon answers again: the count starts over. */
    resets = 0;
    for (int s = 0; s < 15; s++) {
        second(true, true);
    }
    CHECK(linkcheck_silent_ms() < 15000u);
    resets = 0;

    /* No master at all: the same reset, from the main loop alone. */
    for (uint32_t s = 0u; s < limit_s + 15u; s++) {
        second(false, false);
    }
    CHECK(resets > 0);

    printf("linkcheck: ok (reset after %u s of silence)\n", (unsigned)limit_s);
    return 0;
}

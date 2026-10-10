/* The 485 link and MAC driver, driven as the master would drive them, with
 * the real IP stack on top: ARP, ping, a UDP command and a TCP open all have
 * to make the round trip through TMP-v2 frames, and the MAC retries what the
 * master did not get. */

#include <string.h>
#include "config.h"
#include "link485.h"
#include "master.h"
#include "tcp.h"
#include "udp.h"

static void echo(net_t *n, uint32_t source_ip, uint16_t source_port, const uint8_t *source_mac,
                 const uint8_t *payload, uint16_t length)
{
    (void)udp_send(n, source_ip, source_mac, CFG_CONTROL_PORT, source_port, payload, length);
}
static const udp_port_entry_t udp_ports[] = { { CFG_CONTROL_PORT, echo } };

static tcp_conn_t       connection;
static tcp_conn_t      *connections[1] = { &connection };
static tcp_saw_tx_ctx_t to_client;
static tcp_saw_rx_ctx_t from_client;
static uint8_t          to_client_bytes[512], from_client_bytes[128];

static int tap_received, tap_transmitted;

static void tap(bool transmitted, const uint8_t *data, size_t length)
{
    (void)data;
    *(transmitted ? &tap_transmitted : &tap_received) += (int)length;
}

int main(void)
{
    static const uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t frame[128];
    size_t  length;

    CHECK(master_unit_init() == NET_OK);
    link485_set_tap(tap);
    udp_set_ports(&net, udp_ports, 1);
    tcp_saw_tx_init(&to_client, to_client_bytes, sizeof to_client_bytes);
    tcp_saw_rx_init(&from_client, from_client_bytes, sizeof from_client_bytes);
    CHECK(tcp_conn_init(&connection, &tcp_saw_tx_ops, &to_client, &tcp_saw_rx_ops, &from_client,
                        NULL) == NET_OK);
    CHECK(tcp_listen(&connection, CFG_VE_PORT_BASE) == NET_OK);
    tcp_set_connections(&net, connections, 1);
    CHECK(link485_poll_age_ms() == UINT32_MAX);

    /* An empty poll gets an empty NET reply with the sequence echoed, after a
     * 0xFF preamble. */
    poll(TMP_ADDR_COLLECTOR, NULL, 0u);
    CHECK(answered && answer.type == TMP_NET && answer.length == 0u);
    CHECK(answer.sequence == sequence && answer.address == TMP_ADDR_COLLECTOR);
    CHECK(wire_out[0] == 0xFFu && wire_out[1] == TMP_SYNC_0);
    CHECK(tap_received == (int)TMP_FRAME_OVERHEAD && tap_transmitted == (int)TMP_FRAME_OVERHEAD + 1);
    now_ms = 25u;
    CHECK(link485_poll_age_ms() == 25u);

    /* Polls for another address get silence. A corrupted poll for us gets a
     * NAK with its SEQ, but only once the bus has been quiet for
     * CFG_LINK_QUIET_MS: until then more of it may be coming. */
    poll(0x02u, NULL, 0u);
    CHECK(!answered && wire_out_length == 0u);
    {
        uint8_t wire[64];
        const size_t wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR, 9u, 0u,
                                                   (const uint8_t *)"abcdef", 6u);
        wire[10] ^= 0x40u;
        exchange_raw(wire, wire_length);
        CHECK(!answered && wire_out_length == 0u);
        quiet(CFG_LINK_QUIET_MS - 1u);
        CHECK(!answered && wire_out_length == 0u);
        quiet(1u);
        CHECK(answered && answer.type == TMP_NAK && answer.sequence == 9u);
        CHECK(answer.address == TMP_ADDR_COLLECTOR && answer.length == 0u);
        CHECK(wire_out[0] == 0xFFu && link485_naks() == 1u);
        quiet(CFG_LINK_QUIET_MS);                           /* once */
        CHECK(!answered && wire_out_length == 0u);
        /* Not for a corrupted poll to another address, or to broadcast: a
         * damaged byte can make any frame look like one. */
        for (int index = 0; index < 2; index++) {
            uint8_t other[64];
            const size_t other_length = tmp_frame_build(other, TMP_POLL,
                                                        index == 0 ? 0x02u : TMP_ADDR_BROADCAST,
                                                        9u, 0u, (const uint8_t *)"abcdef", 6u);
            other[10] ^= 0x40u;
            exchange_raw(other, other_length);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(!answered && wire_out_length == 0u);
        }
        CHECK(link485_naks() == 1u);
        /* A good poll before the bus went quiet is the one answered, and the
         * bad one before it gets no NAK. */
        {
            uint8_t both[128];
            memcpy(both, wire, wire_length);
            const size_t good_length = tmp_frame_build(both + wire_length, TMP_POLL,
                                                       TMP_ADDR_COLLECTOR, 10u, 0u, NULL, 0u);
            exchange_raw(both, wire_length + good_length);
            CHECK(answered && answer.type == TMP_NET && answer.sequence == 10u);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(!answered && wire_out_length == 0u && link485_naks() == 1u);
        }
        /* ... and the link recovers on the next good frame, even after noise. */
        wire[10] ^= 0x40u;
        memmove(wire + 3, wire, wire_length);
        wire[0] = 0x00u; wire[1] = 0xAAu; wire[2] = 0x13u;
        exchange_raw(wire, wire_length + 3u);
        CHECK(answered && answer.sequence == 9u);
    }

    /* ARP: who has the unit's address? The reply to the poll that carries the
     * request is empty (the stack has not seen it yet); the next poll collects
     * the ARP reply. */
    length = ethernet(frame, broadcast, 0x0806u);
    put16(frame + length, 1u); put16(frame + length + 2, 0x0800u);
    frame[length + 4] = 6u; frame[length + 5] = 4u; put16(frame + length + 6, 1u);
    memcpy(frame + length + 8, master_mac, 6); put32(frame + length + 14, MASTER_IP);
    memset(frame + length + 18, 0, 6);          put32(frame + length + 24, CFG_IP_ADDR);
    poll(TMP_ADDR_COLLECTOR, frame, length + 28u);
    CHECK(answered && answer.length == 0u);
    poll(TMP_ADDR_COLLECTOR, NULL, 0u);
    CHECK(answered && answer.length >= 42u);
    CHECK(memcmp(answer.payload, master_mac, 6) == 0 && memcmp(answer.payload + 6, unit_mac, 6) == 0);
    CHECK(answer.payload[12] == 0x08u && answer.payload[13] == 0x06u && answer.payload[21] == 2u);
    CHECK(memcmp(answer.payload + 22, unit_mac, 6) == 0);

    /* Ping. */
    length = ipv4(frame, 1u, 8u + 4u);
    memset(frame + length, 0, 8); frame[length] = 8u;
    memcpy(frame + length + 8, "ping", 4);
    put16(frame + length + 2, checksum(frame + length, 12u, 0u));
    poll(TMP_ADDR_COLLECTOR, frame, length + 12u);
    poll(TMP_ADDR_COLLECTOR, NULL, 0u);
    CHECK(answered && answer.length >= 46u);
    CHECK(answer.payload[23] == 1u && answer.payload[34] == 0u);        /* ICMP echo reply */
    CHECK(memcmp(answer.payload + 42, "ping", 4) == 0);

    /* UDP to the control port (an echo here). */
    length = ipv4(frame, 17u, 8u + 6u);
    put16(frame + length, 40000u); put16(frame + length + 2, CFG_CONTROL_PORT);
    put16(frame + length + 4, 14u); put16(frame + length + 6, 0u);
    memcpy(frame + length + 8, "status", 6);
    put16(frame + length + 6, checksum(frame + length, 14u, pseudo_header(17u, 14u)));
    poll(TMP_ADDR_COLLECTOR, frame, length + 14u);
    poll(TMP_ADDR_COLLECTOR, NULL, 0u);
    CHECK(answered && answer.length == 34u + 8u + 6u);
    CHECK(answer.payload[36] == (40000u >> 8) && answer.payload[37] == (40000u & 0xFFu));
    CHECK(memcmp(answer.payload + 42, "status", 6) == 0);

    /* TCP: a SYN to a VE.Direct port is answered SYN+ACK, with an MSS that
     * fits our frame buffer. */
    length = ipv4(frame, 6u, 20u);
    memset(frame + length, 0, 20);
    put16(frame + length, 40001u); put16(frame + length + 2, CFG_VE_PORT_BASE);
    put32(frame + length + 4, 1000u);
    frame[length + 12] = 0x50u; frame[length + 13] = TCP_FLAG_SYN;
    put16(frame + length + 14, 4096u);
    put16(frame + length + 16, checksum(frame + length, 20u, pseudo_header(6u, 20u)));
    poll(TMP_ADDR_COLLECTOR, frame, length + 20u);
    poll(TMP_ADDR_COLLECTOR, NULL, 0u);
    CHECK(answered && answer.length >= 54u);
    CHECK(answer.payload[23] == 6u);
    CHECK(answer.payload[34 + 13] == (TCP_FLAG_SYN | TCP_FLAG_ACK));
    CHECK(get_be32(&answer.payload[34 + 8]) == 1001u);
    CHECK(answer.payload[54] == TCP_OPT_MSS);
    CHECK(get_be16(&answer.payload[56]) <= CFG_NET_FRAME - 54u);
    CHECK(connection.state == TCP_SYN_RECEIVED);

    /* More frames than the queue holds: the surplus is dropped, the link
     * carries the rest one per poll and then goes back to empty replies. The
     * SYN+ACK still holds its slot: it stays until the next poll shows the
     * master got it. */
    for (int count = 0; count < 10; count++) {
        (void)udp_send(&net, MASTER_IP, master_mac, CFG_CONTROL_PORT, 40000u,
                       (const uint8_t *)"x", 1u);
    }
    int carried = 0;
    for (int count = 0; count < 10; count++) {
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered);
        carried += (answer.length != 0u);
    }
    CHECK(carried == 5);

    /* MAC retry. The master did not get our answer intact and sends the same
     * poll again, flagged RETRY: it gets the same answer, byte for byte, as
     * often as it asks. The poll's frame (a ping) was taken the first time
     * and is not taken again. */
    {
        static uint8_t first[1u + TMP_FRAME_OVERHEAD + CFG_NET_FRAME];
        const uint32_t retried = link485_retries();
        (void)udp_send(&net, MASTER_IP, master_mac, CFG_CONTROL_PORT, 40000u,
                       (const uint8_t *)"one", 3u);
        (void)udp_send(&net, MASTER_IP, master_mac, CFG_CONTROL_PORT, 40000u,
                       (const uint8_t *)"two", 3u);
        length = ipv4(frame, 1u, 8u + 4u);
        memset(frame + length, 0, 8); frame[length] = 8u;
        memcpy(frame + length + 8, "rtry", 4);
        put16(frame + length + 2, checksum(frame + length, 12u, 0u));
        length += 12u;
        poll(TMP_ADDR_COLLECTOR, frame, length);
        CHECK(answered && answer.length == 42u + 3u && memcmp(answer.payload + 42, "one", 3) == 0);
        const size_t first_length = wire_out_length;
        memcpy(first, wire_out, first_length);
        for (uint32_t again = 1u; again <= 2u; again++) {
            retry(TMP_ADDR_COLLECTOR, frame, length);
            CHECK(answered && wire_out_length == first_length);
            CHECK(memcmp(wire_out, first, first_length) == 0);
            CHECK(link485_retries() == retried + again);
        }
        /* The master moves on: the next frame, then the one echo reply. */
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length == 42u + 3u && memcmp(answer.payload + 42, "two", 3) == 0);
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length >= 46u && answer.payload[34] == 0u);
        CHECK(memcmp(answer.payload + 42, "rtry", 4) == 0);
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length == 0u);

        /* An empty answer retried, with a frame queued since: the frame goes
         * in the retry's answer, and once only. */
        (void)udp_send(&net, MASTER_IP, master_mac, CFG_CONTROL_PORT, 40000u,
                       (const uint8_t *)"late", 4u);
        retry(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length == 42u + 4u && memcmp(answer.payload + 42, "late", 4) == 0);
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length == 0u);

        /* A retry of a poll we never heard (not the SEQ we last answered) is
         * new to us: its frame is taken. */
        const uint32_t before = link485_retries();
        sequence++;                                 /* the poll that was lost */
        retry(TMP_ADDR_COLLECTOR, frame, length);
        CHECK(answered && answer.length == 0u && link485_retries() == before);
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length >= 46u && memcmp(answer.payload + 42, "rtry", 4) == 0);

        /* After a reset we have answered nothing: a retry is new to us too,
         * whatever its SEQ. */
        link485_init();
        retry(TMP_ADDR_COLLECTOR, frame, length);
        CHECK(answered && answer.length == 0u && link485_retries() == before);
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        CHECK(answered && answer.length >= 46u && memcmp(answer.payload + 42, "rtry", 4) == 0);

        /* A poll that went bad on the wire (its CRC broken) gets a NAK once
         * the bus is quiet, and its frame is not taken. The master sends it
         * again with TMP_FLAG_RETRY; to us, who never answered that SEQ, it is
         * new, and its frame is taken once. */
        {
            uint8_t wire[128];
            const uint32_t naks = link485_naks();
            sequence++;
            const size_t wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR,
                                                       sequence, tmp_flags(false, 0u), frame,
                                                       length);
            wire[wire_length - 1u] ^= 0xFFu;
            exchange_raw(wire, wire_length);
            CHECK(!answered);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(answered && answer.type == TMP_NAK && answer.sequence == sequence);
            CHECK(link485_naks() == naks + 1u);
            retry(TMP_ADDR_COLLECTOR, frame, length);
            CHECK(answered && answer.type == TMP_NET && answer.length == 0u);
            poll(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.length >= 46u && memcmp(answer.payload + 42, "rtry", 4) == 0);
            poll(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.length == 0u);              /* one echo */
        }

        /* A NAK leaves our last reply as it was: a retry that went bad itself
         * is NAKed, and the next retry still gets the same reply, whose frame
         * leaves only with a new poll. */
        {
            uint8_t wire[64];
            (void)udp_send(&net, MASTER_IP, master_mac, CFG_CONTROL_PORT, 40000u,
                           (const uint8_t *)"nak", 3u);
            poll(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.length == 42u + 3u && memcmp(answer.payload + 42, "nak", 3) == 0);
            const size_t first_length = wire_out_length;
            memcpy(first, wire_out, first_length);
            const size_t wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR,
                                                       sequence,
                                                       (uint8_t)(tmp_flags(false, 0u)
                                                                 | TMP_FLAG_RETRY), NULL, 0u);
            wire[wire_length - 1u] ^= 0xFFu;
            exchange_raw(wire, wire_length);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(answered && answer.type == TMP_NAK && answer.sequence == sequence);
            retry(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && wire_out_length == first_length);
            CHECK(memcmp(wire_out, first, first_length) == 0);
            poll(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.length == 0u);
        }

        /* A poll whose length field went bad stops short of it: we wait for
         * the rest, and when the bus goes quiet, NAK it and start over, so
         * the master's next try is heard. A frame for another address cut
         * short is dropped the same way, without a NAK. A pause inside a
         * frame shorter than the quiet does not cut it. */
        {
            uint8_t wire[64];
            const uint32_t naks = link485_naks();
            sequence++;
            size_t wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR, sequence,
                                                 tmp_flags(false, 0u), (const uint8_t *)"abc", 3u);
            wire[6] = 0x01u;                                     /* LEN 3 -> 259 */
            exchange_raw(wire, wire_length);
            CHECK(!answered && wire_out_length == 0u);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(answered && answer.type == TMP_NAK && answer.sequence == sequence);
            CHECK(link485_naks() == naks + 1u);
            retry(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.type == TMP_NET && answer.sequence == sequence);

            wire_length = tmp_frame_build(wire, TMP_POLL, 0x02u, 1u, 0u, (const uint8_t *)"abc", 3u);
            exchange_raw(wire, wire_length - 2u);
            quiet(CFG_LINK_QUIET_MS);
            CHECK(!answered && link485_naks() == naks + 1u);
            poll(TMP_ADDR_COLLECTOR, NULL, 0u);
            CHECK(answered && answer.type == TMP_NET && answer.sequence == sequence);

            sequence++;
            wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR, sequence,
                                          tmp_flags(false, 0u), (const uint8_t *)"abc", 3u);
            exchange_raw(wire, 9u);
            quiet(CFG_LINK_QUIET_MS - 1u);
            CHECK(!answered);
            exchange_raw(wire + 9, wire_length - 9u);
            CHECK(answered && answer.type == TMP_NET && answer.sequence == sequence);
            CHECK(link485_naks() == naks + 1u);
        }
    }

    printf("link: ok\n");
    return 0;
}

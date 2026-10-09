/* The load control stream, as recon would use it: a TCP session to
 * CFG_LOAD_PORT through TMP-v2 frames, the real IP stack and fake switches. */

#include <string.h>
#include "config.h"
#include "hal.h"
#include "link485.h"
#include "loadctl.h"
#include "master.h"
#include "tcp.h"

/* ── Fake load switches ─────────────────────────────────────────────── */

static uint8_t  outputs;
static uint16_t sense_mv[2];

void     loads_init(void) { outputs = 0u; }
void     loads_set(uint8_t value) { outputs = value; }
uint8_t  loads_outputs(void) { return outputs; }
uint16_t loads_sense_mv(uint8_t load) { return sense_mv[load]; }

/* ── A TCP client ───────────────────────────────────────────────────── */

static uint16_t client_port = 40005u;
static uint32_t client_next;      /* our next sequence number   */
static uint32_t unit_next;        /* the unit's, as we have seen */
static uint8_t  unit_flags;       /* of its last segment          */
static char     received[2048];   /* what the unit sent, since received_reset() */
static size_t   received_length;

static void received_reset(void)
{
    received_length = 0u;
    received[0] = '\0';
}

/* A segment from the unit, if the frame is one. */
static void take(const struct tmp_frame *frame)
{
    const uint8_t *eth = frame->payload;
    if (frame->length < 54u || get_be16(eth + 12) != 0x0800u || eth[23] != 6u) {
        return;
    }
    const uint8_t *tcp = eth + 34;
    const size_t header = (size_t)(tcp[12] >> 4) * 4u;
    const size_t data_length = get_be16(eth + 16) - 20u - header;
    unit_flags = tcp[13];
    unit_next = get_be32(tcp + 4) + (uint32_t)data_length
              + ((unit_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ? 1u : 0u);
    memcpy(&received[received_length], tcp + header, data_length);
    received_length += data_length;
    received[received_length] = '\0';
}

/* Poll until the unit has nothing more to send. What it queues during one
 * exchange rides out on the next, so two empty replies in a row mean done. */
static void collect(void)
{
    int empty = 0;
    for (int count = 0; count < 16 && empty < 2; count++) {
        poll(TMP_ADDR_COLLECTOR, NULL, 0u);
        if (answered && answer.length != 0u) {
            take(&answer);
            empty = 0;
        } else {
            empty++;
        }
    }
}

static void send_segment(uint8_t flags, const char *data)
{
    static uint8_t frame[256];
    const size_t data_length = data != NULL ? strlen(data) : 0u;
    const size_t length = ipv4(frame, 6u, 20u + data_length);
    uint8_t *tcp = frame + length;
    memset(tcp, 0, 20);
    put16(tcp, client_port);
    put16(tcp + 2, CFG_LOAD_PORT);
    put32(tcp + 4, client_next);
    put32(tcp + 8, (flags & TCP_FLAG_ACK) ? unit_next : 0u);
    tcp[12] = 0x50u;
    tcp[13] = flags;
    put16(tcp + 14, 4096u);
    memcpy(tcp + 20, data, data_length);
    put16(tcp + 16, checksum(tcp, 20u + data_length, pseudo_header(6u, 20u + data_length)));
    client_next += (uint32_t)data_length + ((flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ? 1u : 0u);

    poll(TMP_ADDR_COLLECTOR, frame, length + 20u + data_length);
    if (answered && answer.length != 0u) {
        take(&answer);
    }
    collect();
}

static void say(const char *text)
{
    received_reset();
    send_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, text);
}

static void acknowledge(void)
{
    send_segment(TCP_FLAG_ACK, NULL);
}

static int open_session(void)
{
    client_next = 1000u;
    received_reset();
    send_segment(TCP_FLAG_SYN, NULL);
    CHECK(unit_flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
    send_segment(TCP_FLAG_ACK, NULL);   /* the greeting follows at once */
    return 0;
}

static const char usage[] = "error commands: status, out <0-3>, on <1|2>, off <1|2>\n";

int main(void)
{
    static tcp_conn_t *connections[1];

    CHECK(master_unit_init() == NET_OK);
    loads_init();
    loadctl_init(&net);
    connections[0] = loadctl_connection();
    tcp_set_connections(&net, connections, 1);
    unit_service = loadctl_service;
    CHECK(!loadctl_connected());

    /* A new client is greeted with the state. */
    CHECK(open_session() == 0);
    CHECK(loadctl_connected());
    CHECK(strcmp(received, "out 0 sense 0 0\n") == 0);
    acknowledge();

    /* One answer per command, in order; CRLF is fine. */
    say("on 2\r\nstatus\nbogus\nout 9\n");
    CHECK(outputs == 2u);
    {
        char expected[256];
        snprintf(expected, sizeof expected, "out 2 sense 0 0\nout 2 sense 0 0\n%s%s", usage, usage);
        CHECK(strcmp(received, expected) == 0);
    }
    acknowledge();
    say("on 1\n");
    CHECK(outputs == 3u && strcmp(received, "out 3 sense 0 0\n") == 0);
    acknowledge();
    say("off 2\n");
    CHECK(outputs == 1u && strcmp(received, "out 1 sense 0 0\n") == 0);
    acknowledge();
    say("out 0\n");
    CHECK(outputs == 0u && strcmp(received, "out 0 sense 0 0\n") == 0);
    acknowledge();

    /* A line too long for the unit is refused whole, and the next one works. */
    say("on 1 and some more words to make it long\nstatus\n");
    CHECK(outputs == 0u && strcmp(received, "error line too long\nout 0 sense 0 0\n") == 0);
    acknowledge();

    /* Every second while quiet: the state, with the currents as they are now. */
    received_reset();
    now_ms += 500u;
    collect();
    CHECK(received_length == 0u);
    sense_mv[0] = 418u;
    sense_mv[1] = 7u;
    now_ms += 500u;
    collect();
    CHECK(strcmp(received, "out 0 sense 418 7\n") == 0);
    acknowledge();
    received_reset();
    sense_mv[0] = 3300u;           /* the widest line there is */
    sense_mv[1] = 3300u;
    now_ms += 1000u;
    collect();
    CHECK(strcmp(received, "out 0 sense 3300 3300\n") == 0);
    acknowledge();

    /* A change made elsewhere (UDP "out") is reported at once. */
    received_reset();
    loads_set(2u);
    collect();
    CHECK(strcmp(received, "out 2 sense 3300 3300\n") == 0);
    acknowledge();

    /* The client closes; the unit closes too and listens again. */
    received_reset();
    send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, NULL);
    CHECK(unit_flags & TCP_FLAG_FIN);
    acknowledge();
    CHECK(!loadctl_connected());
    collect();
    client_port++;
    CHECK(open_session() == 0);
    CHECK(loadctl_connected());
    CHECK(strcmp(received, "out 2 sense 3300 3300\n") == 0);

    /* The client closes, our FIN goes, and the client answers it with a
     * reset instead of an ACK (Linux does, when data reaches a socket its
     * application has closed). The stack closes the connection without an
     * event; the port listens again all the same. */
    acknowledge();
    send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, NULL);
    CHECK(unit_flags & TCP_FLAG_FIN);
    send_segment(TCP_FLAG_RST, NULL);
    CHECK(!loadctl_connected());
    collect();
    client_port++;
    CHECK(open_session() == 0);
    CHECK(loadctl_connected());

    printf("loadctl: ok\n");
    return 0;
}

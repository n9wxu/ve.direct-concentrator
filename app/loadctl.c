#include "loadctl.h"
#include <string.h>
#include "config.h"
#include "hal.h"

#define LINE_BYTES      32u      /* longest command; longer lines are refused */
#define QUEUE_BYTES     256u     /* lines waiting to be sent, also the TX buffer */
#define ANSWER_BYTES    64u      /* room kept for one answer before reading on */
#define REPORT_MS       1000u    /* the state, with fresh currents, this often */
#define STATE_BYTES     24u      /* "out 3 sense 3300 3300\n" and room to spare */

static const char usage[] = "error commands: status, out <0-3>, on <1|2>, off <1|2>\n";

static tcp_conn_t       connection;   /* first: the event callback gets a pointer to it */
static tcp_saw_tx_ctx_t to_client;
static tcp_saw_rx_ctx_t from_client;
static uint8_t          to_client_bytes[QUEUE_BYTES];
static uint8_t          from_client_bytes[64];
static net_t           *network;

/* Set by the event callback, acted on from the main loop. */
static volatile uint8_t opened, peer_closed, gone;

static char     line[LINE_BYTES];
static uint8_t  line_length;
static bool     line_too_long;

static char     queue[QUEUE_BYTES];
static uint16_t queued;
static bool     state_owed;      /* a state line did not fit the queue */
static uint8_t  reported;        /* the outputs in the last state line */
static uint32_t sent_ms;         /* when we last sent anything */

static void on_event(tcp_conn_t *conn, uint8_t events)
{
    (void)conn;
    if (events & TCP_EVT_CONNECTED) {
        opened = 1u;
    }
    if (events & TCP_EVT_CLOSED) {
        peer_closed = 1u;
    }
    if (events & (TCP_EVT_RESET | TCP_EVT_ERROR)) {
        gone = 1u;
    }
}

static void listen(void)
{
    tcp_saw_tx_init(&to_client, to_client_bytes, sizeof to_client_bytes);
    tcp_saw_rx_init(&from_client, from_client_bytes, sizeof from_client_bytes);
    (void)tcp_conn_init(&connection, &tcp_saw_tx_ops, &to_client, &tcp_saw_rx_ops, &from_client,
                        on_event);
    (void)tcp_listen(&connection, CFG_LOAD_PORT);
    opened = peer_closed = gone = 0u;
}

void loadctl_init(net_t *net)
{
    network = net;
    listen();
}

tcp_conn_t *loadctl_connection(void)
{
    return &connection;
}

bool loadctl_connected(void)
{
    return connection.state == TCP_ESTABLISHED;
}

static char *decimal(char *at, uint16_t value)
{
    char digits[5];
    uint8_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        *at++ = digits[--count];
    }
    return at;
}

static void queue_text(const char *text, uint16_t length)
{
    if (queued + length <= QUEUE_BYTES) {
        memcpy(&queue[queued], text, length);
        queued = (uint16_t)(queued + length);
    }
}

/* "out <0-3> sense <mV> <mV>" */
static void queue_state(void)
{
    char text[STATE_BYTES];
    if (queued + sizeof text > QUEUE_BYTES) {
        state_owed = true;
        return;
    }
    reported = loads_outputs();
    char *at = text;
    memcpy(at, "out ", 4u);
    at = decimal(at + 4, reported);
    memcpy(at, " sense ", 7u);
    at = decimal(at + 7, loads_sense_mv(0u));
    *at++ = ' ';
    at = decimal(at, loads_sense_mv(1u));
    *at++ = '\n';
    queue_text(text, (uint16_t)(at - text));
    state_owed = false;
}

static bool named(const char *text, uint8_t length, const char *name)
{
    return length == strlen(name) && memcmp(text, name, length) == 0;
}

static void execute(void)
{
    uint8_t length = line_length;
    while (length != 0u && line[length - 1u] == ' ') {
        length--;
    }
    if (length == 0u) {
        return;
    }
    uint8_t name_length = 0u;
    while (name_length < length && line[name_length] != ' ') {
        name_length++;
    }
    uint8_t at = name_length;
    while (at < length && line[at] == ' ') {
        at++;
    }
    /* Every command takes at most one digit. */
    const bool bare = (at == length);
    const char digit = (at + 1u == length) ? line[at] : '\0';

    if (named(line, name_length, "status") && bare) {
        queue_state();
    } else if (named(line, name_length, "out") && digit >= '0' && digit <= '3') {
        loads_set((uint8_t)(digit - '0'));
        queue_state();
    } else if ((named(line, name_length, "on") || named(line, name_length, "off"))
               && (digit == '1' || digit == '2')) {
        const bool on = named(line, name_length, "on");
        const uint8_t bit = (digit == '1') ? 1u : 2u;
        const uint8_t outputs = loads_outputs();
        loads_set(on ? (uint8_t)(outputs | bit) : (uint8_t)(outputs & ~bit));
        queue_state();
    } else {
        queue_text(usage, sizeof usage - 1u);
    }
}

static void read_commands(void)
{
    bool took = false;
    uint8_t byte;
    /* Leave the rest in the receive buffer while the answers wait: the
     * window closes and the client waits too. */
    while (QUEUE_BYTES - queued >= ANSWER_BYTES && tcp_recv(&connection, &byte, 1u) == 1u) {
        took = true;
        if (byte == '\n') {
            if (line_too_long) {
                static const char refused[] = "error line too long\n";
                queue_text(refused, sizeof refused - 1u);
            } else {
                execute();
            }
            line_length = 0u;
            line_too_long = false;
        } else if (byte == '\r') {
            /* CRLF clients */
        } else if (line_length < LINE_BYTES) {
            line[line_length++] = (char)byte;
        } else {
            line_too_long = true;
        }
    }
    if (took) {
        tcp_window_update(network, &connection);
    }
}

/* Output changes made elsewhere (UDP "out") at once, and the state with
 * fresh currents every REPORT_MS while nothing else is going out. */
static void watch(uint32_t now)
{
    if (loads_outputs() != reported) {
        state_owed = true;
    }
    if (now - sent_ms >= REPORT_MS && queued == 0u) {
        state_owed = true;
    }
    if (state_owed) {
        queue_state();
    }
}

static void flush(uint32_t now)
{
    if (queued == 0u || !tcp_tx_idle(&connection)) {
        return;
    }
    const int taken = tcp_send(network, &connection, (const uint8_t *)queue, queued);
    if (taken > 0) {
        queued = (uint16_t)(queued - (uint16_t)taken);
        memmove(queue, &queue[taken], queued);
        sent_ms = now;
    }
}

void loadctl_service(void)
{
    const uint32_t now = hal_millis();

    if (peer_closed) {
        peer_closed = 0u;
        if (connection.state == TCP_CLOSE_WAIT) {
            (void)tcp_close(network, &connection);   /* our FIN answers theirs */
        } else if (connection.state == TCP_CLOSED) {
            gone = 1u;
        }
    }
    if (gone) {
        listen();
        return;
    }
    if (opened) {
        opened = 0u;
        queued = 0u;
        line_length = 0u;
        line_too_long = false;
        queue_state();
    }
    if (connection.state != TCP_ESTABLISHED) {
        return;
    }
    read_commands();
    watch(now);
    flush(now);
}

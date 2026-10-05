#include "veports.h"
#include "board.h"
#include "config.h"
#include "hal.h"
#include "tcp.h"
#include "uart.h"

#define TO_CLIENT_BYTES     512u   /* also the largest segment we send      */
#define FROM_CLIENT_BYTES   128u   /* also the receive window we advertise  */
#define GATHER_MS           4u     /* line idle this long: send what we have */

struct ve_port {
    tcp_conn_t       connection;   /* first: the event callback gets a pointer to it */
    tcp_saw_tx_ctx_t to_client;
    tcp_saw_rx_ctx_t from_client;
    uint8_t          to_client_bytes[TO_CLIENT_BYTES];
    uint8_t          from_client_bytes[FROM_CLIENT_BYTES];
    enum uart_id     uart;
    struct traffic_led led;        /* blinks with bytes either way */
    uint16_t         tcp_port;
    /* Set by the event callback, acted on from the main loop. */
    volatile uint8_t data, peer_closed, gone;
    size_t           seen;         /* receive ring level at the last look */
    uint32_t         seen_ms;      /* when it last grew                   */
    uint32_t         rx_bytes;
    uint32_t         tx_bytes;
};

static struct ve_port ports[VE_PORT_COUNT] = {
    { .uart = UART_VE1, .led = { .pin = PIN_LED_VE1 } },
    { .uart = UART_VE2, .led = { .pin = PIN_LED_VE2 } },
    { .uart = UART_VE3, .led = { .pin = PIN_LED_VE3 } },
    { .uart = UART_VE4, .led = { .pin = PIN_LED_VE4 } },
};
static net_t *network;
static void (*tap)(uint8_t port, const uint8_t *data, size_t length);

static void on_event(tcp_conn_t *connection, uint8_t events)
{
    struct ve_port *port = (struct ve_port *)connection;
    if (events & TCP_EVT_DATA) {
        port->data = 1u;
    }
    if (events & TCP_EVT_CLOSED) {
        port->peer_closed = 1u;
    }
    if (events & (TCP_EVT_RESET | TCP_EVT_ERROR)) {
        port->gone = 1u;
    }
}

static void listen(struct ve_port *port)
{
    tcp_saw_tx_init(&port->to_client, port->to_client_bytes, sizeof port->to_client_bytes);
    tcp_saw_rx_init(&port->from_client, port->from_client_bytes, sizeof port->from_client_bytes);
    (void)tcp_conn_init(&port->connection, &tcp_saw_tx_ops, &port->to_client,
                        &tcp_saw_rx_ops, &port->from_client, on_event);
    (void)tcp_listen(&port->connection, port->tcp_port);
    port->data = port->peer_closed = port->gone = 0u;
}

void veports_init(net_t *net)
{
    network = net;
    for (uint8_t index = 0u; index < VE_PORT_COUNT; index++) {
        struct ve_port *port = &ports[index];
        port->tcp_port = (uint16_t)(CFG_VE_PORT_BASE + index);
        led_init(port->led.pin);
        uart_init(port->uart, CFG_VE_BAUD);
        listen(port);
    }
}

tcp_conn_t *veports_connection(uint8_t index)
{
    return &ports[index].connection;
}

void veports_set_tap(void (*function)(uint8_t, const uint8_t *, size_t))
{
    tap = function;
}

static bool open_to_client(const struct ve_port *port)
{
    return port->connection.state == TCP_ESTABLISHED;
}

static void device_to_client(struct ve_port *port, uint32_t now)
{
    const size_t waiting = uart_rx_available(port->uart);
    if (waiting != port->seen) {
        if (waiting > port->seen) {
            port->rx_bytes += (uint32_t)(waiting - port->seen);
        }
        port->seen = waiting;
        port->seen_ms = now;
    }
    if (waiting == 0u) {
        return;
    }

    uint8_t bytes[TO_CLIENT_BYTES];
    if (!open_to_client(port)) {
        /* Nobody listening: VE.Direct repeats itself every second, keep nothing. */
        (void)uart_read(port->uart, bytes, sizeof bytes);
        port->seen = uart_rx_available(port->uart);
        return;
    }
    /* Stop-and-wait: one segment in flight. Send a whole burst rather than a
     * trickle of tiny segments. */
    if (!tcp_tx_idle(&port->connection)) {
        return;
    }
    if (waiting < TO_CLIENT_BYTES / 2u && now - port->seen_ms < GATHER_MS) {
        return;
    }
    const size_t count = uart_read(port->uart, bytes, sizeof bytes);
    port->seen = uart_rx_available(port->uart);
    (void)tcp_send(network, &port->connection, bytes, (uint16_t)count);
}

static void client_to_device(struct ve_port *port)
{
    uint8_t bytes[32];
    for (;;) {
        size_t room = uart_tx_free(port->uart);
        if (room == 0u) {
            port->data = 1u;    /* come back when the UART has drained */
            return;
        }
        if (room > sizeof bytes) {
            room = sizeof bytes;
        }
        const uint16_t count = tcp_recv(&port->connection, bytes, (uint16_t)room);
        if (count == 0u) {
            break;
        }
        port->tx_bytes += (uint32_t)uart_write(port->uart, bytes, count);
        if (tap != NULL) {
            tap((uint8_t)(port - ports), bytes, count);
        }
    }
    tcp_window_update(network, &port->connection);
}

void veports_service(void)
{
    const uint32_t now = hal_millis();
    for (uint8_t index = 0u; index < VE_PORT_COUNT; index++) {
        struct ve_port *port = &ports[index];

        if (port->data) {
            port->data = 0u;
            client_to_device(port);
        }
        if (port->peer_closed) {
            port->peer_closed = 0u;
            if (port->connection.state == TCP_CLOSE_WAIT) {
                (void)tcp_close(network, &port->connection);   /* our FIN answers theirs */
            } else if (port->connection.state == TCP_CLOSED) {
                port->gone = 1u;
            }
        }
        if (port->gone) {
            listen(port);
        }
        device_to_client(port, now);
        traffic_led_update(&port->led, port->rx_bytes + port->tx_bytes, now);
    }
}

uint32_t veports_rx_bytes(uint8_t index)
{
    return ports[index].rx_bytes;
}

bool veports_connected(uint8_t index)
{
    return open_to_client(&ports[index]);
}

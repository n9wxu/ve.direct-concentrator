#include "tcplink.h"
#include <stdbool.h>
#include "link485.h"
#include "tcp.h"

static net_t   *network;
static uint32_t retransmissions;

void tcplink_init(net_t *net)
{
    network = net;
    retransmissions = 0u;
    link485_set_on_idle_poll(tcplink_idle_poll);
}

uint32_t tcplink_retransmissions(void)
{
    return retransmissions;
}

/* A peer on our subnet: the master's host, one hop away. */
static bool on_link(const tcp_conn_t *conn)
{
    return conn->remote_ip != 0u
        && ((conn->remote_ip ^ network->ipv4_addr) & network->subnet_mask) == 0u;
}

void tcplink_idle_poll(void)
{
    if (network == NULL) {
        return;
    }
    for (uint8_t index = 0u; index < network->tcp_conn_count; index++) {
        tcp_conn_t *conn = network->tcp_conns[index];
        /* NET_ERR_NO_FRAME: nothing unacknowledged, nothing sent. */
        if (conn != NULL && on_link(conn) && tcp_retransmit(network, conn) == NET_OK) {
            retransmissions++;
        }
    }
}

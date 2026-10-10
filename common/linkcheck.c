#include "linkcheck.h"
#include "arp.h"
#include "config.h"
#include "hal.h"

static net_t   *network;
static uint32_t silent_ms;
static uint32_t probe_in_ms;
static uint16_t last_mac_s;     /* the gateway entry's lifetime at the last look */

void linkcheck_init(net_t *net)
{
    network = net;
    silent_ms = 0u;
    probe_in_ms = 0u;
    last_mac_s = 0u;
}

uint32_t linkcheck_silent_ms(void)
{
    return silent_ms;
}

void linkcheck_tick(uint32_t elapsed_ms)
{
    /* A reply from the gateway restarts its entry's lifetime: the only time
     * that number goes up. */
    const uint16_t mac_s = network->gateway_mac_valid ? network->gateway_mac_s : 0u;
    if (mac_s > last_mac_s) {
        silent_ms = 0u;
    } else {
        silent_ms += elapsed_ms;
    }
    last_mac_s = mac_s;

    if (silent_ms >= CFG_LINK_LOSS_RESET_S * 1000u) {
        hal_reset();
    }
    if (probe_in_ms <= elapsed_ms) {
        probe_in_ms = LINKCHECK_PROBE_MS;
        (void)arp_request(network, network->gateway_ipv4);
    } else {
        probe_in_ms -= elapsed_ms;
    }
}

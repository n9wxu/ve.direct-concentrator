#include "board.h"
#include "config.h"
#include "control.h"
#include "ledtest.h"
#include "linkcheck.h"
#include "fota.h"
#include "hal.h"
#include "link485.h"
#include "loadctl.h"
#include "net.h"
#include "tcp.h"
#include "tmp_v2.h"
#include "uart.h"
#include "udp.h"
#include "usbmon.h"
#include "vemock.h"
#include "veports.h"

/* VE.Direct concentrator.
 *
 * The unit is a TMP-v2 slave on the RS-485 pair and an IP host behind it
 * (recon's tmptun/tap0 is the other end). Each of the four VE.Direct serial
 * ports is a raw TCP stream; nothing is decoded here. A fifth TCP stream
 * switches the loads. Two USB serial ports serve the bench: one mirrors the
 * 485 traffic, the other stands in for VE.Direct devices (vemock.h).
 *
 * Each VE.Direct LED blinks with its port's bytes, the bus LED with the
 * frames that cross the 485, the USB LED with USB bytes. The health LED
 * blinks at 1 Hz while the master polls us and at 4 Hz once it has been quiet
 * for 2 s; dark or stuck, the main loop is not running. */

static net_t   net;
static uint8_t rx_frame[CFG_NET_FRAME];
static uint8_t tx_frame[CFG_NET_FRAME];

static tcp_conn_t *connections[VE_PORT_COUNT + 1u];
static struct traffic_led bus_led = { .pin = PIN_LED_BUS };
static struct traffic_led usb_led = { .pin = PIN_LED_USB };

#define POLLED_MS   2000u   /* the master counts as present this long after a poll */

static void health_led(uint32_t now)
{
    const uint32_t half_period = (link485_poll_age_ms() < POLLED_MS) ? 500u : 125u;
    led_write(PIN_LED_HEALTH, ((now / half_period) & 1u) != 0u);
}

static const udp_port_entry_t udp_ports[] = {
    { CFG_CONTROL_PORT, control_input },
    { CFG_TFTP_PORT,    fota_udp_input },
};

int main(void)
{
    static const uint8_t mac[6] = CFG_MAC_ADDR;

    clock_init();
    loads_init();          /* after the clocks: the ADC runs from GCLK0 */
    watchdog_start();
#if CFG_LED_TEST
    __enable_irq();
    ledtest_run();
#endif
    pin_output(PIN_485_DE);
    led_init(PIN_LED_BUS);
    led_init(PIN_LED_HEALTH);
    led_init(PIN_LED_USB);
    uart_init(UART_485, TMP_V2_BAUD);

    link485_init();
    link485_set_tap(usbmon_tap);
    link485_set_before_reply(fota_flush);
    (void)net_init(&net, rx_frame, sizeof rx_frame, tx_frame, sizeof tx_frame, mac,
                   &link485_mac, NULL);
    net.ipv4_addr = CFG_IP_ADDR;
    net.subnet_mask = CFG_NETMASK;
    net.gateway_ipv4 = CFG_SERVER_IP;
    net.mtu = CFG_NET_FRAME - 14u;
    udp_set_ports(&net, udp_ports, 2);

    control_init(&net);
    fota_init(&net);
    veports_init(&net);
    loadctl_init(&net);
    for (uint8_t port = 0u; port < VE_PORT_COUNT; port++) {
        connections[port] = veports_connection(port);
    }
    connections[VE_PORT_COUNT] = loadctl_connection();
    tcp_set_connections(&net, connections, VE_PORT_COUNT + 1u);
    veports_set_tap(vemock_tap);
    linkcheck_init(&net);
    usbmon_init();
    __enable_irq();

    uint32_t last_ms = hal_millis();
    for (;;) {
        watchdog_kick();
        link485_service();
        (void)net_poll(&net);
        veports_service();
        loadctl_service();
        usbmon_service();
        vemock_service();

        const uint32_t now = hal_millis();
        traffic_led_update(&bus_led, link485_frames(), now);
        traffic_led_update(&usb_led, usbmon_bytes() + vemock_bytes(), now);
        health_led(now);
        const uint32_t elapsed = now - last_ms;
        if (elapsed != 0u) {
            last_ms = now;
            net_tick(&net, elapsed);
            fota_tick(elapsed);
            control_tick(elapsed);
            linkcheck_tick(elapsed);
        }
    }
}

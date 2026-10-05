#include <string.h>
#include "board.h"
#include "config.h"
#include "fota.h"
#include "hal.h"
#include "image.h"
#include "link485.h"
#include "net.h"
#include "tmp_v2.h"
#include "uart.h"
#include "udp.h"

/* Bootloader: a TMP-v2 slave with just enough IP to fetch the application by
 * TFTP from recon.
 *
 *   - Told to run the application (BOOT_REQUEST_RUN): start it at once, from
 *     the clean state stage0 left.
 *   - Application valid and nobody asked us to stay: answer polls for
 *     CFG_BOOT_WINDOW_MS, listening for a "stay" command, then run it.
 *   - Otherwise fetch CFG_APP_FILE, again and again until a valid image is in
 *     the slot.
 *
 * Commands (UDP port CFG_CONTROL_PORT, ASCII): status, stay, run. */

#define RETRY_MS  5000u

static net_t   net;
static uint8_t rx_frame[CFG_NET_FRAME];
static uint8_t tx_frame[CFG_NET_FRAME];

static struct traffic_led bus_led = { .pin = PIN_LED_BUS };

static bool     staying;
static bool     leave_requested;
static uint32_t leave_after_ms;
static uint32_t retry_ms;

static bool app_valid(void)
{
    return image_valid((const uint8_t *)(uintptr_t)APP_BASE, APP_SIZE, IMAGE_MAGIC_APP);
}

static void run_app(void)
{
    BOOT_REQUEST = BOOT_REQUEST_RUN;
    hal_reset();
}

static bool is_command(const uint8_t *payload, uint16_t length, const char *word)
{
    const size_t size = strlen(word);
    return length >= size && memcmp(payload, word, size) == 0
        && (length == size || payload[size] <= ' ');
}

static void control_input(net_t *n, uint32_t source_ip, uint16_t source_port,
                          const uint8_t *source_mac, const uint8_t *payload, uint16_t length)
{
    const char *answer = "error unknown command (status, stay, run)\n";
    if (is_command(payload, length, "status")) {
        answer = fota_busy() ? "bootloader fetching\n"
               : app_valid() ? "bootloader app-valid\n" : "bootloader app-invalid\n";
    } else if (is_command(payload, length, "stay") || is_command(payload, length, "app")) {
        staying = true;
        answer = "ok fetching " CFG_APP_FILE "\n";
    } else if (is_command(payload, length, "run")) {
        if (app_valid() && !fota_busy()) {
            leave_requested = true;
            leave_after_ms = 200u;
            answer = "ok starting application\n";
        } else {
            answer = "error no valid application\n";
        }
    }
    (void)udp_send(n, source_ip, source_mac, CFG_CONTROL_PORT, source_port,
                   (const uint8_t *)answer, (uint16_t)strlen(answer));
}

static const udp_port_entry_t udp_ports[] = {
    { CFG_CONTROL_PORT, control_input },
    { CFG_TFTP_PORT,    fota_udp_input },
};

int main(void)
{
    static const uint8_t mac[6] = CFG_MAC_ADDR;

    const uint32_t request = BOOT_REQUEST;
    BOOT_REQUEST = 0u;
    if (request == BOOT_REQUEST_RUN && app_valid()) {
        const uint32_t *vectors = (const uint32_t *)(uintptr_t)APP_BASE;
        SCB->VTOR = APP_BASE;
        __asm__ volatile("msr msp, %0\n bx %1" : : "r"(vectors[0]), "r"(vectors[1]));
    }
    staying = (request == BOOT_REQUEST_STAY) || !app_valid();

    clock_init();
    watchdog_start();
    pin_output(PIN_485_DE);
    pin_output(PIN_LED_BUS);
    if (PIN_DRIVE_1 != PIN_NONE) {
        pin_output(PIN_DRIVE_1);     /* loads off while there is no application */
    }
    if (PIN_DRIVE_2 != PIN_NONE) {
        pin_output(PIN_DRIVE_2);
    }
    uart_init(UART_485, TMP_V2_BAUD);
    __enable_irq();

    link485_init();
    link485_set_before_reply(fota_flush);
    (void)net_init(&net, rx_frame, sizeof rx_frame, tx_frame, sizeof tx_frame, mac,
                   &link485_mac, NULL);
    net.ipv4_addr = CFG_IP_ADDR;
    net.subnet_mask = CFG_NETMASK;
    net.gateway_ipv4 = CFG_SERVER_IP;
    net.mtu = CFG_NET_FRAME - 14u;
    udp_set_ports(&net, udp_ports, 2);
    fota_init(&net);

    uint32_t last_ms = hal_millis();
    uint32_t window_ms = CFG_BOOT_WINDOW_MS;
    uint32_t leave_ms = 0u;

    for (;;) {
        watchdog_kick();
        link485_service();
        (void)net_poll(&net);

        const uint32_t now = hal_millis();
        traffic_led_update(&bus_led, link485_frames(), now);
        const uint32_t elapsed = now - last_ms;
        if (elapsed == 0u) {
            continue;
        }
        last_ms = now;
        net_tick(&net, elapsed);
        fota_tick(elapsed);

        if (leave_requested) {
            /* 200 ms for the answer to "run" to leave; after a fetch,
             * FOTA_DONE_LINGER_MS for a server that missed our last ACK. */
            leave_ms += elapsed;
            if (leave_ms >= leave_after_ms) {
                run_app();
            }
            continue;
        }
        if (!staying) {
            if (window_ms <= elapsed) {
                run_app();
            }
            window_ms -= elapsed;
            continue;
        }
        if (fota_busy()) {
            continue;
        }
        if (fota_state() == FOTA_DONE) {
            leave_requested = true;
            leave_after_ms = FOTA_DONE_LINGER_MS;
            continue;
        }
        if (fota_state() == FOTA_FAILED && !fota_slot_touched() && app_valid()) {
            run_app();   /* nothing was fetched, and what we had is still good */
        }
        if (retry_ms > elapsed) {
            retry_ms -= elapsed;
            continue;
        }
        retry_ms = RETRY_MS;
        (void)fota_start(APP_BASE, APP_SIZE, IMAGE_MAGIC_APP, CFG_APP_FILE);
    }
}

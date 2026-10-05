#include "control.h"
#include <string.h>
#include "config.h"
#include "fota.h"
#include "hal.h"
#include "image.h"
#include "link485.h"
#include "linkcheck.h"
#include "loadctl.h"
#include "uart.h"
#include "udp.h"
#include "veports.h"

/* The answer to a command has to ride out on a poll before the unit resets. */
#define RESET_DELAY_MS  200u

static net_t   *network;
static uint32_t reset_in_ms;       /* 0: no reset pending */
static bool     staging;           /* a bootloader fetch is under way */

static char   answer[256];
static size_t answer_length;

static void add(const char *text)
{
    const size_t length = strlen(text);
    if (answer_length + length <= sizeof answer) {
        memcpy(&answer[answer_length], text, length);
        answer_length += length;
    }
}

static void add_number(uint32_t value)
{
    char digits[11];
    size_t at = sizeof digits - 1u;
    digits[at] = '\0';
    do {
        digits[--at] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    add(&digits[at]);
}

/* A reset already on its way is not put off: "vedc.py rescue" repeats
 * "stay" faster than RESET_DELAY_MS. */
static void reset_in(uint32_t milliseconds)
{
    if (reset_in_ms == 0u) {
        reset_in_ms = milliseconds;
    }
}

static bool is_command(const uint8_t *payload, uint16_t length, const char *word)
{
    const size_t size = strlen(word);
    return length >= size && memcmp(payload, word, size) == 0
        && (length == size || payload[size] <= ' ');
}

static void status(void)
{
    const uint8_t *boot = (const uint8_t *)(uintptr_t)BOOT_BASE;
    add("app v");
    add_number(IMAGE_VERSION);
    add(" boot ");
    if (image_valid(boot, BOOT_SIZE, IMAGE_MAGIC_BOOT)) {
        add("v");
        add_number(image_version(boot));
    } else {
        add("invalid");
    }
    add(" up ");
    add_number(hal_millis() / 1000u);
    add("s out ");
    add_number(loads_outputs());
    add(" sense ");
    add_number(loads_sense_mv(0u));
    add(" ");
    add_number(loads_sense_mv(1u));
    add(loadctl_connected() ? " ctl open" : " ctl idle");
    add(" heard ");
    add_number(linkcheck_silent_ms() / 1000u);
    add("s");
    add(" 485lost ");
    add_number(uart_rx_lost(UART_485));
    for (uint8_t port = 0u; port < VE_PORT_COUNT; port++) {
        add(" ve");
        add_number(port + 1u);
        add(veports_connected(port) ? " open " : " idle ");
        add_number(veports_rx_bytes(port));
    }
    add(staging ? " staging-bootloader\n" : "\n");
}

static void set_outputs(const uint8_t *payload, uint16_t length)
{
    /* "out <digit>" */
    if (length < 5u || payload[4] < '0' || payload[4] > '3') {
        add("error out takes 0-3\n");
        return;
    }
    const uint8_t mask = (uint8_t)(payload[4] - '0');
    loads_set(mask);
    add("ok out ");
    add_number(mask);
    add("\n");
}

void control_init(net_t *net)
{
    network = net;
}

void control_input(net_t *net, uint32_t source_ip, uint16_t source_port,
                   const uint8_t *source_mac, const uint8_t *payload, uint16_t length)
{
    answer_length = 0u;
    if (is_command(payload, length, "status")) {
        status();
    } else if (is_command(payload, length, "out")) {
        set_outputs(payload, length);
    } else if (is_command(payload, length, "app") || is_command(payload, length, "stay")) {
        if (staging) {
            add("error busy staging the bootloader\n");
        } else {
            BOOT_REQUEST = BOOT_REQUEST_STAY;
            reset_in(RESET_DELAY_MS);
            add("ok resetting; the bootloader fetches " CFG_APP_FILE "\n");
        }
    } else if (is_command(payload, length, "bootloader")) {
        if (fota_start(STAGING_BASE, STAGING_SIZE, IMAGE_MAGIC_BOOT, CFG_BOOT_FILE)) {
            staging = true;
            add("ok fetching " CFG_BOOT_FILE "; see status\n");
        } else {
            add("error a fetch is already running\n");
        }
    } else {
        add("error unknown command (status, out <0-3>, app, bootloader)\n");
    }
    (void)udp_send(net, source_ip, source_mac, CFG_CONTROL_PORT, source_port,
                   (const uint8_t *)answer, (uint16_t)answer_length);
}

void control_tick(uint32_t elapsed_ms)
{
    if (staging && !fota_busy()) {
        staging = false;
        if (fota_state() == FOTA_DONE) {
            reset_in(FOTA_DONE_LINGER_MS);  /* stage0 installs it on the way up */
        }
    }
    if (reset_in_ms != 0u) {
        if (reset_in_ms <= elapsed_ms) {
            hal_reset();
        }
        reset_in_ms -= elapsed_ms;
    }
}

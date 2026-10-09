#include "master.h"
#include <string.h>
#include "config.h"
#include "hal.h"
#include "link485.h"

/* ── Fake hardware ──────────────────────────────────────────────────── */

static uint8_t wire_in[4096];
static size_t  wire_in_length, wire_in_at;
uint8_t        wire_out[4096];
size_t         wire_out_length;
uint32_t       now_ms;

uint32_t hal_millis(void) { return now_ms; }
int  resets;
void hal_reset(void) { resets++; }
void nvm_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    (void)address; (void)data; (void)length;
}

bool rs485_getc(uint8_t *byte)
{
    if (wire_in_at >= wire_in_length) {
        return false;
    }
    *byte = wire_in[wire_in_at++];
    return true;
}

void rs485_send(const uint8_t *data, size_t length)
{
    memcpy(&wire_out[wire_out_length], data, length);
    wire_out_length += length;
}

/* ── The unit ───────────────────────────────────────────────────────── */

const uint8_t unit_mac[6] = CFG_MAC_ADDR;
const uint8_t master_mac[6] = { 0x02, 0, 0, 0, 0, 0x01 };

net_t net;
static uint8_t rx_frame[CFG_NET_FRAME], tx_frame[CFG_NET_FRAME];

void (*unit_service)(void);

net_err_t master_unit_init(void)
{
    link485_init();
    const net_err_t result = net_init(&net, rx_frame, sizeof rx_frame, tx_frame, sizeof tx_frame,
                                      unit_mac, &link485_mac, NULL);
    net.ipv4_addr = CFG_IP_ADDR;
    net.subnet_mask = CFG_NETMASK;
    net.gateway_ipv4 = CFG_SERVER_IP;
    net.mtu = CFG_NET_FRAME - 14u;
    return result;
}

/* ── The master ─────────────────────────────────────────────────────── */

uint8_t          sequence;
struct tmp_frame answer;     /* the unit's reply to the last poll */
bool             answered;

void exchange_raw(const uint8_t *bytes, size_t length)
{
    static struct tmp_scanner scanner;
    memcpy(wire_in, bytes, length);
    wire_in_length = length;
    wire_in_at = 0u;
    wire_out_length = 0u;
    link485_service();
    (void)net_poll(&net);
    if (unit_service != NULL) {
        unit_service();
    }

    answered = false;
    tmp_scanner_reset(&scanner);
    for (size_t index = 0u; index < wire_out_length; index++) {
        if (tmp_scanner_push(&scanner, wire_out[index], &answer) == SCAN_FRAME_READY) {
            answered = true;
        }
    }
}

void poll_as(uint8_t address, uint8_t poll_sequence, uint8_t flags, const uint8_t *frame,
             size_t length)
{
    static uint8_t wire[TMP_FRAME_MAX];
    sequence = poll_sequence;
    const size_t wire_length = tmp_frame_build(wire, TMP_POLL, address, poll_sequence, flags,
                                               frame, length);
    exchange_raw(wire, wire_length);
}

void poll(uint8_t address, const uint8_t *frame, size_t length)
{
    poll_as(address, (uint8_t)(sequence + 1u), tmp_flags(false, 0u), frame, length);
}

bool arq_down_bit, arq_up_expected, arq_new;

void arq_poll(const uint8_t *frame, size_t length, bool lost)
{
    const uint8_t flags = TMP_FLAG_ARQ | (arq_down_bit ? TMP_FLAG_SEQ : 0u)
                        | (arq_up_expected ? TMP_FLAG_ACK : 0u);
    poll_as(TMP_ADDR_COLLECTOR, (uint8_t)(sequence + 1u), flags, frame, length);
    arq_new = false;
    if (lost || !answered) {
        return;
    }
    if (length != 0u && ((answer.flags & TMP_FLAG_ACK) != 0u) != arq_down_bit) {
        arq_down_bit = !arq_down_bit;
    }
    if (answer.length != 0u && ((answer.flags & TMP_FLAG_SEQ) != 0u) == arq_up_expected) {
        arq_up_expected = !arq_up_expected;
        arq_new = true;
    }
}

/* ── Frame builders ─────────────────────────────────────────────────── */

uint16_t checksum(const uint8_t *data, size_t length, uint32_t sum)
{
    for (size_t index = 0u; index + 1u < length; index += 2u) {
        sum += (uint32_t)(data[index] << 8) | data[index + 1u];
    }
    if (length & 1u) {
        sum += (uint32_t)(data[length - 1u] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void put16(uint8_t *at, uint16_t value) { at[0] = (uint8_t)(value >> 8); at[1] = (uint8_t)value; }
void put32(uint8_t *at, uint32_t value) { put16(at, (uint16_t)(value >> 16)); put16(at + 2, (uint16_t)value); }

size_t ethernet(uint8_t *frame, const uint8_t *destination, uint16_t type)
{
    memcpy(frame, destination, 6);
    memcpy(frame + 6, master_mac, 6);
    put16(frame + 12, type);
    return 14u;
}

size_t ipv4(uint8_t *frame, uint8_t protocol, size_t payload_length)
{
    uint8_t *ip = frame + ethernet(frame, unit_mac, 0x0800u);
    memset(ip, 0, 20);
    ip[0] = 0x45u;
    put16(ip + 2, (uint16_t)(20u + payload_length));
    ip[8] = 64u;
    ip[9] = protocol;
    put32(ip + 12, MASTER_IP);
    put32(ip + 16, CFG_IP_ADDR);
    put16(ip + 10, checksum(ip, 20u, 0u));
    return 34u;
}

uint32_t pseudo_header(uint8_t protocol, size_t length)
{
    return (MASTER_IP >> 16) + (MASTER_IP & 0xFFFFu) + (CFG_IP_ADDR >> 16) + (CFG_IP_ADDR & 0xFFFFu)
         + protocol + (uint32_t)length;
}

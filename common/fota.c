#include "fota.h"
#include <string.h>
#include "arp.h"
#include "config.h"
#include "hal.h"
#include "image.h"
#include "tftp.h"
#include "udp.h"

#define RESOLVE_RETRY_MS    500u
#define RESOLVE_LIMIT_MS    10000u

static net_t          *network;
static tftp_client_t   tftp;
static enum fota_state state;

static uint32_t slot_base;
static uint32_t slot_size;
static uint32_t slot_magic;
static const char *slot_file;

static uint8_t  block[TFTP_DEFAULT_BLKSIZE];
static uint16_t block_length;     /* 0: nothing waiting to be programmed */
static uint32_t block_offset;
static bool     rejected;         /* not an image for this slot           */
static bool     touched;

static uint32_t resolve_elapsed_ms;
static uint32_t resolve_retry_ms;

void fota_flush(void)
{
    if (block_length == 0u) {
        return;
    }
    nvm_write(slot_base + block_offset, block, block_length);
    block_length = 0u;
    touched = true;
}

static uint32_t word_at(const uint8_t *data, uint32_t offset)
{
    uint32_t value;
    memcpy(&value, data + offset, sizeof value);
    return value;
}

static bool header_fits_slot(const uint8_t *data, uint16_t length)
{
    if (length < 4u * (IMAGE_VECTOR_VERSION + 1u)) {
        return false;
    }
    return word_at(data, 4u * IMAGE_VECTOR_MAGIC) == slot_magic
        && word_at(data, 4u * IMAGE_VECTOR_LENGTH) <= slot_size - 4u;
}

static void on_data(uint16_t block_number, const uint8_t *data, uint16_t length, void *context)
{
    (void)context;
    if (rejected || length == 0u) {
        return;
    }
    /* Nothing is erased until the first block proves to be ours. */
    if (block_number == 1u && !header_fits_slot(data, length)) {
        rejected = true;
        return;
    }
    const uint32_t offset = (uint32_t)(block_number - 1u) * TFTP_DEFAULT_BLKSIZE;
    if (length > sizeof block || offset + length > slot_size) {
        rejected = true;
        return;
    }
    fota_flush();
    memcpy(block, data, length);
    block_length = length;
    block_offset = offset;
}

static void on_done(uint8_t ok, uint16_t error_code, const char *message, void *context)
{
    (void)error_code;
    (void)message;
    (void)context;
    fota_flush();
    const bool good = ok && !rejected
                   && image_valid((const uint8_t *)(uintptr_t)slot_base, slot_size, slot_magic);
    state = good ? FOTA_DONE : FOTA_FAILED;
}

void fota_init(net_t *net)
{
    network = net;
    state = FOTA_IDLE;
    block_length = 0u;
    tftp_client_init(&tftp, CFG_TFTP_PORT, on_data, on_done, NULL);
}

bool fota_busy(void)
{
    return state == FOTA_RESOLVING || state == FOTA_FETCHING;
}

enum fota_state fota_state(void)
{
    return state;
}

bool fota_slot_touched(void)
{
    return touched;
}

bool fota_start(uint32_t base, uint32_t size, uint32_t magic, const char *filename)
{
    if (fota_busy()) {
        return false;
    }
    slot_base = base;
    slot_size = size;
    slot_magic = magic;
    slot_file = filename;
    block_length = 0u;
    rejected = false;
    touched = false;
    resolve_elapsed_ms = 0u;
    resolve_retry_ms = 0u;
    state = FOTA_RESOLVING;
    return true;
}

void fota_udp_input(net_t *net, uint32_t source_ip, uint16_t source_port,
                    const uint8_t *source_mac, const uint8_t *payload, uint16_t length)
{
    if (state == FOTA_FETCHING) {
        tftp_client_input(net, &tftp, source_ip, source_mac, source_port, payload, length);
    } else if (state == FOTA_DONE && source_ip == CFG_SERVER_IP && length >= 4u
               && payload[0] == 0u && payload[1] == TFTP_OP_DATA) {
        /* The server missed our last ACK and sent the last block again. */
        const uint8_t ack[4] = { 0u, TFTP_OP_ACK, payload[2], payload[3] };
        (void)udp_send(net, source_ip, source_mac, CFG_TFTP_PORT, source_port, ack, sizeof ack);
    }
}

void fota_tick(uint32_t elapsed_ms)
{
    if (state == FOTA_FETCHING) {
        tftp_client_tick(network, &tftp, elapsed_ms);
        return;
    }
    if (state != FOTA_RESOLVING) {
        return;
    }
    if (network->gateway_mac_valid) {
        tftp_client_init(&tftp, CFG_TFTP_PORT, on_data, on_done, NULL);
        state = FOTA_FETCHING;
        (void)tftp_client_get(network, &tftp, CFG_SERVER_IP, network->gateway_mac, slot_file, 0u);
        return;
    }
    resolve_elapsed_ms += elapsed_ms;
    if (resolve_elapsed_ms >= RESOLVE_LIMIT_MS) {
        state = FOTA_FAILED;
        return;
    }
    if (resolve_retry_ms <= elapsed_ms) {
        resolve_retry_ms = RESOLVE_RETRY_MS;
        (void)arp_request(network, CFG_SERVER_IP);
    } else {
        resolve_retry_ms -= elapsed_ms;
    }
}

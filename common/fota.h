#ifndef FOTA_H
#define FOTA_H

#include <stdint.h>
#include <stdbool.h>
#include "net.h"

/* Fetch an image by TFTP from CFG_SERVER_IP straight into a flash slot. The
 * bootloader uses it for the application slot; the application uses it to
 * stage a new bootloader. */

enum fota_state {
    FOTA_IDLE,
    FOTA_RESOLVING,   /* ARP for the server      */
    FOTA_FETCHING,
    FOTA_DONE,        /* slot holds a valid image */
    FOTA_FAILED
};

void fota_init(net_t *net);

/* false while a transfer is running. */
bool fota_start(uint32_t slot_base, uint32_t slot_size, uint32_t magic, const char *filename);

/* The UDP handler for CFG_TFTP_PORT. */
void fota_udp_input(net_t *net, uint32_t source_ip, uint16_t source_port,
                    const uint8_t *source_mac, const uint8_t *payload, uint16_t length);

void fota_tick(uint32_t elapsed_ms);

/* Program the block in hand, if any. Stalls the CPU for some milliseconds;
 * hook it to link485_set_before_reply(). */
void fota_flush(void);

enum fota_state fota_state(void);
bool fota_busy(void);

/* After FOTA_DONE, wait this long before resetting: a server that missed our
 * last ACK sends the last block again, and is answered (without writing
 * anything) until the next fota_start(). */
#define FOTA_DONE_LINGER_MS 3000u

/* The slot has been written to since fota_start(): whatever it held is gone. */
bool fota_slot_touched(void);

#endif

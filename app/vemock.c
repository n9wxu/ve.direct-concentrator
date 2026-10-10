#include "vemock.h"
#include "tusb.h"
#include "uart.h"
#include "veports.h"

#define MOCK_CDC    1u      /* the second CDC interface (usb_descriptors.c) */
#define MOCK_SYNC   0xA5u

static enum { WAIT_SYNC, WAIT_PORT, WAIT_LENGTH, IN_DATA } state;
static uint8_t port;        /* 1-4 */
static uint8_t length;
static uint8_t filled;
static uint8_t data[255];
static uint32_t moved;

uint32_t vemock_bytes(void)
{
    return moved;
}

static void push(uint8_t byte)
{
    switch (state) {
    case WAIT_SYNC:
        if (byte == MOCK_SYNC) {
            state = WAIT_PORT;
        }
        break;
    case WAIT_PORT:
        port = byte;
        state = (port >= 1u && port <= VE_PORT_COUNT) ? WAIT_LENGTH : WAIT_SYNC;
        break;
    case WAIT_LENGTH:
        length = byte;
        filled = 0u;
        state = (length != 0u) ? IN_DATA : WAIT_SYNC;
        break;
    case IN_DATA:
    default:
        data[filled++] = byte;
        if (filled == length) {
            (void)uart_inject((enum uart_id)(UART_VE1 + port - 1u), data, length);
            state = WAIT_SYNC;
        }
        break;
    }
}

void vemock_service(void)
{
    uint8_t chunk[64];
    uint32_t count;
    while ((count = tud_cdc_n_read(MOCK_CDC, chunk, sizeof chunk)) != 0u) {
        moved += count;
        for (uint32_t index = 0u; index < count; index++) {
            push(chunk[index]);
        }
    }
}

void vemock_tap(uint8_t index, const uint8_t *bytes, size_t count)
{
    if (!tud_cdc_n_connected(MOCK_CDC)) {
        return;
    }
    while (count != 0u) {
        const uint8_t piece = (uint8_t)(count > 255u ? 255u : count);
        if (tud_cdc_n_write_available(MOCK_CDC) < piece + 3u) {
            break;      /* the host is not reading: drop, as a line would */
        }
        const uint8_t header[3] = { MOCK_SYNC, (uint8_t)(index + 1u), piece };
        (void)tud_cdc_n_write(MOCK_CDC, header, sizeof header);
        (void)tud_cdc_n_write(MOCK_CDC, bytes, piece);
        moved += piece;
        bytes += piece;
        count -= piece;
    }
    (void)tud_cdc_n_write_flush(MOCK_CDC);
}

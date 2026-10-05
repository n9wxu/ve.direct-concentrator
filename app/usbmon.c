#include "usbmon.h"
#include "board.h"
#include "hal.h"
#include "tusb.h"

#define BYTES_PER_LINE 32u

static bool     line_transmitted;
static uint8_t  line_bytes;       /* on the current line; 0: no line open */
static uint32_t lost;
static bool     was_open;
static uint32_t bytes;

void USB_Handler(void)
{
    tud_int_handler(0);
}

uint32_t tusb_time_millis_api(void)
{
    return hal_millis();
}

void usbmon_init(void)
{
    PM->APBBMASK.reg |= PM_APBBMASK_USB;
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_USB | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) {
    }
    pin_peripheral(PIN(PORT_A, 24), 6u);   /* function G: USB D- */
    pin_peripheral(PIN(PORT_A, 25), 6u);   /*             USB D+ */
    tusb_init();
}

static void put(const char *text, uint32_t length)
{
    bytes += tud_cdc_write(text, length);
}

uint32_t usbmon_bytes(void)
{
    return bytes;
}

static void put_decimal(uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[sizeof digits - ++count] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    put(&digits[sizeof digits - count], count);
}

void usbmon_service(void)
{
    tud_task();
    const bool open = tud_cdc_connected();
    if (open && !was_open) {
        put("\r\n# VE.Direct concentrator: 485 monitor (R = received, T = transmitted)\r\n", 73u);
        line_bytes = 0u;
        lost = 0u;
    }
    was_open = open;
    if (open) {
        if (lost != 0u && tud_cdc_write_available() >= 32u) {
            put("\r\n! lost ", 9u);
            put_decimal(lost);
            lost = 0u;
            line_bytes = 0u;
        }
        (void)tud_cdc_write_flush();
    }
}

void usbmon_tap(bool transmitted, const uint8_t *data, size_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!was_open) {
        return;
    }
    for (size_t index = 0u; index < length; index++) {
        /* Worst case for one byte: a new line header and three characters. */
        if (tud_cdc_write_available() < 8u) {
            lost += (uint32_t)(length - index);
            return;
        }
        if (line_bytes == 0u || line_bytes == BYTES_PER_LINE || transmitted != line_transmitted) {
            put(transmitted ? "\r\nT" : "\r\nR", 3u);
            line_transmitted = transmitted;
            line_bytes = 0u;
        }
        const char text[3] = { ' ', hex[data[index] >> 4], hex[data[index] & 0xFu] };
        put(text, 3u);
        line_bytes++;
    }
}

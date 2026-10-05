#include <string.h>
#include "tusb.h"

/* Two CDC-ACM serial ports: the 485 monitor (usbmon.c) and the VE.Direct mock
 * (vemock.c). 1209:0001 is the pid.codes test ID — replace it before the
 * device leaves the bench. */

static const tusb_desc_device_t device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1209,
    .idProduct          = 0x0001,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device_descriptor;
}

#define CONFIG_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LENGTH, 0x00, 100),
    /* interface 0+1, string 3, notification EP 0x81, data EPs 0x02 / 0x82 */
    TUD_CDC_DESCRIPTOR(0, 3, 0x81, 8, 0x02, 0x82, 64),
    /* interface 2+3, string 4, notification EP 0x83, data EPs 0x04 / 0x84 */
    TUD_CDC_DESCRIPTOR(2, 4, 0x83, 8, 0x04, 0x84, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return configuration_descriptor;
}

static const char *const strings[] = {
    NULL,                       /* 0: language */
    "n9wxu",
    "VE.Direct concentrator",
    "485 monitor",
    "VE.Direct mock",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language)
{
    static uint16_t descriptor[32];
    uint8_t count;
    (void)language;

    if (index == 0u) {
        descriptor[1] = 0x0409;   /* English (US) */
        count = 1u;
    } else {
        if (index >= sizeof strings / sizeof strings[0]) {
            return NULL;
        }
        const char *text = strings[index];
        count = (uint8_t)strlen(text);
        if (count > 31u) {
            count = 31u;
        }
        for (uint8_t at = 0u; at < count; at++) {
            descriptor[1u + at] = (uint16_t)text[at];
        }
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * count + 2u));
    return descriptor;
}

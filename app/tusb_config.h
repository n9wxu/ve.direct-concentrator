#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU                OPT_MCU_SAMD21
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED             1

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 2   /* 0: 485 monitor, 1: VE.Direct mock */
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* The monitor writes three characters per 485 byte; a full poll frame is
 * buffered here between two USB transfers. The mock receives up to three
 * devices' worth of 19200-baud text between two main-loop turns. */
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      2048
#define CFG_TUD_CDC_EP_BUFSIZE      64

#endif

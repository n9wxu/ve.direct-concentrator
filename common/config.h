#ifndef CONFIG_H
#define CONFIG_H

/* Network identity and ports, shared by the bootloader and the application.
 * The unit is the single TMP-v2 slave on the 485 pair; recon (tmptun, tap0)
 * is the master and the TFTP server. */

#define CFG_IP_ADDR         NET_IPV4(10, 55, 0, 3)
#define CFG_NETMASK         NET_IPV4(255, 255, 255, 0)
#define CFG_SERVER_IP       NET_IPV4(10, 55, 0, 1)
#define CFG_MAC_ADDR        { 0x02, 0x56, 0x45, 0x44, 0x43, 0x01 }

#define CFG_CONTROL_PORT    5000u   /* UDP, ASCII commands */
#define CFG_VE_PORT_BASE    5001u   /* TCP, VE.Direct port n is BASE + n-1 */
#define CFG_LOAD_PORT       5005u   /* TCP, load switch control stream */
#define CFG_TFTP_PORT       6900u   /* our UDP port for TFTP transfers */

#define CFG_APP_FILE        "vedc-app.bin"
#define CFG_BOOT_FILE       "vedc-boot.bin"

#define CFG_VE_BAUD         19200u

/* Quiet time after a poll before we take the 485 bus, so the master's
 * transceiver has released it. The RP2040 ran recon's RSM485PHT with about
 * 80 us; the bench FT232R adapter needs 150-300 us. 1 ms covers both with
 * margin, at about 1 ms per poll — the unit must stay reachable without a
 * debugger, so margin wins. */
#ifndef CFG_485_TURNAROUND_US
#define CFG_485_TURNAROUND_US 1000u
#endif

/* No answer from recon for this long (linkcheck.h): reset, through the
 * bootloader's window. Long enough to ride out a recon reboot; a reset
 * leaves the loads off. */
#ifndef CFG_LINK_LOSS_RESET_S
#define CFG_LINK_LOSS_RESET_S 600u
#endif

/* 1: the application is replaced by the LED lamp test (app/ledtest.h). */
#ifndef CFG_LED_TEST
#define CFG_LED_TEST 0
#endif

/* Frame buffers. The unit never sends more than this, and advertises a TCP MSS
 * that fits it; longer received frames are cut by the stack. One TFTP block
 * (512 B) plus headers fits. */
#define CFG_NET_FRAME       600u

/* How long the bootloader waits for a "stay" command before starting a valid
 * application. A crashing application is reset by its watchdog, so this window
 * comes round again and the unit stays recoverable. */
#define CFG_BOOT_WINDOW_MS  2000u

#endif

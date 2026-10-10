#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include <stdbool.h>

/* Flash layout (SAMD21G18A, 256 KB):
 *
 *   0x00000  stage0      2 KB   never updated in the field: checks, copies, jumps
 *   0x00800  bootloader 16 KB   TMP-v2 + IP + TFTP; fetches the application
 *   0x04800  application 222 KB
 *   0x3C000  staging    16 KB   a new bootloader waits here for stage0 to install
 *
 * An image carries its descriptor in the reserved Cortex-M0 vector slots 7-9
 * and a CRC-32 (Ethernet/zlib) after its last byte; tools/mkimage.py fills in
 * the length and appends the CRC. */

#define STAGE0_BASE         0x00000000u
#define STAGE0_SIZE         0x00000800u
#define BOOT_BASE           0x00000800u
#define BOOT_SIZE           0x00004000u
#define APP_BASE            0x00004800u
#define APP_SIZE            0x00037800u
#define STAGING_BASE        0x0003C000u
#define STAGING_SIZE        0x00004000u

#define IMAGE_MAGIC_BOOT    0x42444556u   /* "VEDB" */
#define IMAGE_MAGIC_APP     0x41444556u   /* "VEDA" */

#define IMAGE_VECTOR_MAGIC    7u
#define IMAGE_VECTOR_LENGTH   8u
#define IMAGE_VECTOR_VERSION  9u

/* Request left in RAM across a reset (not cleared by startup code). */
#define BOOT_REQUEST        (*(volatile uint32_t *)0x20007FFCu)
#define BOOT_REQUEST_STAY   0x53544159u   /* stay in the bootloader and fetch */
#define BOOT_REQUEST_RUN    0x52554E21u   /* start the application at once    */

uint32_t image_crc32(const uint8_t *data, uint32_t length);

/* A well-formed image of the given kind, at most slot_size bytes with its CRC. */
bool image_valid(const uint8_t *image, uint32_t slot_size, uint32_t magic);

/* Valid images only. */
uint32_t image_length(const uint8_t *image);
uint32_t image_version(const uint8_t *image);
uint32_t image_stored_crc(const uint8_t *image);

#endif

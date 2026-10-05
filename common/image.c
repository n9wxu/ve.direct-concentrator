#include "image.h"
#include <string.h>

static uint32_t word_at(const uint8_t *image, uint32_t offset)
{
    uint32_t value;
    memcpy(&value, image + offset, sizeof value);   /* images are little-endian, as are we */
    return value;
}

uint32_t image_crc32(const uint8_t *data, uint32_t length)
{
    static const uint32_t nibble[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
        0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
    };
    uint32_t crc = 0xFFFFFFFFu;
    while (length-- != 0u) {
        crc ^= *data++;
        crc = (crc >> 4) ^ nibble[crc & 0xFu];
        crc = (crc >> 4) ^ nibble[crc & 0xFu];
    }
    return ~crc;
}

uint32_t image_length(const uint8_t *image)
{
    return word_at(image, 4u * IMAGE_VECTOR_LENGTH);
}

uint32_t image_version(const uint8_t *image)
{
    return word_at(image, 4u * IMAGE_VECTOR_VERSION);
}

uint32_t image_stored_crc(const uint8_t *image)
{
    return word_at(image, image_length(image));
}

bool image_valid(const uint8_t *image, uint32_t slot_size, uint32_t magic)
{
    if (word_at(image, 4u * IMAGE_VECTOR_MAGIC) != magic) {
        return false;
    }
    const uint32_t length = image_length(image);
    if (length < 0x40u || (length & 3u) != 0u || length > slot_size - 4u) {
        return false;
    }
    return image_crc32(image, length) == word_at(image, length);
}

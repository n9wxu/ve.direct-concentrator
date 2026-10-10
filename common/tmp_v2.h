#ifndef TMP_V2_H
#define TMP_V2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* TMP-v2 wire format, compiled verbatim on both link ends. Spec: PROTOCOL-485-v2.md.
 * Frame: AA 55 | VER<<4|TYPE | ADDR | SEQ | FLAGS | LEN(be16) | payload | CRC32(be32). */

/* 500 kbaud: the RSM485PHT ceiling, and an exact divisor of both the SAMD21's
 * 48 MHz and the CM4 PL011's 48 MHz UART clock. Both ends compile this header;
 * keep the copies (trailer-monitor/protocol, ve.direct-concentrator/common)
 * identical. */
#define TMP_V2_BAUD             500000u
#define TMP_V2_VERSION          2u
#define TMP_ADDR_COLLECTOR      0x01u
#define TMP_ADDR_BROADCAST      0x00u

enum tmp_type {
    TMP_POLL   = 0x1,
    TMP_REPORT = 0x2,
    TMP_CMD    = 0x3,
    TMP_ACK    = 0x4,
    TMP_NAK    = 0x5,
    TMP_LOG    = 0x6,
    TMP_NET    = 0x7
};

enum tmp_flag {
    TMP_FLAG_COMPRESSED = 0x01u,
    /* On a POLL: a MAC retry. The master got no intact answer to the poll
     * with this SEQ, and sends it again; a slave that answered it sends the
     * same answer again and does not take the poll's frame a second time. */
    TMP_FLAG_RETRY      = 0x02u
};
#define TMP_DICT_ID_SHIFT       4u

enum tmp_cmd {
    TMP_CMD_SET_OUTPUTS = 0x01
};

#define TMP_SYNC_0              0xAAu
#define TMP_SYNC_1              0x55u
#define TMP_HEADER_BYTES        8u
#define TMP_CRC_BYTES           4u
#define TMP_FRAME_OVERHEAD      (TMP_HEADER_BYTES + TMP_CRC_BYTES)
#define TMP_ETHERNET_FRAME_MAX  1522u
#define TMP_PAYLOAD_MAX         TMP_ETHERNET_FRAME_MAX
#define TMP_FRAME_MAX           (TMP_FRAME_OVERHEAD + TMP_PAYLOAD_MAX)

static inline uint8_t tmp_version_type(enum tmp_type type)
{
    return (uint8_t)(((TMP_V2_VERSION & 0xFu) << 4) | ((uint8_t)type & 0xFu));
}
static inline enum tmp_type tmp_type_of(uint8_t version_type)
{
    return (enum tmp_type)(version_type & 0x0Fu);
}
static inline uint8_t tmp_version_of(uint8_t version_type)
{
    return (uint8_t)((version_type >> 4) & 0x0Fu);
}
static inline uint8_t tmp_flags(bool compressed, uint8_t dictionary_id)
{
    return (uint8_t)((compressed ? TMP_FLAG_COMPRESSED : 0u)
                     | (uint8_t)((dictionary_id & 0xFu) << TMP_DICT_ID_SHIFT));
}
static inline bool tmp_is_compressed(uint8_t flags)
{
    return (flags & TMP_FLAG_COMPRESSED) != 0u;
}
static inline uint8_t tmp_dictionary_id(uint8_t flags)
{
    return (uint8_t)((flags >> TMP_DICT_ID_SHIFT) & 0xFu);
}

static inline void put_be16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}
static inline void put_be32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}
static inline uint16_t get_be16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}
static inline uint32_t get_be32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16)
         | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

/* CRC-32 (reflected, Ethernet/zlib). Bitwise: no table to duplicate across units,
 * and the 500 kbaud link cannot outrun it. On the RP2040 the DMA CRC sniffer
 * can replace this at the same result. */
static inline uint32_t crc32_ethernet(const uint8_t *data, size_t length)
{
    uint32_t remainder = 0xFFFFFFFFu;
    for (size_t index = 0u; index < length; index++) {
        remainder ^= data[index];
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = (uint32_t)0u - (remainder & 1u);
            remainder = (remainder >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~remainder;
}

#define TMP_CRC_REGION_START    2u   /* CRC covers VER..payload, not SYNC or CRC */

/* Compose a frame into out[TMP_FRAME_MAX]; returns total byte count, 0 on overrun. */
static inline size_t tmp_frame_build(uint8_t *out, enum tmp_type type, uint8_t address,
                                     uint8_t sequence, uint8_t flags,
                                     const uint8_t *payload, size_t payload_length)
{
    if (payload_length > TMP_PAYLOAD_MAX) {
        return 0u;
    }
    out[0] = TMP_SYNC_0;
    out[1] = TMP_SYNC_1;
    out[2] = tmp_version_type(type);
    out[3] = address;
    out[4] = sequence;
    out[5] = flags;
    put_be16(&out[6], (uint16_t)payload_length);
    for (size_t index = 0u; index < payload_length; index++) {
        out[TMP_HEADER_BYTES + index] = payload[index];
    }
    const size_t crc_offset = TMP_HEADER_BYTES + payload_length;
    const uint32_t crc = crc32_ethernet(&out[TMP_CRC_REGION_START], crc_offset - TMP_CRC_REGION_START);
    put_be32(&out[crc_offset], crc);
    return crc_offset + TMP_CRC_BYTES;
}

struct tmp_frame {
    enum tmp_type type;
    uint8_t       address;
    uint8_t       sequence;
    uint8_t       flags;
    uint16_t      length;
    uint8_t       payload[TMP_PAYLOAD_MAX];
};

enum tmp_scan_state {
    SCAN_SYNC_0, SCAN_SYNC_1, SCAN_VERSION_TYPE, SCAN_ADDRESS, SCAN_SEQUENCE,
    SCAN_FLAGS, SCAN_LENGTH_HIGH, SCAN_LENGTH_LOW, SCAN_PAYLOAD, SCAN_CRC
};

struct tmp_scanner {
    enum tmp_scan_state state;
    uint8_t             version_type;
    uint8_t             address;
    uint8_t             sequence;
    uint8_t             flags;
    uint16_t            length;
    uint16_t            payload_index;
    uint8_t             crc_index;
    uint8_t             crc_bytes[TMP_CRC_BYTES];
    uint8_t             payload[TMP_PAYLOAD_MAX];
};

static inline void tmp_scanner_reset(struct tmp_scanner *scanner)
{
    scanner->state = SCAN_SYNC_0;
}

enum tmp_scan_result { SCAN_NEED_MORE = 0, SCAN_FRAME_READY = 1, SCAN_CRC_FAILED = -1 };

/* Feed one received byte. Resyncs on garbage; verifies CRC before reporting a frame. */
static inline enum tmp_scan_result tmp_scanner_push(struct tmp_scanner *scanner, uint8_t byte,
                                                    struct tmp_frame *out)
{
    switch (scanner->state) {
    case SCAN_SYNC_0:
        scanner->state = (byte == TMP_SYNC_0) ? SCAN_SYNC_1 : SCAN_SYNC_0;
        return SCAN_NEED_MORE;
    case SCAN_SYNC_1:
        scanner->state = (byte == TMP_SYNC_1) ? SCAN_VERSION_TYPE
                       : (byte == TMP_SYNC_0) ? SCAN_SYNC_1 : SCAN_SYNC_0;
        return SCAN_NEED_MORE;
    case SCAN_VERSION_TYPE:
        scanner->version_type = byte;
        scanner->state = SCAN_ADDRESS;
        return SCAN_NEED_MORE;
    case SCAN_ADDRESS:
        scanner->address = byte;
        scanner->state = SCAN_SEQUENCE;
        return SCAN_NEED_MORE;
    case SCAN_SEQUENCE:
        scanner->sequence = byte;
        scanner->state = SCAN_FLAGS;
        return SCAN_NEED_MORE;
    case SCAN_FLAGS:
        scanner->flags = byte;
        scanner->state = SCAN_LENGTH_HIGH;
        return SCAN_NEED_MORE;
    case SCAN_LENGTH_HIGH:
        scanner->length = (uint16_t)((uint16_t)byte << 8);
        scanner->state = SCAN_LENGTH_LOW;
        return SCAN_NEED_MORE;
    case SCAN_LENGTH_LOW:
        scanner->length |= (uint16_t)byte;
        if (scanner->length > TMP_PAYLOAD_MAX) {
            scanner->state = SCAN_SYNC_0;
            return SCAN_NEED_MORE;
        }
        scanner->payload_index = 0u;
        scanner->crc_index = 0u;
        scanner->state = (scanner->length == 0u) ? SCAN_CRC : SCAN_PAYLOAD;
        return SCAN_NEED_MORE;
    case SCAN_PAYLOAD:
        scanner->payload[scanner->payload_index] = byte;
        scanner->payload_index++;
        if (scanner->payload_index >= scanner->length) {
            scanner->state = SCAN_CRC;
        }
        return SCAN_NEED_MORE;
    case SCAN_CRC:
    default:
        scanner->crc_bytes[scanner->crc_index] = byte;
        scanner->crc_index++;
        if (scanner->crc_index < TMP_CRC_BYTES) {
            return SCAN_NEED_MORE;
        }
        scanner->state = SCAN_SYNC_0;
        {
            uint8_t header[6];
            header[0] = scanner->version_type; header[1] = scanner->address;
            header[2] = scanner->sequence;     header[3] = scanner->flags;
            header[4] = (uint8_t)(scanner->length >> 8); header[5] = (uint8_t)scanner->length;
            uint32_t crc = 0xFFFFFFFFu;
            for (uint8_t i = 0u; i < 6u; i++) {
                crc ^= header[i];
                for (uint8_t b = 0u; b < 8u; b++) {
                    const uint32_t mask = (uint32_t)0u - (crc & 1u);
                    crc = (crc >> 1) ^ (0xEDB88320u & mask);
                }
            }
            for (uint16_t i = 0u; i < scanner->length; i++) {
                crc ^= scanner->payload[i];
                for (uint8_t b = 0u; b < 8u; b++) {
                    const uint32_t mask = (uint32_t)0u - (crc & 1u);
                    crc = (crc >> 1) ^ (0xEDB88320u & mask);
                }
            }
            if ((~crc) != get_be32(scanner->crc_bytes)) {
                return SCAN_CRC_FAILED;
            }
        }
        out->type = tmp_type_of(scanner->version_type);
        out->address = scanner->address;
        out->sequence = scanner->sequence;
        out->flags = scanner->flags;
        out->length = scanner->length;
        for (uint16_t i = 0u; i < scanner->length; i++) {
            out->payload[i] = scanner->payload[i];
        }
        return SCAN_FRAME_READY;
    }
}

#endif /* TMP_V2_H */

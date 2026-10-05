#include <stdio.h>
#include <stdlib.h>
#include "image.h"

#define CHECK(condition) \
    do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)

int main(int argc, char *argv[])
{
    static uint8_t image[0x1000];
    CHECK(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    CHECK(file != NULL);
    const size_t size = fread(image, 1, sizeof image, file);
    fclose(file);

    CHECK(size == 1284u + 4u);                       /* 1283 bytes padded, plus the CRC */
    CHECK(image_valid(image, 0x1000u, IMAGE_MAGIC_APP));
    CHECK(image_length(image) == 1284u);
    CHECK(!image_valid(image, 0x1000u, IMAGE_MAGIC_BOOT));   /* wrong kind       */
    CHECK(!image_valid(image, 1284u, IMAGE_MAGIC_APP));      /* slot too small   */
    CHECK(image_valid(image, 1288u, IMAGE_MAGIC_APP));       /* exactly fits     */

    image[700] ^= 0x01u;
    CHECK(!image_valid(image, 0x1000u, IMAGE_MAGIC_APP));    /* corrupted        */

    CHECK(image_crc32((const uint8_t *)"123456789", 9u) == 0xCBF43926u);
    printf("image: ok\n");
    return 0;
}

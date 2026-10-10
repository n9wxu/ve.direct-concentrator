/* Keep in step with common/image.h. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00004800, LENGTH = 0x00037800
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00007FF0
}
INCLUDE sections.ld

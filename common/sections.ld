/* Shared by stage0, the bootloader and the application; each names its FLASH
 * region and includes this. The top 16 bytes of RAM are left alone: the boot
 * request word lives there across resets (image.h). */

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }

    _sidata = LOADADDR(.data);
    .data :
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(_ebss + 0x800 <= _estack, "less than 2 KB left for the stack")
    /* Room for the CRC that mkimage.py appends. */
    ASSERT(_sidata + (_edata - _sdata) + 4 <= ORIGIN(FLASH) + LENGTH(FLASH), "image does not fit its slot")
}

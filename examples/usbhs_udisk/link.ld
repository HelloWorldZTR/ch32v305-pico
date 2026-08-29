ENTRY(_start)

__stack_size = 2048;
PROVIDE(_stack_size = __stack_size);

MEMORY
{
    /* CH32V305RBT6: 128 KiB Flash and 32 KiB SRAM. */
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 120K
    DISK  (rx)  : ORIGIN = 0x0001E000, LENGTH = 8K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
}

PROVIDE(_flash_disk_start = ORIGIN(DISK));
PROVIDE(_flash_disk_end = ORIGIN(DISK) + LENGTH(DISK));

SECTIONS
{
    .init :
    {
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
    } > FLASH

    .vector :
    {
        *(.vector)
        . = ALIGN(64);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } > FLASH

    .fini :
    {
        KEEP(*(SORT_NONE(.fini)))
        . = ALIGN(4);
    } > FLASH

    .diskimage ORIGIN(DISK) :
    {
        KEEP(*(.diskimage))
    } > DISK

    PROVIDE(_etext = .);
    PROVIDE(_eitcm = .);

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    .ctors :
    {
        KEEP(*crtbegin.o(.ctors))
        KEEP(*crtbegin?.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
    } > FLASH

    .dtors :
    {
        KEEP(*crtbegin.o(.dtors))
        KEEP(*crtbegin?.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
    } > FLASH

    .dalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
    } > RAM AT > FLASH

    .dlalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_lma = .);
    } > FLASH

    .data :
    {
        *(.gnu.linkonce.r.*)
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
        *(.sdata2.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        *(.srodata.cst16 .srodata.cst8 .srodata.cst4 .srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE(_edata = .);
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE(_sbss = .);
        *(.sbss*)
        *(.gnu.linkonce.sb.*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON*)
        . = ALIGN(4);
        PROVIDE(_ebss = .);
    } > RAM

    PROVIDE(_end = _ebss);
    PROVIDE(end = .);

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size (NOLOAD) :
    {
        PROVIDE(_heap_end = .);
        . = ALIGN(4);
        PROVIDE(_susrstack = .);
        . += __stack_size;
        PROVIDE(_eusrstack = .);
    } > RAM

    ASSERT(_data_lma + SIZEOF(.data) <= ORIGIN(DISK),
           "firmware overlaps the reserved 8 KiB flash disk")
    ASSERT(SIZEOF(.diskimage) == LENGTH(DISK),
           "the built-in FAT12 disk image must fill the reserved region")
    ASSERT(_ebss <= ORIGIN(RAM) + LENGTH(RAM) - __stack_size,
           "RAM sections overlap the stack")
}

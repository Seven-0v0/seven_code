/* GD32F4xx App 链接脚本 */
/* Flash: 512KB, RAM: 128KB (根据实际芯片修改) */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08008000, LENGTH = 480K  /* App: 从 32KB 开始，使用剩余空间 */
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .data :
    {
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .heap :
    {
        . = ALIGN(4);
        _heap_start = .;
        . = . + 0x400;
        _heap_end = .;
    } > RAM

    .stack :
    {
        . = ALIGN(4);
        . = . + 0x1000;
        _estack = .;
    } > RAM
}

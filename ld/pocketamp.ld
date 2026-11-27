/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* Pocket AMP standalone app. app.bin starts at flash offset 0x4000, mapped at
 * XIP 0x02000120; the SPL jumps there with r0 = its boot-parameter block.
 * The RAM layout follows RhinoTiger's own base/device/app.ld so the literal
 * flash driver, crt0 and fault vectors need no changes. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFC0
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0))
        *(.text*) *(.rodata*) *(.*_const)
        . = ALIGN(4);
        _etext = .;
    } > XIP

    .ram_text : {
        _rt_start = .;
        KEEP(*(.ram_text*))
        *(.app_ram*)
        . = ALIGN(4);
        _rt_end = .;
    } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);

    _app_xip_end = ORIGIN(XIP) + LENGTH(XIP);

    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*) *(.sdata*) *(.*_data)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > XIP
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON) *(.*_bss)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    .pool (NOLOAD) : {
        _pool_start = .;
        *(.pool*)
        . = ALIGN(4);
        _pool_end = .;
    } > POOL

    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT

    _guard0 = 0x01C74000;
    _ustack_lo = 0x01C74100;
    _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000;
    _sstack_lo = 0x01C7A100;
    _sstack_top = 0x01C7C000;

    /DISCARD/ : { *(.comment) *(.note*) }
}

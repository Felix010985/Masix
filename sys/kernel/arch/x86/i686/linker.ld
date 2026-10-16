ENTRY(entry_start)

SECTIONS
{
    /* GRUB требует >=1МБ */
    . = 1M;

    /* Код загрузчика: временный bootstrap, исполняется по физическим адресам,
     * поэтому его vaddr остаётся низким (== физическому адресу).
     * Стоит первым в образе: протокол LINUX (Limine) входит по адресу
     * загрузки ядра, то есть по первому байту защищённой части образа. */
    .text.entry : ALIGN(16) { KEEP(*(.text.entry)) }
    .text.boot : ALIGN(16) { KEEP(*(.text.boot)) }

    /* Заголовки протоколов: GRUB их ищет в начале образа, Limine тоже.
     * Тега фреймбуфера в заголовке multiboot2 нет намеренно: ядро само
     * запрашивает видеорежим у VBE или GOP. */
    .multiboot2 : ALIGN(8) { KEEP(*(.multiboot2)) }
    .limine_requests : ALIGN(8) {
        KEEP(*(.limine_requests_start_marker))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end_marker))
    }

    .bss.boot : ALIGN(4096) { KEEP(*(.bss.boot)) }

    /* Выравниваем физическое начало .text по 4 МБ, чтобы 4МБ-страницные PDE
     * могли точно отобразить физическое ядро на виртуальное */
    . = ALIGN(0x400000);
    _phys_text = .;

    /* PAGE_OFFSET как в Linux i386: ядро линкуется на 0xC0000000 + paddr */
    . = 0xC0000000 + _phys_text;

    .text : AT(_phys_text) {
        *(.text)
    }

    . = ALIGN(4);
    _phys_data = . - 0xC0000000;

    .data : AT(_phys_data) {
        *(.data)
        *(.rodata)
    }

    . = ALIGN(4096);
    _phys_bss = . - 0xC0000000;

    /* Границы BSS: бутстрап обнуляет эту область сам, загрузчик этого не делает */
    _bss_start = .;
    .bss : AT(_phys_bss) {
        *(COMMON)
        *(.bss)
    }
    _bss_end = .;

    _kernel_end = .;

    /* Служебные символы отладки */
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

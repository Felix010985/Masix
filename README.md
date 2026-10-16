<p align="center">
  <img src="https://github.com/Felix010985/felix010985.github.io/blob/main/tools/masix.png" alt="logo" width="240">
</p>

<h1 align="center">Masix</h1>

<p align="center">
  Независимое POSIX-совместимое ядро для x86 (i686) **И В БУДУЩЕМ НЕ ТОЛЬКО**, написанное с нуля.
</p>

---

> [!WARNING]
> **Проект в активной разработке.**
> Всё в этом репозитории - исходники, внутренние API, ABI системных вызовов,
> список реализованных вызовов, структура каталогов и правила сборки -
> **может измениться в любой момент без предупреждения**. Ничто здесь пока
> не считается стабильным: ни бинарный интерфейс ядра, ни набор драйверов,
> ни файловые системы.

## О репозитории

Это репозиторий mainline ядра **Masix**, код здесь исключительно проверенный человеком, код здесь обновляется после успешных 
проверок testing ядра.

## Что уже реализовано

### 🧠 Ядро
- **Монолитное ядро** под 32-битную цель `i686-unknown-elf`, полностью
  свободностоящее (`-ffreestanding -nostdlib`), собирается `clang` + `ld.lld`.
- **POSIX/Linux-совместимый ABI системных вызовов**: `int $0x80`, аргументы в
  `ebx/ecx/edx/esi/edi`, результат в `eax`, отрицательное значение - код
  ошибки. Номера совпадают с Linux i386.
- **Драйверы изолированы (уже, раньше нет)**: аппаратный код лежит в `sys/kernel/drivers/`,
  публичные интерфейсы - в `sys/kernel/drivers/include/drivers/`.

### 🧩 Управление памятью
- **Физический менеджер (PMM)**: битовая карта страниц, `pmm_alloc_zeroed()`
  / `pmm_free_page()`; память выделяется строго выше 16 МБ, чтобы не затирать
  образ ядра.
- **Виртуальный менеджер (VMM)**: 32-битные таблицы страниц; ядро живёт выше
  `0xC0000000`, у каждого процесса свой каталог страниц, есть отдельное окно
  MMIO (`0xE8000000`, до 16 МБ) для видеопамяти.

### ⏱️ Процессы и планировщик
- Вытесняющая многозадачность по таймеру, ring 3 для
  пользователя, отдельный стек ядра и TSS на задачу.
- `fork`, `execve` (включая `binfmt_script` - поддержку `#!` (шебанги)), `exit`/`exit_group`,
  `wait4`, смена рабочего каталога (`chdir`/`getcwd`).
- Переключение по PIT на 100 Гц.

### 🗄️ VFS и файловые системы
- Слой **VFS** с таблицей дескрипторов (`MAX_FD = 32`) и буферами для
  обычных файлов.
- Регистрация символьных устройств драйверами через `vfs_register_device()`;
  имя `console` зарезервировано под stdio (fd 0/1/2).
- **procfs** (`/proc`): `meminfo`, `version`.
- **devtmpfs** (`/dev`): `null`, `zero`, `full`, `random`, `tty0`, `com1`.
- **ramfs** - встроенная файловая система с блочным размещением.
- **initramfs** в формате CPIO (`newc`) - загрузка пользовательского окружения
  из модуля загрузчика.

### 🔌 IPC и сеть
- **pipe/pipe2** через VFS.
- **Unix-сокеты** `AF_UNIX` + `SOCK_STREAM` (`bind`, `listen`, `connect`,
  `accept`, `send`, `recv`) через Linux i386 `socketcall`.
- **Сеть `AF_INET`**: UDP (`SOCK_DGRAM`) для петлевого интерфейса
  `127.0.0.1`, плюс драйвер **RTL8139** (PCI) с опросом RX/TX, ARP и
  статическим адресом `10.0.2.15`.

### 🖥️ Консоль
- Драйвер **линейного фреймбуфера** с выводом текста, шрифтами (PSF и
  встроенный `8x16`, включая кириллицу), ANSI-последовательностями и курсором.
- Текстовая консоль **VGA** как запасной вариант, если фреймбуфер не получен.
- Редактор строки на уровне ядра (эхо, backspace, буферизация строки),
  поэтому программа получает готовую строку.

### 🚀 Загрузка
- Ядро не привязано к загрузчику. Сведения о машине сводятся к одной структуре
  `struct boot_info`, а код каждого протокола - в своём каталоге:
  - **Multiboot2** (`sys/kernel/arch/x86/grub/multiboot2.c`, `multiboot2_gather()`)
  - **LINUX / bzImage** (`sys/kernel/arch/x86/linux/linux.c`, `linux_gather()`) -
    понимают GRUB (команда `linux`) и Limine (`protocol: linux`)
  - **Limine** (`sys/kernel/arch/x86/limine/limine.c`, `limine_gather()`)
- Поддерживаются прошивки **BIOS** и **UEFI**.
- Режим фреймбуфера ядро спрашивает у прошивки само:
  - **VBE** (VESA BIOS, `arch/x86/i686/vbe.c`) - под BIOS;
  - **GOP** (EFI Graphics Output Protocol, `arch/x86/i686/gop.c`) - под UEFI.
- Выбор источника в командной строке: `fb=vbe`, `fb=gop`, `fb=none`, `fb=auto`
  (по умолчанию: под UEFI - GOP, под BIOS - VBE) но еще не очень реализовано в mainline.
- Разделение кода: общий для 32 и 64 бит код - в `arch/x86/`, зависящий от
  разрядности (переход в реальный режим, 32-битные таблицы EFI) - в
  `arch/x86/i686/`.

### ⭐ Ключевые функции ядра

- **Память:** `pmm_init`, `pmm_alloc_zeroed`, `pmm_free_page`,
  `vmm_kernel_map`, `vmm_map_mmio`, `vmm_userspace_create`,
  `vmm_userspace_clone`, `vmm_user_map`.
- **Процессы:** `task_init`, `task_create`, `task_fork`, `task_execve`,
  `task_schedule`, `task_destroy`, `current_task`.
- **Системные вызовы:** `syscall_init`, `syscall_handler_asm`,
  `syscall_handler_c`.
- **VFS/ФС:** `vfs_init`, `vfs_open`, `vfs_open_flags`, `vfs_read`,
  `vfs_write`, `vfs_close`, `vfs_pipe`, `vfs_mount`, `vfs_register_device`,
  `vfs_lookup_device`, `vfs_socket*`; `fs_read`, `fs_write`, `fs_create`,
  `fs_delete`, `fs_read_at`; `initramfs_unpack`, `procfs_open_file`,
  `devtmpfs_open`.
- **ELF:** `elf_load_binary`.
- **Консоль:** `tty_init`, `tty_read`, `tty_write`, `tty_write_char`,
  `tty_draw_cursor`.
- **Драйверы:** `serial_init`, `serial_input_init`, `serial_putc`,
  `serial_puts`; `keyboard_init`, `keyboard_getc`, `keyboard_push_char`;
  `pic_init`; `pit_init`, `pit_get_ticks`, `pit_sleep`; `vga_putc_color`,
  `vga_clear`; `framebuffer_init`, `framebuffer_put_pixel`,
  `framebuffer_clear_color`; `rtl8139_init`, `rtl8139_poll`,
  `rtl8139_available`; `power_shutdown`.
- **Загрузка:** `boot_memmap_summary`, `multiboot2_gather`, `linux_gather`,
  `limine_gather`, `framebuffer_request_dispatch`, `vbe_request`, `gop_request`,
  `realmode_int10`.
- **Аварии:** `panic`, `panic_exception_gpf`, `panic_exception_div_zero`,
  `panic_exception_pf`.

## 🌱 bootstrap/

Минимальный свободностоящий юзерленд для проверки ядра: **PID 1** и маленький
шелл. Без libc и внешних зависимостей - все вызовы напрямую через `int $0x80`.

- `bootstrap/src/init.c` - PID 1: монтирует `/proc`, `/sys`, `/dev`, привязывает
  `/dev/tty0` к stdio (fd 0/1/2), запускает `/bin/sh` и перезапускает его после
  каждого выхода.
- `bootstrap/src/sh.c` - шелл: встроенные команды `help`, `clear` (`c`), `ver`,
  `fork`, `panic`, `exit`; любое другое слово пробуется как `/bin/<имя>`.

## 🛠️ Сборка и запуск

### Требования
`clang`, `ld.lld`, `grub-mkrescue`, `qemu-system-x86_64`, `cpio`, `objcopy` и
любой хост-компилятор C для настройки.

### Шаги

```bash
# 1. Клонировать репозиторий
git clone https://github.com/vxczxc/MazokOS.git && cd MazokOS

# 2. Сгенерировать конфигурацию (config.mk)
cc tools/configure.c -o conf
./conf

# 3. Собрать ядро, bootstrap и initramfs
make

# 4. Собрать загрузочный ISO-образ
make iso

# 5. Запустить в QEMU
make run
```

`make run` запускает QEMU с `-m 256M`, последовательностью `-serial stdio` и
сетевой картой `rtl8139`. Лог ядра идёт в COM1, поэтому его видно в терминале.

### Цели Makefile

| Цель | Что делает |
| --- | --- |
| `make` / `make all` | ядро + bootstrap + initramfs |
| `make kernel` | только ELF ядра |
| `make bzimage` | образ под протокол LINUX |
| `make bootstrap` | только юзерленд |
| `make initramfs` | только CPIO-образ |
| `make iso` | загрузочный ISO |
| `make run` | `make iso` + запуск в QEMU |
| `make clean` | очистка `build/` |

## 📑 Структура репозитория

- `sys/kernel/` - исходники ядра.
  - `arch/x86/` - общий для 32/64 бит код и код протоколов загрузки;
  - `arch/x86/i686/` - код, зависящий от разрядности (реальный режим, paging,
    GDT/IDT/TSS, VBE/GOP), и скрипт линкера;
  - `drivers/` - драйверы и их публичные заголовки;
  - `fs/` - VFS, procfs, devtmpfs, ramfs;
  - `include/` - общие заголовки ядра.
- `sys/linuxkpi/` - прослойка совместимости с ядерным API Linux (`printk`,
  `slab`, заголовки `linux/*.h`).
- `sys/tty/` - консоль/терминал и шрифты.
- `bootstrap/` - свободностоящий юзерленд (init + shell).
- `include/` - общие заголовки.
- `tools/` - хостовые утилиты сборки (`configure.c`, `mkbzimage.c`, `psf2h.c`).

## 🗺️ В планах

- DRM/KMS-подсистема.
- Полноценный переносимый сетевой стек.
- Рефакторинг подсистем графики и файловых систем.
- Документация, примеры программ и гайд MFS (Masix From Scratch).
- Официальный сайт с образами ядра.

## 📜 Лицензия

Исходники распространяются под **GPL-2.0-only** (см. заголовок SPDX в каждом
файле).

В будущем своя лицензия.

---

См. также [`CONTRIBUTING.md`](CONTRIBUTING.md) - как контрибьютить, и
[`AGENTS.md`](AGENTS.md) - заметки для агентов и правила работы с кодом.

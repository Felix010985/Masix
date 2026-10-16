.SILENT:

# Сгенерированная конфигурация (tools/configure). Значения здесь
# ниже только значения по умолчанию, приоритет у config.mk
-include config.mk

CC      := clang
LD      := ld.lld
GRUB    := grub-mkrescue
QEMU    := qemu-system-x86_64
OBJCOPY ?= objcopy
HOSTCC  ?= cc

BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/iso
BOOT_DIR  := $(ISO_DIR)/boot
GRUB_DIR  := $(BOOT_DIR)/grub

ROOTFS_SRC_DIR := $(BUILD_DIR)/rootfs
INITRAMFS_IMG  := $(BUILD_DIR)/initramfs.cpio

BOOTSTRAP_DIR := bootstrap

# Образы ядра: ELF для multiboot2 и Limine, физический образ и bzImage
# для протокола LINUX.
KERNEL_ELF  := $(BUILD_DIR)/kernel.elf
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
KERNEL_BZ   := $(BUILD_DIR)/kernel.bzImage
MKZ_TOOL    := $(BUILD_DIR)/tools/mkbzimage

TARGET_FLAGS ?= -target i686-unknown-elf
ARCH_CFLAGS  ?= -Isys/kernel/arch/x86/include -Isys/kernel/arch/x86/i686/include
LINKER_SCRIPT ?= sys/kernel/arch/x86/i686/linker.ld

SYS_CFLAGS  := $(TARGET_FLAGS) -Isys/linuxkpi/include $(ARCH_CFLAGS) -Isys/kernel/include -Isys/kernel/drivers/include -Isys/tty/include -Isys/kernel/fs/include -Iinclude -ffreestanding -nostdlib -fno-stack-protector -fno-pic -O0 -g -Wall -Wextra -MMD
# Юзерленд bootstrap: свободностоящий, без libc и субмодулей.
# Все системные вызовы, напрямую через int 0x80 (bootstrap/src/syscall.h).
BOOT_CFLAGS  := $(TARGET_FLAGS) -I$(BOOTSTRAP_DIR)/src -nostdinc -ffreestanding -mno-sse -mno-sse2 -fno-stack-protector -fno-pic -static -Os -ffunction-sections -fdata-sections -Wall -Wextra -MMD
BOOT_LDFLAGS := -nostdlib -static -Wl,-Ttext=0x02000000 -Wl,--gc-sections -Wl,-s

LDFLAGS ?= -m elf_i386 -T $(LINKER_SCRIPT) -n

SYS_SRCS   := $(shell find sys -name '*.c' 2>/dev/null | sort)
SYS_OBJS   := $(SYS_SRCS:sys/%.c=$(BUILD_DIR)/sys/%.o)

BOOT_SRCS  := $(shell find $(BOOTSTRAP_DIR) -name '*.c' 2>/dev/null | sort)
BOOT_ELFS  := $(BOOT_SRCS:$(BOOTSTRAP_DIR)/%.c=$(BUILD_DIR)/$(BOOTSTRAP_DIR)/%.elf)

ALL_OBJS   := $(SYS_OBJS)
DEP_FILES  := $(ALL_OBJS:%.o=%.d) $(BOOT_ELFS:%.elf=%.d)

CLR_RESET   := \033[0m
CLR_GREEN   := \033[1;32m
CLR_YELLOW  := \033[1;33m
CLR_RED     := \033[1;31m
CLR_CYAN    := \033[1;36m
CLR_GRAY    := \033[0;90m
CLR_MAGENTA := \033[1;35m

STATUS_OK   := [$(CLR_GREEN)  OK  $(CLR_RESET)]
STATUS_WARN := [$(CLR_YELLOW) WARN $(CLR_RESET)]
STATUS_INFO := [$(CLR_CYAN) INFO $(CLR_RESET)]

.PHONY: all
all: $(KERNEL_ELF) $(KERNEL_BZ) bootstrap $(INITRAMFS_IMG)
	printf "$(CLR_GREEN)==== Компиляция и линковка ядра и bootstrap успешно завершена! ====$(CLR_RESET)\n"

.PHONY: bootstrap
bootstrap: $(BOOT_ELFS)
	printf "$(CLR_GREEN)==== Сборка bootstrap успешно завершена! ====$(CLR_RESET)\n"

.PHONY: kernel
kernel: $(KERNEL_ELF)
	printf "$(CLR_GREEN)==== Сборка ядра Masix завершена ====$(CLR_RESET)\n"

.PHONY: bzimage
bzimage: $(KERNEL_BZ)
	printf "$(CLR_GREEN)==== Сборка образа bzImage завершена ====$(CLR_RESET)\n"

.PHONY: initramfs
initramfs: $(INITRAMFS_IMG)
	printf "$(CLR_GREEN)==== Сборка initramfs завершена ====$(CLR_RESET)\n"

.PHONY: iso
iso: $(BUILD_DIR)/mazukios.iso
	printf "$(CLR_GREEN)==== ISO-образ Masix полностью готов к тестированию! ====$(CLR_RESET)\n"

.PHONY: run
run: iso
	printf "$(CLR_MAGENTA)==== Запуск Masix в QEMU ====$(CLR_RESET)\n"
	$(QEMU) -cdrom $(BUILD_DIR)/mazukios.iso -m 256M -serial stdio -nic user,model=rtl8139 -no-reboot -no-shutdown

$(BUILD_DIR)/mazukios.iso: $(KERNEL_ELF) $(KERNEL_BZ) $(INITRAMFS_IMG) $(GRUB_DIR)/grub.cfg
	printf "$(STATUS_INFO) Создание ISO-образа с initramfs\n"
	mkdir -p $(BOOT_DIR)
	cp $(KERNEL_ELF) $(BOOT_DIR)/
	cp $(KERNEL_BZ) $(BOOT_DIR)/
	cp $(INITRAMFS_IMG) $(BOOT_DIR)/
	$(GRUB) -o $@ $(ISO_DIR) 2>/dev/null
	printf "$(STATUS_OK) ISO успешно собран: $(CLR_GREEN)$@$(CLR_RESET)\n"

# Физический образ ядра: тот самый, что грузят загрузчики по адресу 1 МБ.
$(KERNEL_BIN): $(KERNEL_ELF)
	printf "$(STATUS_INFO) Извлечение физического образа ядра\n"
	$(OBJCOPY) -O binary $< $@

# Инструмент хоста для сборки bzImage.
$(MKZ_TOOL): tools/mkbzimage.c
	printf "$(STATUS_INFO) Сборка инструмента mkbzimage\n"
	mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -o $@ $<

# Образ для протокола загрузки LINUX (GRUB: linux, Limine: protocol: linux).
$(KERNEL_BZ): $(KERNEL_BIN) $(MKZ_TOOL)
	printf "$(STATUS_INFO) Сборка образа bzImage\n"
	@entry=$$(nm $(KERNEL_ELF) | awk '$$3=="entry_start" {print $$1}'); \
	if [ "$$entry" != "00100000" ]; then \
		printf "$(CLR_RED)[FAIL]$(CLR_RESET) точка входа entry_start должна лежать по адресу 0x00100000 (получено 0x$$entry)\n"; \
		exit 1; \
	fi
	$(MKZ_TOOL) $(KERNEL_BIN) $@

$(BUILD_DIR)/kernel.elf: $(SYS_OBJS) $(LINKER_SCRIPT)
	printf "$(STATUS_INFO) Линковка ядра Masix...\n"
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SYS_OBJS)
	printf "      $(CLR_GRAY)| Размер файла: $$(wc -c < $@) байт$(CLR_RESET)\n"

# Bootstrap-бинарники: статические, без crt и libc,
# входная точка: собственный _start из исходника.
$(BUILD_DIR)/$(BOOTSTRAP_DIR)/%.elf: $(BOOTSTRAP_DIR)/%.c
	printf "$(STATUS_INFO) Компиляция bootstrap: $(CLR_CYAN)$<$(CLR_RESET) -> $@\n"
	mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) $(BOOT_LDFLAGS) -o $@ $< 2> $(BUILD_DIR)/tmp_boot_err.log || ( \
		printf "[$(CLR_RED)FAIL$(CLR_RESET)] CC (Bootstrap) $<\n"; \
		cat $(BUILD_DIR)/tmp_boot_err.log; \
		rm -f $(BUILD_DIR)/tmp_boot_err.log; \
		exit 1 \
	)
	WARN_COUNT=$$(grep -c "warning:" $(BUILD_DIR)/tmp_boot_err.log || true); \
	if [ $$WARN_COUNT -gt 0 ]; then \
		printf "$(STATUS_WARN) CC (Bootstrap) $(CLR_CYAN)$<$(CLR_RESET) -> $@ $(CLR_GRAY)(x$$WARN_COUNT warnings)$(CLR_RESET)\n"; \
		grep "warning:" $(BUILD_DIR)/tmp_boot_err.log | sed 's/^/      | /' | head -n 2; \
		if [ $$WARN_COUNT -gt 2 ]; then \
			printf "      $(CLR_GRAY)... и еще $$(($$WARN_COUNT - 2)) свернуто$(CLR_RESET)\n"; \
		fi; \
	fi; \
	rm -f $(BUILD_DIR)/tmp_boot_err.log

$(INITRAMFS_IMG): $(BOOT_ELFS)
	printf "$(STATUS_INFO) Упаковка initramfs в формат CPIO...\n"
	mkdir -p $(ROOTFS_SRC_DIR)/bin
	mkdir -p $(ROOTFS_SRC_DIR)/etc
	mkdir -p $(ROOTFS_SRC_DIR)/dev
	mkdir -p $(ROOTFS_SRC_DIR)/proc
	mkdir -p $(ROOTFS_SRC_DIR)/sys

	for elf in $(BOOT_ELFS); do \
		cp $$elf $(ROOTFS_SRC_DIR)/bin/$$(basename $$elf .elf); \
	done

	if [ ! -f $(ROOTFS_SRC_DIR)/bin/init ] && [ -f $(ROOTFS_SRC_DIR)/bin/sh ]; then \
		cp $(ROOTFS_SRC_DIR)/bin/sh $(ROOTFS_SRC_DIR)/bin/init; \
	fi

	echo "Welcome to Masix (kernel bootstrap)!" > $(ROOTFS_SRC_DIR)/etc/motd

	cd $(ROOTFS_SRC_DIR) && find . | cpio -o -H newc > ../../$(INITRAMFS_IMG) 2>/dev/null
	printf "      $(CLR_GRAY)| Размер initramfs: $$(wc -c < $(INITRAMFS_IMG)) байт$(CLR_RESET)\n"

	rm -rf $(ROOTFS_SRC_DIR)

$(BUILD_DIR)/sys/%.o: sys/%.c
	mkdir -p $(dir $@)
	$(CC) $(SYS_CFLAGS) -c $< -o $@ 2> $(BUILD_DIR)/tmp_sys_err.log || ( \
		printf "[$(CLR_RED)FAIL$(CLR_RESET)] CC (Kernel) $<\n"; \
		cat $(BUILD_DIR)/tmp_sys_err.log; \
		rm -f $(BUILD_DIR)/tmp_sys_err.log; \
		exit 1 \
	)
	WARN_COUNT=$$(grep -c "warning:" $(BUILD_DIR)/tmp_sys_err.log || true); \
	if [ $$WARN_COUNT -gt 0 ]; then \
		printf "$(STATUS_WARN) CC (Kernel)   $(CLR_CYAN)$<$(CLR_RESET) -> $@ $(CLR_GRAY)(x$$WARN_COUNT warnings)$(CLR_RESET)\n"; \
		grep "warning:" $(BUILD_DIR)/tmp_sys_err.log | sed 's/^/      | /' | head -n 2; \
		if [ $$WARN_COUNT -gt 2 ]; then \
			printf "      $(CLR_GRAY)... и еще $$(($$WARN_COUNT - 2)) свернуто$(CLR_RESET)\n"; \
		fi; \
	else \
		printf "$(STATUS_OK) CC (Kernel)   $(CLR_CYAN)$<$(CLR_RESET) -> $@\n"; \
	fi; \
	rm -f $(BUILD_DIR)/tmp_sys_err.log

$(GRUB_DIR)/grub.cfg:
	mkdir -p $(GRUB_DIR)
	printf "$(STATUS_INFO) Генерация конфигурации GRUB...\n"
	echo 'set timeout=5' > $@
	@echo 'set default=5' >> $@
	@echo '' >> $@
	@echo 'menuentry "Masix (multiboot2)" {' >> $@
	@echo '    multiboot2 /boot/kernel.elf' >> $@
	@echo '    module2 /boot/initramfs.cpio initramfs' >> $@
	@echo '    boot' >> $@
	@echo '}' >> $@
	@echo '' >> $@
	@echo 'menuentry "Masix (linux protocol)" {' >> $@
	@echo '    linux /boot/kernel.bzImage' >> $@
	@echo '    initrd /boot/initramfs.cpio' >> $@
	@echo '    boot' >> $@
	@echo '}' >> $@

.PHONY: clean
clean:
	printf "$(CLR_CYAN)==== Очистка рабочей директории build/ ====$(CLR_RESET)\n"
	rm -rf $(BUILD_DIR)


-include $(DEP_FILES)

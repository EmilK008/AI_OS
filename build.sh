#!/bin/bash
# ===========================================================================
# AI_OS Build Script
# Assembles bootloader, compiles kernel, links, and creates disk image
# ===========================================================================

set -e

# Tool paths
GCC_DIR="/c/Users/emilk/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$GCC_DIR:$PATH"

NASM="nasm"
CC="gcc"
LD="ld"
OBJCOPY="objcopy"

# Compiler flags for freestanding 32-bit x86 kernel
CFLAGS="-m32 -march=i686 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-stack-arg-probe -mno-sse -mno-sse2 -mno-mmx -nostdlib -nostdinc -Wall -Wextra -Iinclude -O2 -c"

# BearSSL needs its own include path (freestanding shim for <stdint.h>,
# <stddef.h>, <string.h>, <limits.h>) plus the library's own public and
# private headers. Warnings are downgraded because upstream code is tidy
# but triggers -Wextra edge cases. -w silences everything, -Wno-error=
# would be fine too. We keep -O2 for perf (crypto is expensive).
BEARSSL_CFLAGS="-m32 -march=i686 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-stack-arg-probe -mno-sse -mno-sse2 -mno-mmx -nostdlib -nostdinc -w -Ilib/bearssl_shim -Iinclude -Ilib/bearssl/inc -Ilib/bearssl/src -O2 -c"

LDFLAGS="-m i386pe -T linker.ld -nostdlib --section-alignment 16 --file-alignment 16"

BUILD_DIR="build"
IMG="$BUILD_DIR/ai_os.img"

echo "========================================"
echo "  AI_OS Build System v0.3 (GUI)"
echo "========================================"

# Create build directory
mkdir -p "$BUILD_DIR"

# Step 1: Assemble bootloader
echo "[1/5] Assembling bootloader..."
$NASM -f bin boot/boot.asm -o "$BUILD_DIR/boot.bin"
echo "      boot.bin: $(wc -c < "$BUILD_DIR/boot.bin") bytes"

# Step 2: Assemble kernel entry
echo "[2/5] Assembling kernel entry..."
$NASM -f elf32 kernel/kernel_entry.asm -o "$BUILD_DIR/kernel_entry.o"

# Step 3: Compile C sources
echo "[3/5] Compiling kernel..."
# Core
$CC $CFLAGS kernel/kernel.c    -o "$BUILD_DIR/kernel.o"
$CC $CFLAGS kernel/idt.c       -o "$BUILD_DIR/idt.o"
$CC $CFLAGS kernel/memory.c    -o "$BUILD_DIR/memory.o"
$CC $CFLAGS kernel/string.c    -o "$BUILD_DIR/string.o"
$CC $CFLAGS kernel/setjmp.S     -o "$BUILD_DIR/setjmp.o"
PUFF_CFLAGS="-m32 -march=i686 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-stack-arg-probe -mno-sse -mno-sse2 -mno-mmx -nostdlib -nostdinc -w -Iinclude -Ilib/puff -O2 -c"
$CC $PUFF_CFLAGS lib/puff/puff.c -o "$BUILD_DIR/puff.o"
$CC $CFLAGS kernel/bearssl_shim.c -o "$BUILD_DIR/bearssl_shim.o"
$CC $CFLAGS kernel/bearssl_entropy.c -o "$BUILD_DIR/bearssl_entropy.o"
$CC $CFLAGS kernel/process.c   -o "$BUILD_DIR/process.o"
$CC $CFLAGS kernel/fs.c        -o "$BUILD_DIR/fs.o"
$CC $CFLAGS -Ilib/puff kernel/net.c -o "$BUILD_DIR/net.o"
# net_tls.c and trust_anchors.c pull in BearSSL public headers, so they
# must compile with the BearSSL include paths / shim.
$CC $BEARSSL_CFLAGS kernel/net_tls.c       -o "$BUILD_DIR/net_tls.o"
$CC $BEARSSL_CFLAGS kernel/trust_anchors.c -o "$BUILD_DIR/trust_anchors.o"
# Drivers
$CC $CFLAGS drivers/vga.c      -o "$BUILD_DIR/vga.o"
$CC $CFLAGS drivers/keyboard.c -o "$BUILD_DIR/keyboard.o"
$CC $CFLAGS drivers/timer.c    -o "$BUILD_DIR/timer.o"
$CC $CFLAGS drivers/speaker.c  -o "$BUILD_DIR/speaker.o"
$CC $CFLAGS drivers/framebuffer.c -o "$BUILD_DIR/framebuffer.o"
$CC $CFLAGS drivers/mouse.c    -o "$BUILD_DIR/mouse.o"
$CC $CFLAGS drivers/rtc.c     -o "$BUILD_DIR/rtc.o"
$CC $CFLAGS drivers/ata.c     -o "$BUILD_DIR/ata.o"
$CC $CFLAGS drivers/pci.c     -o "$BUILD_DIR/pci.o"
$CC $CFLAGS drivers/rtl8139.c -o "$BUILD_DIR/rtl8139.o"
# Shell & Apps
$CC $CFLAGS shell/shell.c      -o "$BUILD_DIR/shell.o"
$CC $CFLAGS apps/editor.c      -o "$BUILD_DIR/editor.o"
$CC $CFLAGS apps/snake.c       -o "$BUILD_DIR/snake.o"
$CC $CFLAGS apps/snake_window.c -o "$BUILD_DIR/snake_window.o"
$CC $CFLAGS apps/filemgr.c     -o "$BUILD_DIR/filemgr.o"
$CC $CFLAGS apps/calculator.c  -o "$BUILD_DIR/calculator.o"
$CC $CFLAGS apps/notepad.c     -o "$BUILD_DIR/notepad.o"
$CC $CFLAGS apps/paint.c      -o "$BUILD_DIR/paint.o"
$CC $CFLAGS apps/settings.c   -o "$BUILD_DIR/settings.o"
$CC $CFLAGS apps/taskmanager.c -o "$BUILD_DIR/taskmanager.o"
$CC $CFLAGS apps/browser.c    -o "$BUILD_DIR/browser.o"
# GUI
$CC $CFLAGS gui/font_data.c    -o "$BUILD_DIR/font_data.o"
$CC $CFLAGS gui/event.c        -o "$BUILD_DIR/event.o"
$CC $CFLAGS gui/window.c       -o "$BUILD_DIR/window.o"
$CC $CFLAGS gui/desktop.c      -o "$BUILD_DIR/desktop.o"
$CC $CFLAGS gui/terminal.c     -o "$BUILD_DIR/terminal.o"
$CC $CFLAGS gui/appmenu.c      -o "$BUILD_DIR/appmenu.o"

# Step 3b: Compile BearSSL (TLS library) - client-only subset.
#
# We skip:
#   - rand/sysrng.c          (pulls in <wincrypt.h> / <unistd.h> via host libc;
#                             AI_OS supplies its own PRNG seeding in net_tls.c)
#   - ssl/ssl_server*.c      (server-side TLS; browser is client only)
#   - ssl/ssl_hs_server.c
#
# Everything else is pure C and compiles against our freestanding shim.
echo "[3b/5] Compiling BearSSL (TLS library)..."
BEARSSL_OBJDIR="$BUILD_DIR/bearssl"
mkdir -p "$BEARSSL_OBJDIR"

BEARSSL_OBJS=""
BEARSSL_FILE_COUNT=0
for src in $(find lib/bearssl/src -name '*.c' | sort); do
    base=$(basename "$src" .c)
    # Exclusion list
    case "$base" in
        sysrng)          continue ;;
        ssl_server*)     continue ;;
        ssl_hs_server)   continue ;;
    esac
    obj="$BEARSSL_OBJDIR/${base}.o"
    $CC $BEARSSL_CFLAGS "$src" -o "$obj"
    BEARSSL_OBJS="$BEARSSL_OBJS $obj"
    BEARSSL_FILE_COUNT=$((BEARSSL_FILE_COUNT + 1))
done
echo "      BearSSL: compiled $BEARSSL_FILE_COUNT translation units"

# Step 4: Link kernel
echo "[4/5] Linking kernel..."
$LD $LDFLAGS \
    "$BUILD_DIR/kernel_entry.o" \
    "$BUILD_DIR/kernel.o" \
    "$BUILD_DIR/idt.o" \
    "$BUILD_DIR/memory.o" \
    "$BUILD_DIR/string.o" \
    "$BUILD_DIR/setjmp.o" \
    "$BUILD_DIR/puff.o" \
    "$BUILD_DIR/bearssl_shim.o" \
    "$BUILD_DIR/bearssl_entropy.o" \
    "$BUILD_DIR/process.o" \
    "$BUILD_DIR/fs.o" \
    "$BUILD_DIR/vga.o" \
    "$BUILD_DIR/keyboard.o" \
    "$BUILD_DIR/timer.o" \
    "$BUILD_DIR/shell.o" \
    "$BUILD_DIR/editor.o" \
    "$BUILD_DIR/snake.o" \
    "$BUILD_DIR/snake_window.o" \
    "$BUILD_DIR/filemgr.o" \
    "$BUILD_DIR/calculator.o" \
    "$BUILD_DIR/notepad.o" \
    "$BUILD_DIR/paint.o" \
    "$BUILD_DIR/settings.o" \
    "$BUILD_DIR/taskmanager.o" \
    "$BUILD_DIR/browser.o" \
    "$BUILD_DIR/speaker.o" \
    "$BUILD_DIR/framebuffer.o" \
    "$BUILD_DIR/mouse.o" \
    "$BUILD_DIR/rtc.o" \
    "$BUILD_DIR/ata.o" \
    "$BUILD_DIR/pci.o" \
    "$BUILD_DIR/rtl8139.o" \
    "$BUILD_DIR/net.o" \
    "$BUILD_DIR/net_tls.o" \
    "$BUILD_DIR/trust_anchors.o" \
    "$BUILD_DIR/font_data.o" \
    "$BUILD_DIR/event.o" \
    "$BUILD_DIR/window.o" \
    "$BUILD_DIR/desktop.o" \
    "$BUILD_DIR/terminal.o" \
    "$BUILD_DIR/appmenu.o" \
    $BEARSSL_OBJS \
    -o "$BUILD_DIR/kernel.pe"

# Convert PE to flat binary.
# -R .bss drops the BSS section from the output; it's already marked NOLOAD
# in linker.ld and placed at 0x100000, but without -R objcopy would pad the
# binary with zeros all the way from end-of-.data to the BSS VMA, bloating
# kernel.bin by ~1 MB. BSS is zeroed at runtime in kernel_entry.asm.
$OBJCOPY -O binary -R .bss "$BUILD_DIR/kernel.pe" "$BUILD_DIR/kernel.bin"

KERNEL_BYTES=$(wc -c < "$BUILD_DIR/kernel.bin")
echo "      kernel.bin: $KERNEL_BYTES bytes"

# Sanity check: the bootloader reads KERNEL_SECTORS*512 bytes into
# physical 0x10000 in real mode. Writing past 0xA0000 (VGA memory hole)
# silently corrupts the display, and wrapping load_seg past 0xFFFF
# overwrites the IVT, yielding a dead black-screen boot.
#
# We pick a hard cap of 0x90000 bytes = 576 KiB of in-memory kernel
# (matches the bootloader-safe limit). Kernel.bin must also fit inside
# KERNEL_SECTORS * 512 bytes.
KERNEL_MAX_SECTORS=$(grep -E '^KERNEL_SECTORS\s+equ' boot/boot.asm | awk '{print $3}')
KERNEL_MAX_BYTES=$((KERNEL_MAX_SECTORS * 512))
if [ "$KERNEL_BYTES" -gt "$KERNEL_MAX_BYTES" ]; then
    echo "!!! kernel.bin ($KERNEL_BYTES B) exceeds bootloader capacity ($KERNEL_MAX_BYTES B)." >&2
    echo "!!! Either shrink the kernel or raise KERNEL_SECTORS in boot/boot.asm" >&2
    echo "!!! (but keep KERNEL_SECTORS*512 <= 0x90000 = 589824 or the boot will break)." >&2
    exit 1
fi

# Step 5: Create disk image
echo "[5/5] Creating disk image..."
cat "$BUILD_DIR/boot.bin" "$BUILD_DIR/kernel.bin" > "$BUILD_DIR/ai_os.img"

# Pad to 1.44MB floppy disk size (required for correct CHS geometry in QEMU)
IMGSIZE=$(wc -c < "$BUILD_DIR/ai_os.img")
FLOPPYSIZE=1474560
if [ "$IMGSIZE" -lt "$FLOPPYSIZE" ]; then
    dd if=/dev/zero bs=1 count=$(($FLOPPYSIZE - $IMGSIZE)) >> "$BUILD_DIR/ai_os.img" 2>/dev/null
fi

echo "      ai_os.img: $(wc -c < "$BUILD_DIR/ai_os.img") bytes"

# Create hard disk image for persistent storage (8MB, only if not existing)
DISK_IMG="$BUILD_DIR/disk.img"
if [ ! -f "$DISK_IMG" ]; then
    dd if=/dev/zero of="$DISK_IMG" bs=512 count=16384 2>/dev/null
    echo "      disk.img: 8 MB (created fresh)"
else
    echo "      disk.img: 8 MB (existing, preserved)"
fi

echo ""
echo "========================================"
echo "  Build complete!"
echo "  Run: qemu-system-i386 -fda build/ai_os.img -hda build/disk.img -vga std -m 128 -netdev user,id=net0 -device rtl8139,netdev=net0"
echo "========================================"

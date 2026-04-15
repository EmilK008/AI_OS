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
$CC $CFLAGS kernel/process.c   -o "$BUILD_DIR/process.o"
$CC $CFLAGS kernel/fs.c        -o "$BUILD_DIR/fs.o"
# Drivers
$CC $CFLAGS drivers/vga.c      -o "$BUILD_DIR/vga.o"
$CC $CFLAGS drivers/keyboard.c -o "$BUILD_DIR/keyboard.o"
$CC $CFLAGS drivers/timer.c    -o "$BUILD_DIR/timer.o"
$CC $CFLAGS drivers/speaker.c  -o "$BUILD_DIR/speaker.o"
$CC $CFLAGS drivers/framebuffer.c -o "$BUILD_DIR/framebuffer.o"
$CC $CFLAGS drivers/mouse.c    -o "$BUILD_DIR/mouse.o"
# Shell & Apps
$CC $CFLAGS shell/shell.c      -o "$BUILD_DIR/shell.o"
$CC $CFLAGS apps/editor.c      -o "$BUILD_DIR/editor.o"
$CC $CFLAGS apps/snake.c       -o "$BUILD_DIR/snake.o"
$CC $CFLAGS apps/snake_window.c -o "$BUILD_DIR/snake_window.o"
$CC $CFLAGS apps/filemgr.c     -o "$BUILD_DIR/filemgr.o"
$CC $CFLAGS apps/calculator.c  -o "$BUILD_DIR/calculator.o"
$CC $CFLAGS apps/notepad.c     -o "$BUILD_DIR/notepad.o"
# GUI
$CC $CFLAGS gui/font_data.c    -o "$BUILD_DIR/font_data.o"
$CC $CFLAGS gui/event.c        -o "$BUILD_DIR/event.o"
$CC $CFLAGS gui/window.c       -o "$BUILD_DIR/window.o"
$CC $CFLAGS gui/desktop.c      -o "$BUILD_DIR/desktop.o"
$CC $CFLAGS gui/terminal.c     -o "$BUILD_DIR/terminal.o"
$CC $CFLAGS gui/appmenu.c      -o "$BUILD_DIR/appmenu.o"

# Step 4: Link kernel
echo "[4/5] Linking kernel..."
$LD $LDFLAGS \
    "$BUILD_DIR/kernel_entry.o" \
    "$BUILD_DIR/kernel.o" \
    "$BUILD_DIR/idt.o" \
    "$BUILD_DIR/memory.o" \
    "$BUILD_DIR/string.o" \
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
    "$BUILD_DIR/speaker.o" \
    "$BUILD_DIR/framebuffer.o" \
    "$BUILD_DIR/mouse.o" \
    "$BUILD_DIR/font_data.o" \
    "$BUILD_DIR/event.o" \
    "$BUILD_DIR/window.o" \
    "$BUILD_DIR/desktop.o" \
    "$BUILD_DIR/terminal.o" \
    "$BUILD_DIR/appmenu.o" \
    -o "$BUILD_DIR/kernel.pe"

# Convert PE to flat binary
$OBJCOPY -O binary "$BUILD_DIR/kernel.pe" "$BUILD_DIR/kernel.bin"

echo "      kernel.bin: $(wc -c < "$BUILD_DIR/kernel.bin") bytes"

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
echo ""
echo "========================================"
echo "  Build complete!"
echo "  Run: qemu-system-i386 -fda build/ai_os.img -vga std -m 128"
echo "========================================"

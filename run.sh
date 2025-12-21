#!/bin/bash
# GB-EMU3 Run Script
# Rebuilds and runs the emulator with optional boot ROM support

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# Default boot ROM locations to check
BOOT_ROM_PATHS=(
    "$PROJECT_DIR/bootroms/dmg_boot.bin"
    "$PROJECT_DIR/dmg_boot.bin"
    "$PROJECT_DIR/boot.bin"
    "$PROJECT_DIR/roms/dmg_boot.bin"
    "$HOME/.local/share/gb-emu/dmg_boot.bin"
    "$HOME/roms/dmg_boot.bin"
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}"
echo "╔═══════════════════════════════════════╗"
echo "║         GB-EMU3 Build & Run           ║"
echo "╚═══════════════════════════════════════╝"
echo -e "${NC}"

# Create build directory if needed
mkdir -p "$BUILD_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo -e "${YELLOW}Configuring CMake...${NC}"
    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release
fi

# Always rebuild
echo -e "${YELLOW}Building...${NC}"
cmake --build "$BUILD_DIR" --parallel

echo -e "${GREEN}Build complete!${NC}"
echo ""

# Find boot ROM
BOOT_ROM=""
for path in "${BOOT_ROM_PATHS[@]}"; do
    if [ -f "$path" ]; then
        BOOT_ROM="$path"
        echo -e "${GREEN}Found boot ROM: $BOOT_ROM${NC}"
        break
    fi
done

if [ -z "$BOOT_ROM" ]; then
    echo -e "${YELLOW}No boot ROM found. Checked:${NC}"
    for path in "${BOOT_ROM_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo -e "${YELLOW}Running without boot ROM (skipping to \$0100)${NC}"
fi

# Build command with all arguments passed through
EMU_ARGS=()

if [ -n "$BOOT_ROM" ]; then
    EMU_ARGS+=("--boot-rom" "$BOOT_ROM")
fi

# Pass all script arguments to the emulator
EMU_ARGS+=("$@")

echo ""
echo -e "${CYAN}Running: $BUILD_DIR/gb-emu3 ${EMU_ARGS[*]}${NC}"
echo ""

exec "$BUILD_DIR/gb-emu3" "${EMU_ARGS[@]}"

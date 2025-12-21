#!/bin/bash

# Mooneye Test Suite Runner for GB-EMU3
# Hardware-accurate test runner - REQUIRES boot ROM
#
# Target: DMG-ABC (original Game Boy, revisions A/B/C)
# Note: Boot ROM is REQUIRED for hardware accuracy
#
# Usage: ./mooneye_runner.sh [OPTIONS]
#
# Categories:
#   --sanity     Quick sanity check - core tests (default)
#   --ppu        PPU tests only
#   --timer      Timer tests only
#   --oam        OAM DMA tests
#   --cpu        CPU timing & behavior tests
#   --bits       Hardware I/O bits tests
#   --interrupts Interrupt tests
#   --mbc        All MBC tests
#   --acceptance All acceptance tests
#   --all        Everything

set -e

EMULATOR="./build/gb-emu3"
TEST_DIR="test_roms/mts-20240926-1737-443f6e1"
TIMEOUT_SECONDS=30
RESULTS_FILE="mooneye_results.txt"
MAX_CYCLES=50000000  # 50M cycles (~12 seconds of emulation)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Boot ROM is REQUIRED for hardware accuracy
BOOTROM_PATH=""

CATEGORY="sanity"

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --boot-rom=*)
            BOOTROM_PATH="${arg#*=}"
            ;;
        --sanity)     CATEGORY="sanity" ;;
        --ppu)        CATEGORY="ppu" ;;
        --timer)      CATEGORY="timer" ;;
        --oam)        CATEGORY="oam" ;;
        --cpu)        CATEGORY="cpu" ;;
        --bits)       CATEGORY="bits" ;;
        --interrupts) CATEGORY="interrupts" ;;
        --mbc)        CATEGORY="mbc" ;;
        --mbc1)       CATEGORY="mbc1" ;;
        --mbc2)       CATEGORY="mbc2" ;;
        --mbc5)       CATEGORY="mbc5" ;;
        --acceptance) CATEGORY="acceptance" ;;
        --all)        CATEGORY="all" ;;
        --help|-h)
            echo "Mooneye Test Runner for GB-EMU3 (Hardware Accurate)"
            echo ""
            echo "REQUIRES boot ROM for hardware accuracy!"
            echo ""
            echo "Categories:"
            echo "  --sanity     Quick sanity - PPU, Timer, OAM, Bits (default)"
            echo "  --ppu        PPU tests"
            echo "  --timer      Timer tests"
            echo "  --oam        OAM DMA tests"
            echo "  --cpu        CPU timing & behavior"
            echo "  --bits       Hardware I/O bits"
            echo "  --interrupts Interrupt tests"
            echo "  --mbc        All MBC tests"
            echo "  --acceptance All acceptance"
            echo "  --all        Everything"
            echo ""
            echo "Options:"
            echo "  --boot-rom=PATH  Path to DMG boot ROM (REQUIRED)"
            echo ""
            echo "Example:"
            echo "  ./mooneye_runner.sh --boot-rom=dmg_boot.bin --ppu"
            exit 0
            ;;
        *) echo "Unknown: $arg (use --help)"; exit 1 ;;
    esac
done

# Find boot ROM if not specified
find_bootrom() {
    for dir in "." ".." "bootroms" "../bootroms" "$HOME/.local/share/gbemu/bootroms"; do
        for name in "dmg_boot.bin" "dmg0_boot.bin" "boot.bin"; do
            if [ -f "$dir/$name" ]; then
                local size=$(stat -c%s "$dir/$name" 2>/dev/null || stat -f%z "$dir/$name" 2>/dev/null)
                if [ "$size" = "256" ]; then
                    realpath "$dir/$name"
                    return 0
                fi
            fi
        done
    done
    return 1
}

if [ -z "$BOOTROM_PATH" ]; then
    BOOTROM_PATH=$(find_bootrom) || true
fi

if [ -z "$BOOTROM_PATH" ] || [ ! -f "$BOOTROM_PATH" ]; then
    echo -e "${RED}ERROR: Boot ROM not found!${NC}"
    echo "Hardware-accurate testing REQUIRES a DMG boot ROM."
    echo "Use --boot-rom=PATH to specify location."
    exit 1
fi

echo -e "${GREEN}Boot ROM:${NC} $BOOTROM_PATH"

# Build emulator
echo -e "${CYAN}Building emulator...${NC}"
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1 && make -j$(nproc) > /dev/null 2>&1 && cd ..
[ ! -f "$EMULATOR" ] && echo "ERROR: Emulator build failed" && exit 1

PASS=0 FAIL=0 TOTAL=0 SKIP=0
declare -a FAILED=()

# DMG-ABC compatibility filter
is_dmg_abc() {
    local b=$(basename "$1")
    # Skip CGB/AGB only
    [[ "$b" == *"-C.gb" || "$b" == *"-cgb"* || "$b" == *"-A.gb" || "$b" == *"-agb"* ]] && return 1
    # Skip DMG-0 (pre-production), SGB-only, MGB-only
    [[ "$b" == *"-dmg0"* || "$b" == *"-S.gb" || "$b" == *"-sgb"* ]] && return 1
    [[ "$b" == "boot_regs-mgb.gb" ]] && return 1
    return 0
}

run_test() {
    local rom="$1" name=$(basename "$1")
    ((TOTAL++))
    local out exit_code=0
    
    # Run test with boot ROM
    out=$(timeout $TIMEOUT_SECONDS "$EMULATOR" --headless --cycles $MAX_CYCLES --boot-rom "$BOOTROM_PATH" "$rom" 2>&1) || exit_code=$?

    if echo "$out" | grep -q 'TEST PASSED'; then
        echo -e "  ${GREEN}✓${NC} $name"; ((PASS++))
    elif echo "$out" | grep -q 'TEST FAILED'; then
        echo -e "  ${RED}✗${NC} $name"; FAILED+=("$name"); ((FAIL++))
    elif [ $exit_code -eq 124 ] || [ $exit_code -eq 137 ]; then
        echo -e "  ${YELLOW}⏱${NC} $name (timeout)"; FAILED+=("$name [timeout]"); ((FAIL++))
    else
        echo -e "  ${YELLOW}?${NC} $name (no result)"; FAILED+=("$name [?]"); ((FAIL++))
    fi
}

run_dir() {
    local name="$1" path="$2"
    echo -e "\n${CYAN}=== $name ===${NC}"
    [ ! -d "$path" ] && echo -e "  ${YELLOW}(not found)${NC}" && return
    for rom in "$path"/*.gb; do
        [ -f "$rom" ] || continue
        is_dmg_abc "$rom" || { ((SKIP++)); continue; }
        run_test "$rom" || true
    done
}

run_files() {
    local name="$1"; shift
    echo -e "\n${CYAN}=== $name ===${NC}"
    for rom in "$@"; do
        [ -f "$rom" ] || continue
        is_dmg_abc "$rom" || { ((SKIP++)); continue; }
        run_test "$rom" || true
    done
}

echo -e "\n${CYAN}=== Mooneye Test Runner (GB-EMU3) ===${NC}"
echo -e "${YELLOW}Category: $CATEGORY | Target: DMG-ABC (Hardware Accurate)${NC}"

case "$CATEGORY" in
    sanity)
        run_dir "Timer" "$TEST_DIR/acceptance/timer"
        run_dir "Bits" "$TEST_DIR/acceptance/bits"
        run_dir "PPU" "$TEST_DIR/acceptance/ppu"
        run_dir "OAM DMA" "$TEST_DIR/acceptance/oam_dma"
        run_files "OAM DMA (root)" "$TEST_DIR/acceptance"/oam_dma*.gb
        run_dir "Interrupts" "$TEST_DIR/acceptance/interrupts"
        ;;
    ppu)
        run_dir "PPU" "$TEST_DIR/acceptance/ppu"
        ;;
    timer)
        run_dir "Timer" "$TEST_DIR/acceptance/timer"
        ;;
    oam)
        run_dir "OAM DMA" "$TEST_DIR/acceptance/oam_dma"
        run_files "OAM DMA (root)" "$TEST_DIR/acceptance"/oam_dma*.gb
        ;;
    cpu)
        run_files "Call Timing" "$TEST_DIR/acceptance"/call_*.gb
        run_files "Ret Timing" "$TEST_DIR/acceptance"/ret_*.gb "$TEST_DIR/acceptance"/reti_*.gb
        run_files "JP Timing" "$TEST_DIR/acceptance"/jp_*.gb
        run_files "RST Timing" "$TEST_DIR/acceptance/rst_timing.gb"
        run_files "Push/Pop Timing" "$TEST_DIR/acceptance/push_timing.gb" "$TEST_DIR/acceptance/pop_timing.gb"
        run_files "LD/ADD Timing" "$TEST_DIR/acceptance/ld_hl_sp_e_timing.gb" "$TEST_DIR/acceptance/add_sp_e_timing.gb"
        run_files "Halt" "$TEST_DIR/acceptance"/halt_*.gb
        run_files "EI/DI" "$TEST_DIR/acceptance"/ei_*.gb "$TEST_DIR/acceptance"/di_*.gb "$TEST_DIR/acceptance/rapid_di_ei.gb"
        run_files "Interrupt/Div Timing" "$TEST_DIR/acceptance/intr_timing.gb" "$TEST_DIR/acceptance/div_timing.gb"
        ;;
    bits)
        run_dir "Bits" "$TEST_DIR/acceptance/bits"
        ;;
    interrupts)
        run_dir "Interrupts" "$TEST_DIR/acceptance/interrupts"
        run_files "Interrupts (root)" "$TEST_DIR/acceptance/if_ie_registers.gb"
        ;;
    mbc1)
        run_dir "MBC1" "$TEST_DIR/emulator-only/mbc1"
        ;;
    mbc2)
        run_dir "MBC2" "$TEST_DIR/emulator-only/mbc2"
        ;;
    mbc5)
        run_dir "MBC5" "$TEST_DIR/emulator-only/mbc5"
        ;;
    mbc)
        run_dir "MBC1" "$TEST_DIR/emulator-only/mbc1"
        run_dir "MBC2" "$TEST_DIR/emulator-only/mbc2"
        run_dir "MBC5" "$TEST_DIR/emulator-only/mbc5"
        ;;
    acceptance)
        run_dir "Timer" "$TEST_DIR/acceptance/timer"
        run_dir "Bits" "$TEST_DIR/acceptance/bits"
        run_dir "PPU" "$TEST_DIR/acceptance/ppu"
        run_dir "OAM DMA" "$TEST_DIR/acceptance/oam_dma"
        run_files "OAM DMA (root)" "$TEST_DIR/acceptance"/oam_dma*.gb
        run_dir "Interrupts" "$TEST_DIR/acceptance/interrupts"
        run_files "Interrupts (root)" "$TEST_DIR/acceptance/if_ie_registers.gb"
        run_files "Call Timing" "$TEST_DIR/acceptance"/call_*.gb
        run_files "Ret Timing" "$TEST_DIR/acceptance"/ret_*.gb "$TEST_DIR/acceptance"/reti_*.gb
        run_files "JP Timing" "$TEST_DIR/acceptance"/jp_*.gb
        run_files "RST Timing" "$TEST_DIR/acceptance/rst_timing.gb"
        run_files "Push/Pop Timing" "$TEST_DIR/acceptance/push_timing.gb" "$TEST_DIR/acceptance/pop_timing.gb"
        run_files "LD/ADD Timing" "$TEST_DIR/acceptance/ld_hl_sp_e_timing.gb" "$TEST_DIR/acceptance/add_sp_e_timing.gb"
        run_files "Halt" "$TEST_DIR/acceptance"/halt_*.gb
        run_files "EI/DI" "$TEST_DIR/acceptance"/ei_*.gb "$TEST_DIR/acceptance"/di_*.gb "$TEST_DIR/acceptance/rapid_di_ei.gb"
        run_files "Interrupt/Div Timing" "$TEST_DIR/acceptance/intr_timing.gb" "$TEST_DIR/acceptance/div_timing.gb"
        ;;
    all)
        run_dir "MBC1" "$TEST_DIR/emulator-only/mbc1"
        run_dir "MBC2" "$TEST_DIR/emulator-only/mbc2"
        run_dir "MBC5" "$TEST_DIR/emulator-only/mbc5"
        run_dir "Timer" "$TEST_DIR/acceptance/timer"
        run_dir "Bits" "$TEST_DIR/acceptance/bits"
        run_dir "PPU" "$TEST_DIR/acceptance/ppu"
        run_dir "OAM DMA" "$TEST_DIR/acceptance/oam_dma"
        run_files "OAM DMA (root)" "$TEST_DIR/acceptance"/oam_dma*.gb
        run_dir "Interrupts" "$TEST_DIR/acceptance/interrupts"
        run_files "Interrupts (root)" "$TEST_DIR/acceptance/if_ie_registers.gb"
        run_files "Call Timing" "$TEST_DIR/acceptance"/call_*.gb
        run_files "Ret Timing" "$TEST_DIR/acceptance"/ret_*.gb "$TEST_DIR/acceptance"/reti_*.gb
        run_files "JP Timing" "$TEST_DIR/acceptance"/jp_*.gb
        run_files "RST Timing" "$TEST_DIR/acceptance/rst_timing.gb"
        run_files "Push/Pop Timing" "$TEST_DIR/acceptance/push_timing.gb" "$TEST_DIR/acceptance/pop_timing.gb"
        run_files "LD/ADD Timing" "$TEST_DIR/acceptance/ld_hl_sp_e_timing.gb" "$TEST_DIR/acceptance/add_sp_e_timing.gb"
        run_files "Halt" "$TEST_DIR/acceptance"/halt_*.gb
        run_files "EI/DI" "$TEST_DIR/acceptance"/ei_*.gb "$TEST_DIR/acceptance"/di_*.gb "$TEST_DIR/acceptance/rapid_di_ei.gb"
        run_files "Interrupt/Div Timing" "$TEST_DIR/acceptance/intr_timing.gb" "$TEST_DIR/acceptance/div_timing.gb"
        ;;
esac

echo -e "\n${CYAN}=== Summary ===${NC}"
echo -e "  Passed: ${GREEN}$PASS${NC} / $TOTAL"
echo -e "  Failed: ${RED}$FAIL${NC}"
[ $SKIP -gt 0 ] && echo -e "  Skipped: ${YELLOW}$SKIP${NC} (non-DMG-ABC)"

if [ ${#FAILED[@]} -gt 0 ]; then
    echo -e "\n${RED}Failed:${NC}"
    for t in "${FAILED[@]}"; do echo "  - $t"; done
fi

{
    echo "=== Mooneye $(date) ==="
    echo "Category: $CATEGORY | Passed: $PASS/$TOTAL | Failed: $FAIL"
    [ ${#FAILED[@]} -gt 0 ] && printf '%s\n' "Failed:" "${FAILED[@]/#/  - }"
} > "$RESULTS_FILE"

exit $FAIL

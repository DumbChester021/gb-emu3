# HANDOFF PROMPT: Fixing lcdon_timing-GS Test

## Context for Next Session

You are continuing work on a cycle-accurate Game Boy DMG emulator. The goal is to fix the `lcdon_timing-GS` Mooneye test.

## Current Status

- **85/89 Mooneye tests passing**
- **4 failing tests**: 
  - `lcdon_timing-GS.gb` (primary focus)
  - `lcdon_write_timing-GS.gb`
  - `hblank_ly_scx_timing-GS.gb`
  - `intr_2_mode0_timing_sprites.gb`

## The Current Failure

```
Test failed: STAT LYC=0
Cycle: $6F (111)
Expected: $80
Actual: $82
```

- $80 = mode 0 (HBLANK), no flags
- $82 = mode 2 (OAM_SCAN), no flags
- **The issue is MODE BITS, not LYC coincidence**

## Key Files

- `src/ppu/PPU.cpp` - Main PPU implementation
- `src/ppu/PPU.hpp` - PPU header
- `docs/PPU_TIMING_LEARNINGS.md` - Detailed learnings
- `test_roms/mooneye-test-suite-main/acceptance/ppu/lcdon_timing-GS.s` - Test source

## What Was Discovered

### The Contradiction in the Test

At cycle 111, the test expects BOTH:
- **LY (FF44) = $01** (visible LY is 1)
- **STAT with LYC=1 = $80** (no LYC coincidence!)

If LY=1 and LYC=1, they should match. But the test says NO match.

**This proves: Visible LY changes BEFORE STAT LYC comparison updates!**

### The Solution: Phased PPU Architecture

Real DMG hardware has intra-cycle ordering. We implemented:

```cpp
// PHASE 1: Commit visibility changes
if (dot_counter == 0 && ly_update_pending) {
    ly = next_ly;  // Visible LY changes NOW
    ly_update_pending = false;
    ly_comparator_pending = true;  // Schedule for NEXT phase
}

// PHASE 2: Update comparator (one phase after visible LY)
if (ly_comparator_pending) {
    ly_for_comparison = ly;
    ly_comparator_pending = false;
    CheckStatInterrupt();  // STAT bit 2 updates NOW
}
```

This makes visible LY lead and STAT LYC lag by one phase.

### Current Problem: Mode Bits

The LYC timing issue may be fixed now, but the test is failing on **MODE BITS**:
- Expected $80 = mode 0 (HBLANK)
- Actual $82 = mode 2 (OAM_SCAN)

At cycle 111 after line 0 ends:
- Internal mode = OAM_SCAN (for line 1)
- But STAT should still show mode 0 (HBLANK)

**Mode visibility likely also needs phased updates.**

## What Was Tried (and failed)

1. **Latching STAT mode bits at every transition**: Caused 6-test regression
2. **Removing ly_for_comparison update from line end**: Caused 1-test regression
3. **Using effective_ly for STAT reads**: Oscillated between fixing LY and breaking STAT
4. **Various combinations**: Kept oscillating

## What Needs to Be Done

1. **Implement mode visibility phasing**: 
   - Add `mode_visible` separate from `mode` (internal)
   - Mode visible updates one phase after internal mode changes
   - STAT reads use `mode_visible`, not `mode`

2. **Full phased architecture**:
   - Phase 1: Commit visibility changes (ly, mode_visible)
   - Phase 2: Evaluate comparators, update STAT bits
   - Phase 3: Mode logic, scheduling future changes

## Key Test Data (from test source)

```
cycle_counts:
.db 0   17  60  110 130 174 224 244
.db 1   18  61  111 131 175 225 245
.db 2   19  62  112 132 176 226 246

expect_stat_lyc0:
.db $84 $84 $87 $84 $82 $83 $80 $82
.db $84 $87 $84 $80 $82 $80 $80 $82  <- Row 2, position 3 (cycle 111) = $80
.db $84 $87 $84 $82 $83 $80 $82 $83

expect_stat_lyc1:
.db $80 $80 $83 $80 $86 $87 $84 $82
.db $80 $83 $80 $80 $86 $84 $80 $82  <- Row 2, position 3 (cycle 111) = $80
.db $80 $83 $80 $86 $87 $84 $82 $83
```

## How to Build and Test

```bash
cd build
make -j$(nproc)
cd ..
./mooneye_runner.sh --all    # Run all tests
./build/gb-emu3 --headless test_roms/mts-20240926-1737-443f6e1/acceptance/ppu/lcdon_timing-GS.gb  # Run specific test with trace
```

## Reference Emulator

- SameBoy 1.0.2 source at `reference/SameBoy-1.0.2/Core/display.c`
- Uses `ly_for_comparison` for internal timing
- Has sophisticated STAT update logic

## ChatGPT Insights (December 2025)

ChatGPT confirmed:
1. A phased PPU model is more hardware-accurate
2. DMG has intra-cycle ordering - visibility and internal state update at different moments
3. Trying to fix this with flags like `effective_ly` will keep oscillating
4. Separating visibility, comparison, and scheduling resolves it cleanly
5. Mode visibility likely needs same treatment as LY visibility

## Next Steps

1. Add `mode_visible` variable (separate from `mode`)
2. In Phase 1: delay `mode_visible` update similar to `ly`
3. In STAT read: use `mode_visible` instead of `mode`
4. Test if this fixes the mode $82 vs $80 issue
5. If it works, consider full phased refactor

## Important Notes

- Line 0 after LCD enable is special (451 dots instead of 456)
- Mode transitions happen at line end but visibility may lag
- Keep the separation: `mode` (internal), `mode_for_interrupt`, `mode_visible`
- Reference `lcd_just_enabled` and `first_line_after_lcd` flags for special cases

# HANDOFF PROMPT: PPU Timing Status (December 2024)

> **STATUS: `lcdon_timing-GS` HAS BEEN FIXED!**

## Context

This document was originally created for handoff during debugging of `lcdon_timing-GS`. 
**That test is NOW PASSING.** This document is preserved for historical reference.

---

## Current Status (January 2025)

- **87/89 Mooneye tests passing** ✅
- **2 remaining failing tests**:
  - `hblank_ly_scx_timing-GS.gb` (4-cycle timing offset issue - see PPU_TIMING_LEARNINGS.md)
  - `intr_2_mode0_timing_sprites.gb` (sprite timing)

## What Was Fixed

The phased PPU architecture was successfully implemented:

1. **Visible LY changes BEFORE STAT LYC comparison updates** - proven by test expectations
2. **Mode visibility delay of 4 cycles** for HBlank→OAM_SCAN transition
3. **OAM/VRAM access blocking with explicit flags** at precise T-cycle timings
4. **Line-end timing pattern** - set state at line end for visibility at next line's dot 0

## Key Implementation Details

See `docs/PPU_TIMING_LEARNINGS.md` for complete technical details.

### State Separation
- `ly` - visible LY value (CPU sees this)
- `ly_for_comparison` - internal comparison value (lags behind visible LY)
- `mode` - internal PPU mode
- `mode_visible` - visible mode (STAT reads), lags internal mode at line transitions

### OAM/VRAM Blocking (Hardware-Accurate)
- Uses explicit `oam_read_blocked`, `vram_read_blocked` flags
- NOT mode-based checks (which would be a hack)
- Set/cleared at precise T-cycle timings per SameBoy

## Reference Files

- `src/ppu/PPU.cpp` - Main PPU implementation
- `src/ppu/PPU.hpp` - PPU header with state variables
- `docs/PPU_TIMING_LEARNINGS.md` - Detailed technical learnings
- `reference/SameBoy-1.0.2/Core/display.c` - Reference implementation

## Remaining Work (Future Sessions)

### hblank_ly_scx_timing-GS.gb
**Problem**: 4-cycle timing offset between dot_counter and SameBoy's cycles_for_line.

**Root Cause Analysis (January 2025)**:
- OAM scan runs dots 0-79, but SameBoy uses cycles 4-83 (4-cycle offset)
- Mode 3 starts at dot 85 vs SameBoy cycle 89
- LY changes at dot 3 = cycle 3 (NO offset)
- This inconsistency causes 206-dot delta instead of correct 202-dot

**Fix Attempted & Reverted**: Comprehensive 4-cycle shift gave correct 202-dot delta but caused 5 regressions (intr_2_* and lcdon_* tests).

**See**: `docs/PPU_TIMING_LEARNINGS.md` for complete analysis.

### intr_2_mode0_timing_sprites.gb
Sprite timing during mode transitions. Requires careful analysis of OAM scan and sprite fetch timing.

---

## How to Run Tests

```bash
cd build && make -j$(nproc) && cd ..
./mooneye_runner.sh --all    # Run all tests
```

## Principles (DO NOT COMPROMISE)

1. **Never guess** - verify against SameBoy and test ROM sources
2. **No hacks** - use explicit timing flags, not "good enough" mode checks
3. **Test passing is not the goal** - hardware accuracy is
4. **Components don't see each other** - model the separation of real hardware
5. **Regressions need investigation** - don't just revert, understand why

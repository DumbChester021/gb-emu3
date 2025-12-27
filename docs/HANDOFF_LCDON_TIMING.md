# HANDOFF PROMPT: PPU Timing Status (December 2024)

> **STATUS: `lcdon_timing-GS` HAS BEEN FIXED!**

## Context

This document was originally created for handoff during debugging of `lcdon_timing-GS`. 
**That test is NOW PASSING.** This document is preserved for historical reference.

---

## Current Status (December 27, 2024)

- **86/89 Mooneye tests passing** ✅
- **3 remaining failing tests**:
  - `hblank_ly_scx_timing-GS.gb` (SCX affects Mode 3 duration - deferred, see learnings)
  - `intr_2_mode0_timing_sprites.gb` (sprite timing)
  - `lcdon_write_timing-GS.gb` (write timing during LCD enable)

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
**Problem**: SCX affects Mode 3 duration. Test expects different LY increment timing based on SCX value.

**Attempt Made & Reverted**: Tried using `167 + (SCX & 7)` formula from SameBoy but it caused 4-test regression because:
- SameBoy's formula is for **batched Mode 3 fast-path**
- Our PPU runs **pixel-by-pixel FIFO rendering**
- The approaches are fundamentally incompatible without restructuring

**Future Approach Options**:
1. Implement conditional scanline batching (match SameBoy's batching conditions)
2. Adjust FIFO fetcher delays based on SCX
3. Research SameBoy's non-batched SCX handling in FIFO path

### intr_2_mode0_timing_sprites.gb
Sprite timing during mode transitions. Requires careful analysis of OAM scan and sprite fetch timing.

### lcdon_write_timing-GS.gb
Write timing during the LCD enable sequence. May require special handling of writes during the first line after LCD enable.

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

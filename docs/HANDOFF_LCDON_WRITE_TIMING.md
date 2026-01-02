# Handoff: lcdon_write_timing-GS Fixed

**Date**: December 27, 2024  
**Status**: 87/89 Mooneye tests passing

## What Was Fixed

`lcdon_write_timing-GS.gb` - OAM and VRAM **write** timing after LCD enable.

### Key Insight: Loop Exit Timing

When PPU::Step(4) runs, it processes 4 dots and exits with `dot_counter = N+4`.
Checks at dot N only fire if N is PROCESSED during iteration.
**Writes at dot N mean loop exited AT N without processing the check.**

### Fixes Applied

| Blocking Flag | Unblock At | Re-block At | Accessible Window |
|---------------|------------|-------------|-------------------|
| `oam_write_blocked` | dot 76 | dot 80 | writes at 77-80 |
| `vram_write_blocked` | (N/A) | dot 83 | blocked from 84+ |

All changes reference specific SameBoy 1.0.2 `display.c` line numbers.

## Remaining Failures (2)

1. **`hblank_ly_scx_timing-GS.gb`** - 4-cycle timing offset
   - Root cause: OAM scan at dots 0-79 vs SameBoy cycles 4-83
   - Mode 3 starts 4 dots early, causing 206-dot delta instead of 202
   - See `docs/PPU_TIMING_LEARNINGS.md` for complete analysis
   
2. **`intr_2_mode0_timing_sprites.gb`** - Mode 3 duration with sprites
   - Sprite fetching timing affects when HBlank interrupt fires

## Files Changed

- `src/ppu/PPU.cpp` - OAM/VRAM write blocking timing fixes
- `docs/PPU_TIMING_LEARNINGS.md` - Detailed documentation of fixes
- `README.md` - Updated test count to 87/89

## Resources

- SameBoy 1.0.2: `reference/SameBoy-1.0.2/Core/display.c`
- PPU timing docs: `docs/PPU_TIMING_LEARNINGS.md`
- Test sources: `test_roms/mooneye-test-suite-main/acceptance/ppu/`

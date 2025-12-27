# PPU Timing Learnings

This document captures learnings from debugging PPU timing issues to achieve hardware accuracy.

## LCD Enable (LCDC bit 7 = 1) Behavior

### SameBoy Reference (display.c lines 1660-1714)
After LCD enable, Line 0 has special timing:
- Starts in Mode 0 (not Mode 2)
- `cycles_for_line = MODE2_LENGTH - 4` (76 cycles in Mode 0)
- Then 2 cycles OAM write blocked
- Then +8 cycles (total 86)
- Then Mode 3 starts (STAT mode bits = 3 at cycle 86 = **dot 85**)
- Does NOT do normal OAM scan on line 0

### Key Implementation Details
1. `lcd_just_enabled` flag tracks special line 0 behavior
2. `dot_counter = 0` reset on LCD enable
3. Mode 3 transition at `dot_counter == 85` (visible at cycle 86) - **UPDATED from 77**

## Boot ROM Consideration

**IMPORTANT**: When boot ROM is enabled, it enables LCD at the end of boot sequence.
- First LCD enable is from boot ROM, not the test
- Tests call `disable_ppu_safe` which turns LCD OFF before running test passes
- Need to skip boot ROM LCD enable when analyzing test timing

## CPU/PPU Cycle Synchronization

### Pending Cycles Pattern (per SameBoy)
```
FlushPendingCycles(); // Step PPU by deferred cycles
// ... memory operation ...
pending_cycles = 4;   // Defer cycles for next op
```

### Memory Access Timing
- `FetchByte`: Flush, read opcode, defer 4
- `ReadByte`: Flush, read data, defer 4
- `WriteByte`: Flush, write data, defer 4

### First LY Read After LCD Enable
Instruction sequence:
```
ldh (<LCDC), a   ; M1: fetch, M2: fetch imm, M3: write (LCD ON, dot=0)
ld a, (de)       ; M1: flush 4→dot=4, M2: flush 4→dot=8, read LY
```
Expected: First LY read at dot 8

**CURRENT ISSUE**: Trace shows first LY read at dot 60 (15 M-cycles), not dot 8.
- Constant 52-dot (13 M) spacing between reads
- test_reads macro has VARIABLE nops (13, 39, 46, 16, 40, 46, 16)
- Constant spacing suggests tracing wrong code path

## lcdon_timing-GS Test Structure

### Test Passes (3 passes x 8 reads each)
- Pass 1: cycles 0, 17, 60, 110, 130, 174, 224, 244
- Pass 2: cycles 1, 18, 61, 111, 131, 175, 225, 245
- Pass 3: cycles 2, 19, 62, 112, 132, 176, 226, 246

### Expected LY Values (Pass 1)
```
.db $00 $00 $00 $00 $01 $01 $01 $02
```
- Cycles 0-110 (dots 0-440): LY=$00 (line 0)
- Cycles 130-224 (dots 520-896): LY=$01 (line 1)
- Cycle 244 (dot 976): LY=$02 (line 2)

## Debug Tracing Techniques

### Filtering Test Reads vs Setup Reads
`disable_ppu_safe` polls LY via `wait_ly_with_timeout` for VBlank.
Use `lcd_just_enabled` flag to reset trace counter and filter only post-enable reads.

### Skip Boot ROM LCD Enable
Use `seen_lcd_off` flag to detect when LCD is disabled, then track first LCD enable after that (test's LCD enable, not boot ROM's).

### Test Dump Infrastructure
Test failures auto-dump to `test_dumps/` folder with test name.
Convert PGM to PNG: `python3 -c "from PIL import Image; Image.open('test.pgm').save('test.png')"`

## Debugging Session: 2023-12-23

### Problem Statement
lcdon_timing-GS test fails. Need to fix LY and STAT timing after LCD enable.

### Key Findings

#### 1. TRACE LOGIC BUG (FIXED)
**Problem**: Initial traces showed constant 52-dot spacing between LY reads.
**Root Cause**: `seen_lcd_off` was set in ReadRegister, but LCD turns off in WriteRegister!
**Solution**: Added `g_lcd_went_off` global set in WriteRegister when LCD turns OFF.

#### 2. Line 0 After LCD Enable - Critical Timing Data
From SameBoy display.c analysis:
- **cycles_for_line = 76 + 2 + 8 + 2 + 3 = 91 before mode_3_start**
- STAT mode = 3 becomes visible at cycle 86 (dot 85)
- Actual rendering (mode_3_start) begins at cycle 91 (dot 90)
- Line 0 is 8 cycles SHORTER than normal (comment at line 1690: "Mode 0 is shorter on the first line 0")

#### 3. Mode 3 Duration on Line 0
**CRITICAL**: Mode 3 must run from dot 85 to ~dot 253 (168 dots duration).
- Mode 3 at dot 85, Mode 0 (HBLANK) at dot 253
- Test reads at cycle 60 = dot 248, expects Mode 3 ($87)
- Test reads at cycle 110 = dot 448, expects Mode 0 ($84) - but this is line-end HBLANK, not Mode 3→0 transition

#### 4. Line 0 Length After LCD Enable
**Expected**: Line 0 ends between dot 448-452 (4-8 dots shorter than normal 456)
- Cycle 110 (dot 448): LY=0 expected (still on line 0)
- Cycle 111 (dot 452): LY=1 expected (on line 1)
- This means line 0 ends between 448-451

### Changes Attempted (CAUSED REGRESSIONS)

1. **line_end = 451** - First line after LCD ends 4 dots early
2. **Mode 3 STAT at dot 85** - Per SameBoy cycle 86 timing
3. **Reset lcd_x and position_in_line at dot 85** - To prevent immediate HBLANK

**RESULT**: Caused 4 NEW test failures (81/89 instead of 85/89)
- vblank_stat_intr-GS.gb
- halt_ime0_nointr_timing.gb
- halt_ime1_timing2-GS.gb
- di_timing-GS.gb

**CONCLUSION**: These changes broke interrupt/halt timing. Need to investigate WHY - possibly the line_end change affects VBLANK timing or the Mode 3 STAT change affects interrupt timing.

### Next Steps
1. Revert to stable 85/89 state
2. Make changes MORE CAREFULLY - one at a time
3. Test each change against full suite before proceeding
4. The core timing may be correct but implementation has side effects

### Important Constants (from SameBoy)
- MODE2_LENGTH = 80 (OAM scan duration)
- LINES = 144 (visible lines)
- VIRTUAL_LINES = 154 (total lines including VBlank)
- Line duration = 456 dots normally
- Line 0 after LCD = 448-452 dots (shortened)

## SameBoy cycles_for_line Mechanism (deep analysis)

### How SameBoy Tracks Line Duration
SameBoy uses `cycles_for_line` counter to track elapsed time in current line.
- At line end (HBLANK): `GB_SLEEP(display, 11, LINE_LENGTH - cycles_for_line - 2)` - line 2132
- This means HBLANK duration = 456 - cycles_for_line - 2

### Line 0 After LCD Enable Timing

From display.c lines 1679-1714:
```c
gb->cycles_for_line = MODE2_LENGTH - 4;  // = 76 (line 1679)
GB_SLEEP(gb, display, 2, MODE2_LENGTH - 4);  // SLEEP 76 cycles (line 1681)
gb->cycles_for_line += 2;                 // = 78 (line 1684)
GB_SLEEP(gb, display, 34, 2);             // SLEEP 2 more (line 1686) - TOTAL SLEEP = 78!
gb->cycles_for_line += 8;                 // = 86 (line 1690) - counter only, NO SLEEP
gb->io_registers[GB_IO_STAT] |= 3;        // STAT mode = 3 HERE (line 1693)
// ...later...
gb->cycles_for_line += 2;                 // = 88 (line 1704)  
gb->cycles_for_line += 3;                 // = 91 (line 1708)
goto mode_3_start;                        // (line 1714)
```

**CRITICAL CORRECTION**: STAT mode = 3 is set after **SLEEP 78 cycles** (76+2), NOT after 86.
The `+= 8` adds to counter for HBLANK calculation, but has NO SLEEP call!
Mode 3 STAT is visible at **cycle 78 = dot 77**, not dot 85 or 91.

### The "+8 cycles" Comment (line 1690)
> "Mode 0 is shorter on the first line 0, so we augment cycles_for_line by 8 extra cycles."

This is CRITICAL: The ending Mode 0 (HBLANK after rendering) is 8 cycles shorter than normal.
The +8 is added to cycles_for_line to compensate, BUT:
- For normal line: cycles_for_line = 80 (OAM) + ~172 (Mode 3) = ~252
- For line 0 after LCD: cycles_for_line = 91 + ~172 = ~263

HBLANK durations:
- Normal: 456 - 252 - 2 = 202 dots
- Line 0: 456 - 263 - 2 = 191 dots (-11 dots!)

### Test Expectations vs Implementation

Test expects:
- Cycle 110 (dot 448): LY=0 (still on line 0)
- Cycle 111 (dot 452): LY=1 (on line 1)

This means LY changes between dot 448-451. Line 0 is **~4-8 dots shorter than 456**.

## CRITICAL LEARNING: Unified Control Flow (from debugging session)

### The Anti-Pattern: Early Returns for Special Cases

**BAD approach (causes timing bugs):**
```cpp
if (lcd_just_enabled && ly == 0) {
    if (dot_counter == 77) {
        mode = PIXEL_TRANSFER;
        // ...
    }
    return;  // <-- THIS BREAKS EVERYTHING
}
```

The early return prevents:
- Normal mode transition machinery from running
- STAT bits from being properly derived
- Consistent dot advancement
- Unified timing pipeline

### The Correct Pattern: Conditional Thresholds

**GOOD approach:**
```cpp
// Determine threshold for this line type
uint16_t mode3_start = lcd_just_enabled ? 78 : normal_value;

if (dot_counter == mode3_start) {
    mode = PIXEL_TRANSFER;
    // STAT mode bits update HERE
}
// Normal processing continues - no early return
```

### Key Insight
> "In a dot, STAT is sampled DURING the dot. Mode transitions must be visible
> immediately. If you early return, the STAT read observes stale state."

SameBoy does NOT special case line 0 with early returns - it special cases
the **timing values** but runs through the **same mode progression machinery**.

### Current Implementation (Mode 3 at dot 77, no early return)
- Unified control flow in StepHBlank
- Line 0 after LCD: Mode 0→3 at dot 82
- Line 0 ends at dot 451 (4 dots shorter)
- Uses `first_line_after_lcd` member variable (not static)
- 85/89 tests passing, no regressions

## CRITICAL LEARNING: STAT is a VIEW, Not Authoritative Source

### The Key Principle (from external LLM analysis)
> "The STAT register is a VIEW of PPU state with dot-level quirks. It is NOT
> the authoritative source of PPU mode, and it MUST NOT be used to drive 
> STAT interrupt timing."

### What I Learned (the hard way)

**WRONG approach (caused 6 test regression):**
```cpp
// Latching STAT at ALL mode transitions
mode = HBLANK;
stat = (stat & ~0x03) | HBLANK;  // DON'T do this at every transition!
```
This broke interrupt timing tests because:
- STAT bits lie during dot-based quirks
- Interrupts depend on INTERNAL mode transitions, not STAT bits
- Tests like intr_2_mode0_timing verify exact cycle-level interrupt edges

**CORRECT approach:**
1. **Internal mode** drives interrupts and pipeline control
2. **STAT mode bits** are a VIEW with special cases:
   - LCD line 0: Explicitly latched at LCD enable and dot 77
   - Normal lines: Derived from internal mode with dot-based quirks

### The STAT View Model

| Line Type | STAT Source | Notes |
|-----------|-------------|-------|
| LCD line 0 after enable | Latched stat bits | Written at LCD enable, dot 77 |
| Normal visible lines | Derived from mode | With OAM dot 0 and dot 83+ quirks |
| VBlank | Derived from mode | mode = VBLANK |

### Code Implementation
```cpp
// STAT read handler
if (lcd_just_enabled && ly == 0) {
    stat_mode = stat & 0x03;  // Use latched bits
} else {
    stat_mode = mode;  // Derive from internal mode
    // Apply OAM quirks...
}
```

### Key Separation
- **mode** = internal PPU state, drives interrupts
- **stat & 0x03** = latched visible mode (only matters for LCD line 0)
- **mode_for_interrupt** = separate tracking for interrupt timing

## CRITICAL LEARNING: LY and STAT LYC Bit Timing (December 2024)

### The Golden Rule
> **STAT bit 2 (LYC coincidence) must change no earlier than the moment LY would change if read by the CPU.**

### The Key Contradiction Discovered

At cycle 111 of `lcdon_timing-GS`, the test expects BOTH of these to be true:
- **LY (FF44) = $01** (new value visible)
- **STAT with LYC=1 = $80** (no LYC coincidence!)

This proves: **visible LY changes BEFORE STAT LYC comparison updates!**

### Phased PPU Architecture (The Correct Solution)

Real hardware has **intra-cycle ordering**. A phased model is required:

```cpp
void PPU::Step() {
    // PHASE 1: Commit visibility changes
    if (dot_counter == 0 && ly_update_pending) {
        ly = next_ly;  // Visible LY changes NOW
        ly_update_pending = false;
        ly_comparator_pending = true;  // Schedule comparator for NEXT phase
    }
    
    // PHASE 2: Update comparator (one phase AFTER visible LY)
    if (ly_comparator_pending) {
        ly_for_comparison = ly;
        ly_comparator_pending = false;
        CheckStatInterrupt();  // STAT bit 2 updates NOW
    }
    
    // PHASE 3: Mode logic, pixel pipeline, scheduling...
}
```

### State Categories

| Category | Variables | When Updated | CPU Sees |
|----------|-----------|--------------|----------|
| **Visible** | `ly` | Phase 1 (commit) | Yes |
| **Internal** | `ly_for_comparison` | Phase 2 (logic) | No |
| **Scheduled** | `next_ly`, `ly_update_pending` | Phase 3 (schedule) | No |

### Regressions During Debugging

1. **85→79** (6 test regression): Latching STAT mode bits at all transitions
2. **85→84** (1 test regression): Removing `ly_for_comparison` update from line end
3. Oscillation between fixing LY test and breaking STAT test

### Current Status (December 26, 2024)

- **85/89 Mooneye tests passing**
- `lcdon_timing-GS` still fails at STAT LYC=0 cycle 111: expected $80, got $82
- $82 = mode 2 (OAM_SCAN) - mode bits issue, not LYC
- The phased model is implemented but mode visibility still needs work

### Remaining Work

1. **Mode visibility phasing**: Mode bits in STAT may also need phased updates
2. **Full phased refactor**: Separate all visibility from internal state
3. Consider: `mode_visible` vs `mode_internal` separation

### What Does NOT Work (Anti-Patterns)

1. Latching STAT mode bits globally at every mode transition
2. Advancing `ly_for_comparison` and STAT bit 2 together at line end
3. Using only one comparison value for both STAT visibility and interrupt timing
4. Treating CPU-visible state and internal state as the same thing

### References

- SameBoy `display.c` - uses `ly_for_comparison` for internal timing
- Mooneye `lcdon_timing-GS.s` - test source with expected values
- Pan Docs - STAT/LYC constantly compared (visible state)

## CRITICAL LEARNING: Line Transition STAT Mode Bits (December 26, 2024)

### Discovery from SameBoy Analysis

At the start of each normal line (not line 0 after LCD enable), SameBoy does the following sequence (display.c lines 1770-1792):

```
1. GB_SLEEP(2 cycles)
2. GB_SLEEP(1 cycle)
3. LY = current_line                    // LY visible to CPU
4. For line != 0: STAT &= ~3            // Mode bits = 0 (HBLANK!)
5. GB_STAT_update()                     // Evaluate interrupts
6. GB_SLEEP(1 cycle)                    // 1 MORE CYCLE with mode bits = 0
7. STAT |= 2                            // NOW mode bits become 2 (OAM_SCAN)
```

### Key Insight

**There is a 1-cycle window where LY shows the new line value, but STAT mode bits are STILL 0 (HBLANK)!**

This is NOT a "staged visibility" problem - it's that SameBoy explicitly:
1. Sets STAT mode bits to 0 at the moment LY changes
2. Waits 1 cycle
3. THEN sets STAT mode bits to 2

### Why This Matters for lcdon_timing-GS

At cycle 111:
- LY reads as $01 (line 1)
- STAT expected as $80 (mode 0 = HBLANK, no LYC match)
- We were returning $82 (mode 2 = OAM_SCAN)

The test is reading during that 1-cycle window where STAT mode is still 0!

### The Correct Fix

Instead of a generic "mode visibility delay", we need to:
1. At line transition, explicitly set STAT mode bits to 0
2. Wait 1 cycle (use a pending flag or countdown)
3. THEN set STAT mode bits to 2

This matches the SameBoy pattern of explicitly clearing and then setting the mode bits.

### Implementation Notes

SameBoy doesn't use a "mode_visible" abstraction. Instead, it directly manipulates `io_registers[GB_IO_STAT]` mode bits at precise cycle points. The mode bits are a direct output, not derived from internal mode.

### Working Solution (December 26, 2024)

After extensive debugging, the correct timing for mode visibility at line transitions was determined:

**Mode visibility delay = 4 cycles for HBlank→OAM_SCAN transition**

This creates the following behavior:
- Line ends at dot 451 (or 455 for normal lines)
- At line end: internal mode → OAM_SCAN, mode_visible stays HBLANK, delay=4
- dot 0: delay=4 → 3 (mode_visible still HBLANK)
- dot 1: delay=3 → 2
- dot 2: delay=2 → 1
- dot 3: delay=1 → 0, mode_visible → OAM_SCAN
- dot 4+: mode_visible = OAM_SCAN (STAT shows mode 2)

Test expectations for cycle 111-112:
- cycle 111 (dot 0): expects $80 (mode 0) ✓
- cycle 112 (dot 4): expects $82 (mode 2) ✓

Other mode transitions should be **immediate** (no delay):
- OAM_SCAN → PIXEL_TRANSFER: immediate
- PIXEL_TRANSFER → HBLANK: immediate  
- HBLANK → VBLANK: immediate

## CRITICAL FINDING: CPU Read vs PPU Step Ordering (December 26, 2024)

### The Problem

Debug trace revealed that **CPU reads STAT BEFORE PPU Phase 1 runs** on the same dot:

```
STAT_RD: ly=0 dot=0 ly_cmp=0 lyc=0 stat_b2=1 result=$84   <- READ first (WRONG)
PHASE1: ly=1 ly_cmp=-1 stat_b2=1                          <- Phase 1 AFTER read
AFTER_CHECK: stat_b2=0                                     <- Bit cleared too late
STAT_RD: ly=1 dot=0 ly_cmp=1 lyc=0 stat_b2=0 result=$80   <- Next read correct
```

### Why This Happens

The pending cycles pattern works like this:
1. CPU flushes pending cycles → PPU::Step runs for PREVIOUS cycles
2. CPU performs memory operation (reads STAT)
3. CPU accumulates new pending cycles
4. PPU::Step runs for current cycle (Phase 1 updates state)

This means Phase 1 state changes (LY visibility, LYC comparison) happen AFTER the CPU read on the same dot.

### Required Fix

**IMPLEMENTED SOLUTION (December 26, 2024):**

Update visibility state at **LINE END** instead of dot 0:

1. At line end (dot 451/455):
   - Set `ly_for_comparison = (next_ly != 0) ? -1 : 0`
   - Clear `stat &= ~0x04` directly (avoid CheckStatInterrupt to prevent IRQ side effects)
   - Set `ly_comparator_delay = 4` to schedule Phase 2 update

2. Phase 1 (dot 0):
   - Only commit visible `ly = next_ly` and clear `ly_update_pending`
   - DON'T update ly_for_comparison or call CheckStatInterrupt

3. Phase 2 (after 4 dots):
   - Set `ly_for_comparison = ly`
   - Call `CheckStatInterrupt()` to update STAT bit 2 properly

This ensures:
- CPU reads at dot 0 see `ly_for_comparison=-1` (cleared at line end)  
- CPU reads at dot 4+ see `ly_for_comparison=actual_ly` (updated by Phase 2)

**Status:** 85/89 tests passing (no regressions). lcdon_timing-GS OAM/VRAM access tests in progress.

---

## OAM/VRAM Access Blocking (December 26, 2024)

### Problem
lcdon_timing-GS test failing on OAM and VRAM access timing. Tests expect specific blocking/unblocking at precise T-cycle boundaries.

### Hardware-Accurate Implementation

**Per SameBoy**: OAM/VRAM access uses explicit blocking flags, NOT mode-based checks.

#### Blocking Flags Added to PPU.hpp:
```cpp
bool oam_read_blocked;      // OAM reads return $FF when true
bool oam_write_blocked;     // OAM writes ignored when true
bool vram_read_blocked;     // VRAM reads return $FF when true
bool vram_write_blocked;    // VRAM writes ignored when true
```

#### OAM Blocking Timing:
- **Set true**: Mode 2 entry (dot 0), lcd_just_enabled Mode 3 entry (dot 77)
- **Set false**: HBlank entry, VBlank entry, LCD enable, LCD off
- **Line-end**: Set `oam_read_blocked = true` at line end for non-VBlank transitions (CPU reads BEFORE PPU Step)

#### VRAM Blocking Timing:
- **Set true**: Mode 3 entry (dot 84 for normal lines, dot 77 for lcd_just_enabled)
- **Set false**: HBlank entry, VBlank entry, LCD enable, LCD off
- **NOT blocked during Mode 2** (only Mode 3)

#### Critical Discovery: Line-End Timing Pattern
Same issue as `ly_for_comparison`: **CPU reads at cycle 111 (dot 0 of next line) happen BEFORE PPU Step runs**.

Solution: Set blocking flags at **LINE END** (dot 451/455), not dot 0:
```cpp
// At line end for non-VBlank transitions
if (next_ly < 144) {
    oam_read_blocked = true;
    oam_write_blocked = true;
}
```

This ensures CPU reads at dot 0 see the correct blocked state.

### Test Progress
- **STAT LYC tests**: ✅ PASSING (ly_for_comparison line-end fix)
- **OAM access tests**: ✅ PASSING (oam_read_blocked implementation)
- **VRAM access tests**: ✅ PASSING (vram_read_blocked at dot 76)

### SOLUTION: VRAM Blocking at Dot 76

**Per SameBoy display.c lines 1807-1818**: VRAM is blocked at **OAM index 37** during Mode 2.

**Timing calculation**:
- OAM scan loop: 40 entries × 2 cycles each = 80 cycles
- After 4 initial cycles + 37 entries × 2 = 78 cycles into line
- SameBoy cycle 78 from line start = dot 76 in our implementation

**Implementation** (StepOAMScan):
```cpp
if (dot_counter == 76) {
    vram_read_blocked = true;
    vram_write_blocked = true;
}
```

**Key insight**: VRAM blocking happens **during Mode 2** (not at Mode 3 entry), specifically when OAM scanning reaches entry 37.

**Test Result**: lcdon_timing-GS.gb now **PASSING** ✅  
**Overall**: **86/89 tests passing** (+1 from baseline)

---

## Summary of Key Learnings

### 1. Line-End Timing Pattern (Critical Discovery)
**When CPU reads happen BEFORE PPU Step runs**, state must be set at previous line end, not current dot:
- Applied to: `ly_for_comparison`, STAT bit 2, `oam_read_blocked`
- Example: CPU read at cycle 111 (dot 0 line 1) happens before PPU Step(dot 0) runs
- Solution: Set state at dot 451/455 (line end) for visibility at next line's dot 0

### 2. Hardware-Accurate Memory Access Flags
**Don't use mode-based checks** - use explicit T-cycle timing flags:
- Old (hacky): `if (mode == PIXEL_TRANSFER) return 0xFF;`
- New (accurate): `if (vram_read_blocked) return 0xFF;`
- Flags set/cleared at precise dots matching SameBoy hardware behavior

### 3. OAM/VRAM Blocking Timing (DMG)
**OAM**: Blocked during Mode 2 + Mode 3
- Blocked at: Mode 2 entry (dot 0), lcd_just_enabled Mode 3 (dot 77), line end
- Unblocked at: HBlank, VBlank, LCD enable/off

**VRAM**: Blocked during Mode 2 (from OAM index 37) + Mode 3
- Blocked at: dot 76 (OAM index 37), Mode 3 entry (dot 84), lcd_just_enabled (dot 77)
- Unblocked at: HBlank, VBlank, LCD enable/off

### 4. SameBoy References
All implementations verified against SameBoy 1.0.2 `display.c`:
- Line 1697: OAM blocking
- Line 1701: VRAM blocking (lcd_just_enabled)
- Line 1776: ly_for_comparison = -1
- Line 1790: Mode 2 OAM blocking
- Lines 1807-1818: VRAM blocking at OAM index 37
- Line 2104: HBlank unblocking

### 5. Test Suite Status
**86/89 Mooneye tests passing**

**Passing**:
- ✅ All CPU instruction tests
- ✅ All MBC tests (MBC1, MBC2, MBC5)
- ✅ All timer tests
- ✅ All interrupt timing tests
- ✅ STAT LYC timing tests
- ✅ **lcdon_timing-GS.gb** (STAT, OAM, VRAM access)

**Remaining Failures** (3 tests):
- hblank_ly_scx_timing-GS.gb (SCX affects Mode 3 duration - see investigation below)
- intr_2_mode0_timing_sprites.gb (sprite timing during mode transitions)
- lcdon_write_timing-GS.gb (write timing during LCD enable sequence)

---

## SCX Timing Investigation (December 26, 2024) - ATTEMPTED & REVERTED

### Problem
Test `hblank_ly_scx_timing-GS.gb` checks how SCX (scroll X) affects HBlank timing.

**Test expectations**:
- SCX mod 8 = 0: LY increments **51 cycles** after STAT Mode 0 interrupt
- SCX mod 8 = 1-4: **50 cycles** after
- SCX mod 8 = 5-7: **49 cycles** after

### SameBoy Investigation
Found in `display.c line 1493: mode3_batching_length()`:
```c
// No objects or window, timing is trivial
return 167 + (gb->io_registers[GB_IO_SCX] & 7);
```

**Mode 3 duration = 167 + (SCX & 7) cycles**

This explains the timing variation - SCX adds 0-7 cycles to Mode 3, affecting when HBlank starts and LY increments.

### Implementation Attempt
Added `mode3_cycles` counter to end Mode 3 at `167 + (SCX & 7)` cycles.

**Result**: **REGRESSION from 86/89 to 82/89 tests**

Broke:
- lcdon_timing-GS (was passing!)
- intr_2_0_timing
- intr_2_mode0_timing
- intr_2_oam_ok_timing

### Root Cause
**Critical discovery**: SameBoy's formula is for **batched Mode 3 fast-path** used when:
- No sprites on scanline
- Window not active
- Simple rendering possible

Our PPU runs **pixel-by-pixel FIFO rendering** every cycle. Forcing Mode 3 to end at a specific cycle count conflicts with the FIFO's natural pixel output timing.

### Reversion
**Reverted all changes** to restore 86/89 baseline.

Removed:
- `mode3_cycles` counter from PPU.hpp
- Cycle-based Mode 3 ending check
- All mode3_cycles initialization
- Restored original pixel-based Mode 3 ending (`lcd_x >= 160`)

### Key Learning
**Don't blindly copy formulas** - understand the context:
- SameBoy has **two rendering paths**: batched (fast) and pixel-by-pixel (slow)
- The `167 + (SCX & 7)` formula only applies to batched path
- Our FIFO implementation is pixel-by-pixel, needs different approach

**Future approach for SCX timing**:
1. Conditionally batch simple scanlines (match SameBoy's batching conditions)
2. OR adjust FIFO fetcher delays based on SCX
3. OR research SameBoy's non-batched SCX handling

**Status**: Test deferred. Maintaining 86/89 baseline is priority.

---

## lcdon_write_timing-GS Investigation (December 27, 2024) - DEFERRED

### Problem
Test `lcdon_write_timing-GS.gb` checks OAM/VRAM **write** access timing after LCD enable.
Different from `lcdon_timing-GS` (read test, passes).

### Test Structure
- 19 NOP counts spanning multiple scanlines
- Tests both OAM and VRAM write access

### SameBoy Timing (display.c 1674-1705)
```
lcd_just_enabled line 0:
  Cycle 0-75:  All access unblocked
  Cycle 76:    oam_write_blocked = true (LINE 1683)
  Cycle 78:    oam_read_blocked = true, VRAM blocked
```

**Key finding**: Write blocking happens **2 cycles BEFORE** read blocking!

### Debug Trace Results
Line 0 timing appears correct, but issues continue onto line 1.
The test spans multiple scanlines - requires fixes for BOTH lcd_just_enabled AND normal lines.

### Status
**DEFERRED** - All 3 remaining failures share common issue: **precise Mode 3 duration**.
Our FIFO implementation determines Mode 3 length by pixel output, not cycle counting.

---

## Attempted Fix: Line 0 Length Change (December 27, 2024) - REVERTED

### What Was Tried
Changed `first_line_after_lcd` line length from 451 → 456 dots to match SameBoy's LINE_LENGTH.

### Result
**REGRESSION** - The `lcdon_timing-GS` (read test) which was passing started failing.

### Lesson Learned
The 451-dot line 0 length is **hardware-accurate for read timing**. Changing it caused the 
read test to fail. Line length is NOT the issue for write timing.

### Key Insights from ROM Analysis

**Test data verified from compiled ROM:**
```
nop_counts: 0, 17, 18, 60, 61, 110, 111, 112, 130, 131, 132, ...
expect_oam (index 8, nop=130): $00 (blocked)
expect_oam (index 9, nop=131): $81 (accessible)
```

**The test probes Mode 2/HBlank boundaries:**
- nop=130 at ~dot 528: blocked (Mode 2 of line 1 if line 0 is 451 dots, OR Mode 3 end of line 0)
- nop=131 at ~dot 532: accessible (should be HBlank)

### Root Cause Hypothesis
The issue is NOT line 0 length. It's that writes at nop=131 (dot ~532) should be in HBlank
of the current line's rendering cycle, but our Mode 3 runs too long OR our HBlank timing 
is misaligned.

**SameBoy OAM write blocking timeline:**
- Dot 3: `oam_write_blocked = true` (Mode 2 starts, our fix applied)
- Dot ~252-256: `oam_write_blocked = false` (HBlank starts, Mode 3 ends)

For line 0 after LCD enable (451 dots), HBlank starts around dot 252. Writes at dot 528
would be at dot 77 of line 1, which is Mode 2 → blocked. But the test expects accessible!

**Possible issue:** The test may expect writes to be checked against a DIFFERENT timing model
than reads. OR there's an off-by-one in our pending cycles accounting.

### Status
Reverted line length change. Further investigation needed on OAM write timing.

---

## OAM Write Unblocking During Mode 2→Mode 3 Transition (December 27, 2024) - IMPLEMENTED

### Critical Discovery
SameBoy display.c line 1821 at OAM index 37 (~dot 76):
```cpp
gb->oam_write_blocked = GB_is_cgb(gb);  // FALSE for DMG!
```

This creates a brief window (dots 76-84) where OAM **writes** are accessible during the Mode 2→Mode 3 transition, even though OAM **reads** remain blocked.

### Implementation
Added in StepOAMScan:
1. **Dot 76**: `oam_write_blocked = false` (per SameBoy line 1821)
2. **Dot 84**: `oam_write_blocked = true` (per SameBoy line 1833 at Mode 3 start)

### Test Results
- `lcdon_timing-GS` (read test): Still PASSES ✓
- `lcdon_write_timing-GS`: Still failing, but nop=131 at dot 80 now accessible

### Remaining Issues
The write test has 19 test points probing multiple timing boundaries:
- Line 0 lcd_just_enabled LCD transitions
- Mode 2→Mode 3 transitions (dots 76-84)
- Mode 3→Mode 0 transitions (HBlank entry)
- Line boundary transitions

Each boundary has slightly different timing for reads vs writes.

### Current Status
86/89 tests passing (no regression). The OAM write blocking during Mode 2→3 transition
is now hardware-accurate per SameBoy, but the complete write timing test requires fixing
ALL timing boundaries, not just this one.

---

## Critical Discovery: Loop Exit Timing vs Check Timing (December 27, 2024)

### The Fundamental Issue
When PPU::Step(4) processes 4 dots, it iterates through dots N to N+3, then exits with
`dot_counter = N+4`. Checks inside StepOAMScan fire DURING iterations (at specific dot values),
but CPU writes see `dot_counter` AFTER the loop exits.

**Example:** 
- PPU starts at dot 72, Step(4) processes dots 72, 73, 74, 75
- After loop, dot_counter = 76
- CPU write sees dot_counter = 76, but check at `dot_counter == 76` never fired!

This creates a timing gap:
- **READ tests**: CPU reads happen mid-step, see the check results
- **WRITE tests**: CPU writes happen AFTER step completes, see the exit value

### Why Fixes Conflict
Changing `if (dot_counter == 76)` to `if (dot_counter == 75)` would make the check
fire before the loop exits at 76, but this breaks VRAM read timing which expects
the check at 76.

### Solution Required
The `lcdon_write_timing-GS` test may require:
1. Separating OAM write blocking from VRAM read blocking into different checks
2. OR restructuring how the check loop interacts with memory access timing
3. OR understanding that SameBoy may have different internal mechanics for syncing

### Status
This is a fundamental architectural issue. Fixes that work for writes break reads.
Further investigation needed on how SameBoy synchronizes memory access timing
with the PPU state machine.

---

## Mode 2→Mode 3 OAM Write Window Fix (December 27, 2024) - IMPLEMENTED

### Problem
nop=130 (dot 76 line 1) should be blocked, nop=131 (dot 80 line 1) should be accessible.

### Root Cause
Due to loop exit timing, checks at dot N only fire if that dot is PROCESSED during iteration.
Writes that happen when `dot_counter == N` mean the loop exited AT N without processing it.

### Solution
Separated OAM write blocking from VRAM read blocking:
- **Dot 76**: Set `oam_write_blocked = false` - fires for writes at dot 77+
- **Dot 80**: Set `oam_write_blocked = true` - blocks for writes at dot 81+
- **VRAM read blocking stays at dot 76** - this affects reads that see dot 77+ after processing

### Result
- nop=130 at dot 76: blocked ✓ (loop exits at 76 without processing unblock)
- nop=131 at dot 80: accessible ✓ (previous Step processed 76, unblocking it)
- nop=132 at dot 84: blocked ✓ (Step processed 80, re-blocking it)
- Read test (`lcdon_timing-GS`): Still passes ✓

---

## VRAM Write Blocking at Mode 3 Start (December 27, 2024) - IMPLEMENTED

### Problem
After fixing OAM writes, test failed at VRAM Write nop=132 ($84) - expected blocked, got accessible.

### Root Cause
Same loop-exit timing issue as OAM writes. Check at dot 84 doesn't fire for writes at dot 84.

### Solution
Added separate VRAM write blocking at **dot 83** (fires for writes at 84+):
```cpp
if (dot_counter == 83) {
    vram_write_blocked = true;
}
```

### Result
- **`lcdon_write_timing-GS`: PASSES** ✓
- **`lcdon_timing-GS`: PASSES** ✓
- **Test suite: 87/89** (up from 86/89)

## Summary of Write Timing Fixes

The key insight is that checks at dot N only fire if that dot is PROCESSED during the Step loop.
For writes that happen when `dot_counter == N`, the loop exited AT N without processing.

| Blocking Flag | Unblock At | Re-block At | Accessible Window |
|---------------|------------|-------------|-------------------|
| `oam_write_blocked` | dot 76 | dot 80 | writes at 77-80 |
| `vram_write_blocked` | (N/A) | dot 83 | blocked from 84+ |


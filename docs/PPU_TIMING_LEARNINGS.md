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


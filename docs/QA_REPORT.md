# GB-EMU3 QA Report - December 2024

## Test Results Summary

| Test Suite | Result |
|------------|--------|
| Blargg cpu_instrs | **11/11 PASSED** ✅ |
| Blargg instr_timing | **PASSED** ✅ |
| Blargg halt_bug | **PASSED** ✅ (visual only) |

---

## Hardware Accuracy Issues

### Priority 1 - Critical

#### 1. PPU - Missing Pixel FIFO (`PPU.cpp:181`)
```cpp
// TODO: Implement pixel FIFO fetcher
```
**Impact:** Mid-scanline effects, variable mode 3 timing, and some games won't work correctly.

**Hardware Behavior:**
- 2 FIFOs (background + sprite)
- 8-step fetch cycle: tile no, tile data low, tile data high, push
- Mode 3 length varies based on sprites and scroll

**Fix Complexity:** High (major rewrite of PPU core)

---

#### 2. PPU - No Sprite Rendering
**Impact:** Games with sprites won't display characters, enemies, or UI elements.

**Hardware Behavior:**
- OAM scan finds up to 10 sprites per line
- Sprite pixels override BG based on priority
- OBP0/OBP1 palettes, X-flip, Y-flip

**Fix Complexity:** Medium

---

#### 3. PPU - No Window Layer
**Impact:** RPG status bars, maps, and overlays won't render.

**Hardware Behavior:**
- WX/WY registers control window position
- Window resets its internal line counter independently
- Window can be disabled mid-scanline

**Fix Complexity:** Medium

---

### Priority 2 - Important

#### 4. STOP Instruction (`Instructions.cpp:916`)
```cpp
// STOP - 4 T-cycles (TODO: proper STOP behavior)
```
**Current:** Just reads next byte (2-byte instruction format)

**Hardware Behavior:**
- Halts CPU and LCD (DMG) or blanks screen (CGB)
- Used for CGB speed switch
- Exits on button press or interrupt

**Fix Complexity:** Low

---

#### 5. Interrupt Dispatch Timing Accuracy
**Current:** 20 T-cycles (5 M-cycles) - FIXED ✅

Already fixed in this session with proper 5 M-cycle timing:
- 2 internal M-cycles (wait states)
- 2 M-cycles for stack push
- 1 M-cycle for PC update

---

#### 6. EI Delay Accuracy
**Current:** IME scheduled, enabled at start of next instruction - VERIFIED ✅

Per documentation: EI delays IME enable until after the next instruction's M1 cycle.

---

### Priority 3 - Minor

#### 7. DMA Bus Conflicts Not Implemented
**Current:** DMA runs independently, no bus blocking.

**Hardware Behavior:**
- During OAM DMA, CPU can only access HRAM
- DMA takes priority on external bus

**Impact:** Minor - most games work around this.

---

#### 8. Unused I/O Register Behavior
**Current:** Returns 0xFF for unmapped addresses.

**Hardware Behavior:**
- Some registers have specific unused bit patterns
- FEA0-FEFF has model-specific behavior

**Impact:** Very minor - rarely matters.

---

## Code Quality Findings

### TODOs Found: 2
1. `Instructions.cpp:916` - STOP instruction behavior
2. `PPU.cpp:181` - Pixel FIFO fetcher

### FIXMEs Found: 0 ✅
### HACKs Found: 0 ✅
### Stubs Found: 0 ✅

---

## Implemented & Verified Features

### CPU
- ✅ All 256 base opcodes + 256 CB opcodes
- ✅ T-cycle accurate timing
- ✅ Per-M-cycle tick callback for components
- ✅ InternalDelay for opcodes with internal cycles
- ✅ HALT behavior with IME=0 wake
- ✅ HALT bug (PC fails to increment)
- ✅ EI delay (after next instruction)
- ✅ DI cancels pending EI
- ✅ RETI enables IME immediately
- ✅ Interrupt dispatch: 20 T-cycles (5 M-cycles)

### Timer
- ✅ 16-bit DIV counter, upper 8 bits exposed
- ✅ TIMA falling edge detection on DIV bits
- ✅ TMA reload delay (4 T-cycles)
- ✅ DIV write causes TIMA glitch if selected bit is 1
- ✅ TAC write causes TIMA glitch on falling edge
- ✅ DIV bit 4 falling edge for APU (512 Hz)

### APU
- ✅ 4 sound channels
- ✅ Frame sequencer (512 Hz from DIV)
- ✅ Length counter, envelope, sweep
- ✅ Wave RAM with proper access

### Serial
- ✅ Internal clock mode (8192 baud)
- ✅ Shift register with 8-bit transfer
- ✅ Interrupt on transfer complete

### Joypad
- ✅ Matrix scanning (P14/P15 select)
- ✅ Interrupt on button press

### DMA
- ✅ OAM DMA (160 bytes, 4T per byte)
- ✅ Source address decoding

### MBC
- ✅ No MBC (32KB ROM)
- ✅ MBC1 (large ROM + RAM banking modes)
- ✅ MBC2 (built-in RAM)
- ✅ MBC3 (RTC support)
- ✅ MBC5 (9-bit ROM bank, 4-bit RAM bank)
- ✅ Battery-backed saves

---

## Recommended Fix Order

1. **STOP instruction** (Low effort, completes CPU)
2. **Sprite rendering** (Medium effort, major visual improvement)
3. **Window layer** (Medium effort, many games need it)
4. **Pixel FIFO** (High effort, advanced accuracy)

---

## Files Requiring Changes

| File | Change Needed |
|------|---------------|
| `Instructions.cpp` | STOP instruction |
| `PPU.cpp` | Sprites, Window, FIFO |
| `PPU.hpp` | Sprite buffer, Window state |

---

## Reference Documentation Used

- `docs/cycle_accurate.txt` - AntonioND's Cycle-Accurate GB Docs
- `docs/gbctr.txt` - GBCTR technical reference
- gbdev.io - Pan Docs and opcode tables

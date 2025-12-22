# GB-EMU3 Technical Documentation

> A hardware-accurate DMG Game Boy emulator with T-cycle timing and state machine PPU.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              EMULATOR (LR35902 SoC)                         │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         CLOCK DISTRIBUTION                          │   │
│  │                      (4.194304 MHz Master Clock)                    │   │
│  └──────────┬──────────────────┬──────────────────┬───────────────────┘   │
│             │                  │                  │                        │
│             ▼                  ▼                  ▼                        │
│  ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐           │
│  │       CPU        │ │       PPU        │ │       APU        │           │
│  │     (SM83)       │ │  (State Machine) │ │  (4 Channels)    │           │
│  │                  │ │                  │ │                  │           │
│  │ • T-cycle timing │ │ • Pixel FIFO     │ │ • Frame Seq.     │           │
│  │ • Bus interface  │ │ • Mode states    │ │ • DIV clocked    │           │
│  │ • Interrupts     │ │ • OAM scan       │ │ • Stereo output  │           │
│  └────────┬─────────┘ └────────┬─────────┘ └────────┬─────────┘           │
│           │                    │                    │                      │
│           │              ┌─────┴─────┐              │                      │
│           │              │   VRAM    │              │                      │
│           │              │   8KB     │              │                      │
│           │              └───────────┘              │                      │
│           ▼                                         ▼                      │
│  ┌──────────────────────────────────────────────────────────────────┐     │
│  │                           MEMORY BUS                              │     │
│  │                       (Address Decoder)                           │     │
│  │  ┌─────────────────────────────────────────────────────────────┐ │     │
│  │  │ $0000-$7FFF  ROM    │ $A000-$BFFF  External RAM             │ │     │
│  │  │ $8000-$9FFF  VRAM   │ $C000-$DFFF  Work RAM                 │ │     │
│  │  │ $FE00-$FE9F  OAM    │ $FF00-$FF7F  I/O Registers            │ │     │
│  │  │ $FF80-$FFFE  HRAM   │ $FFFF        Interrupt Enable         │ │     │
│  │  └─────────────────────────────────────────────────────────────┘ │     │
│  └──────────────────────────────────────────────────────────────────┘     │
│           │                    │                    │                      │
│           ▼                    ▼                    ▼                      │
│  ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐           │
│  │      Timer       │ │     Joypad       │ │     Serial       │           │
│  │                  │ │                  │ │                  │           │
│  │ • DIV counter    │ │ • 8 buttons      │ │ • Link cable     │           │
│  │ • TIMA/TMA/TAC   │ │ • Matrix scan    │ │ • 8192 baud      │           │
│  │ • 512Hz → APU    │ │ • IRQ on press   │ │ • Master/Slave   │           │
│  └──────────────────┘ └──────────────────┘ └──────────────────┘           │
│                                                                             │
└──────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                              CARTRIDGE (External)                            │
│                                                                              │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                      Memory Bank Controller (MBC)                      │  │
│  │                                                                        │  │
│  │   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐               │  │
│  │   │  ROM Banks  │    │  RAM Banks  │    │  RTC (MBC3) │               │  │
│  │   │  Up to 8MB  │    │  Up to 128K │    │  Clock/Alarm│               │  │
│  │   └─────────────┘    └─────────────┘    └─────────────┘               │  │
│  │                                                                        │  │
│  │   Supported: No MBC, MBC1, MBC2, MBC3, MBC5                           │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Component Interconnection

Each hardware component is **decoupled** - components don't know about each other, just like real chips on a PCB. They communicate only through the Bus:

```
                     Interrupt Signals (directly exposed wires)
            ┌──────────────────────────────────────────────────────┐
            │                                                      │
            │    ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐           │
            │    │ PPU  │  │Timer │  │Serial│  │Joypad│           │
            │    └──┬───┘  └──┬───┘  └──┬───┘  └──┬───┘           │
            │       │VBlank   │TIMER    │SERIAL   │JOYPAD         │
            │       │STAT     │         │         │               │
            │       ▼         ▼         ▼         ▼               │
            │    ┌─────────────────────────────────────┐          │
            │    │        Interrupt Controller         │          │
            │    │         (IF / IE registers)         │          │
            │    └──────────────────┬──────────────────┘          │
            │                       │                              │
            │                       ▼                              │
            │              ┌────────────────┐                      │
            └──────────────│      CPU       │◄─────────────────────┘
                           └────────────────┘
```

---

## ROM Loading Flow

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Load File     │────▶│  Parse Header   │────▶│  Detect MBC     │
│                 │     │                 │     │                 │
│ • File dialog   │     │ • Title         │     │ • No MBC        │
│ • Drag & drop   │     │ • Type code     │     │ • MBC1          │
│ • Command line  │     │ • ROM size      │     │ • MBC2          │
│ • Recent files  │     │ • RAM size      │     │ • MBC3 + RTC    │
└─────────────────┘     │ • Checksum      │     │ • MBC5          │
                        └─────────────────┘     └─────────────────┘
                                │                       │
                                ▼                       ▼
                        ┌─────────────────┐     ┌─────────────────┐
                        │  Validate ROM   │     │ Initialize MBC  │
                        │                 │     │                 │
                        │ • Size check    │     │ • Bank regs     │
                        │ • Header valid  │     │ • RAM init      │
                        │ • Logo check    │     │ • Load save     │
                        └─────────────────┘     └─────────────────┘
```

---

## File Structure

```
gb-emu3/
├── CMakeLists.txt
├── run.sh                    # Auto-rebuild + boot ROM detection
├── docs/
│   ├── TECHNICAL.md          ◄── You are here
│   └── QA_REPORT.md          # Hardware accuracy assessment
├── bootroms/
│   └── dmg_boot.bin          # DMG boot ROM (user-provided)
├── src/
│   ├── main.cpp              # Entry point, argument parsing
│   ├── Emulator.hpp/cpp      # SoC orchestrator
│   │
│   ├── cpu/
│   │   ├── CPU.hpp/cpp       # SM83 core, bus connection
│   │   ├── Instructions.hpp  # Opcode function declarations
│   │   ├── Instructions.cpp  # All 256 main opcodes (~900 lines)
│   │   ├── InstructionsCB.cpp # CB prefix + dispatch (~745 lines)
│   │   └── InterruptController.hpp/cpp
│   │
│   ├── ppu/
│   │   └── PPU.hpp/cpp       # State machine PPU
│   │
│   ├── apu/
│   │   └── APU.hpp/cpp       # 4 channels + frame sequencer
│   │
│   ├── timer/
│   │   └── Timer.hpp/cpp     # Hardware-accurate DIV/TIMA
│   │
│   ├── input/
│   │   └── Joypad.hpp/cpp    # Matrix scanning
│   │
│   ├── serial/
│   │   └── Serial.hpp/cpp    # Link cable stub
│   │
│   ├── memory/
│   │   ├── Bus.hpp/cpp       # Address decoder
│   │   ├── Memory.hpp/cpp    # WRAM/HRAM
│   │   ├── DMA.hpp/cpp       # OAM DMA transfers
│   │   └── BootROM.hpp/cpp   # 256-byte boot ROM overlay
│   │
│   ├── cartridge/
│   │   └── Cartridge.hpp/cpp # MBC1/2/3/5 + battery saves
│   │
│   └── frontend/
│       └── Window.hpp/cpp    # SDL2 rendering + file dialog
│
└── test_roms/                # Test ROMs (gitignored)
```

---

## Implementation Status

| Component | Header | Implementation | Accuracy |
|-----------|--------|----------------|----------|
| CPU (SM83) | ✅ | ✅ Complete | T-cycle accurate, 20T interrupt dispatch |
| PPU | ✅ | ✅ Complete | DMG ACID2 passing, pixel FIFO, X-priority |
| APU | ✅ | ✅ Complete | Frame sequencer, DIV-clocked |
| Timer | ✅ | ✅ Complete | Hardware accurate, obscure behavior |
| Joypad | ✅ | ✅ Complete | Matrix scan, interrupt, opposing key prevention |
| Serial | ✅ | ✅ Complete | Internal clock |
| Bus | ✅ | ✅ Complete | Full routing |
| Memory | ✅ | ✅ Complete | WRAM/HRAM |
| DMA | ✅ | ✅ Complete | Per-cycle |
| Cartridge | ✅ | ✅ Complete | MBC1-5, battery saves, dirty tracking |
| Boot ROM | ✅ | ✅ Complete | Overlay |
| Frontend | ✅ | ✅ Complete | SDL2, async file dialog |

---

## CPU Instruction Timing

T-cycle timing follows hardware behavior where each memory access = 4 T-cycles:

| Instruction Type | Cycles | Memory Operations |
|-----------------|--------|-------------------|
| `NOP` | 4 | 1 fetch |
| `LD r,r'` | 4 | 1 fetch |
| `LD r,n` | 8 | 2 fetches |
| `LD (HL),r` | 8 | 1 fetch + 1 write |
| `LD (HL),n` | 12 | 2 fetches + 1 write |
| `JP nn` | 16 | 3 fetches + internal |
| `JR cc,n` (taken) | 12 | 2 fetches + internal |
| `JR cc,n` (not taken) | 8 | 2 fetches |
| `CALL nn` | 24 | 3 fetches + 2 writes |
| `RET` | 16 | 1 fetch + 2 reads + internal |
| `CB prefix` | +4 | +1 fetch |

---

## Timer Hardware Behavior

```
DIV Counter (16-bit internal, upper 8 bits exposed):
┌─────────────────────────────────────────────────────────────┐
│ Bit 15 14 13 12 11 10  9  8 │  7  6  5  4  3  2  1  0     │
│                             │  ◄── DIV register ($FF04)    │
│                             │                               │
│         bit 9 ──────────────┼──► TAC=00 (4096 Hz)          │
│         bit 3 ──────────────┼──► TAC=01 (262144 Hz)        │
│         bit 5 ──────────────┼──► TAC=10 (65536 Hz)         │
│         bit 7 ──────────────┼──► TAC=11 (16384 Hz)         │
│                             │                               │
│         bit 4 ──────────────┼──► APU Frame Sequencer       │
└─────────────────────────────────────────────────────────────┘

TIMA Overflow:
1. TIMA overflows → set pending flag
2. Wait 4 T-cycles
3. TIMA = TMA, request interrupt
```

---

## APU Hardware Accuracy

The APU implementation follows SameBoy's hardware-verified behavior:

### Frame Sequencer (512 Hz)

| Feature | Status | SameBoy Reference |
|---------|--------|-------------------|
| DIV bit 12 falling edge clocking | ✅ | Lines 295-302 |
| Power-on frame sequencer reset | ✅ | Lines 1310-1315 |
| Skip first DIV event if bit already high | ✅ | Lines 1283-1287 |

### Channel 1 Sweep

| Feature | Status | SameBoy Reference |
|---------|--------|-------------------|
| Overflow check on trigger | ✅ | Lines 1466-1481 |
| Second overflow check after frequency update | ✅ | Lines 790-815 |
| Negate mode lockout (disabling negate kills channel) | ✅ | Lines 1377-1388 |
| Sweep timer reload uses 8 when period is 0 | ✅ | Line 1464 |

### Trigger Behavior

| Feature | Status | SameBoy Reference |
|---------|--------|-------------------|
| DAC must be enabled to enable channel | ✅ | Lines 1442-1447 |
| Length counter reloads to max only if 0 | ✅ | Lines 1448-1459 |
| Length enable set to false on reload (un-freeze) | ✅ | Line 1459 |
| Wave trigger +3 cycle delay | ✅ | Line 1586 |

### Wave Channel (CH3)

| Feature | Status | SameBoy Reference |
|---------|--------|-------------------|
| DMG wave RAM corruption on re-trigger | ✅ | Lines 1550-1574 |
| wave_form_just_read 1-cycle access window | ⏳ | Lines 910-929 |
| Read returns current sample byte in window | ⏳ | Lines 1051-1058 |

### DMG Length Counter Survival

| Feature | Status | SameBoy Reference |
|---------|--------|-------------------|
| Length counters preserved on APU power cycle | ✅ | Lines 1291-1316 |
| NRx1 writes allowed when APU powered off | ✅ | Lines 1257-1266 |

### Blargg dmg_sound Results

| Test | Status |
|------|--------|
| 01-registers | ✅ |
| 02-len ctr | ✅ |
| 03-trigger | ✅ |
| 04-sweep | ✅ |
| 05-sweep details | ✅ |
| 06-overflow on trigger | ✅ |
| 07-len sweep period sync | ✅ |
| 08-len ctr during power | ✅ |
| 09-wave read while on | ⏳ |
| 10-wave trigger while on | ⏳ |
| 11-regs after power | ✅ |
| 12-wave write while on | ⏳ |

**9/12 tests passing** - Wave channel timing (09, 10, 12) requires sub-M-cycle precision.

---

## Test Results

### Blargg cpu_instrs - 11/11 PASSED ✅

All CPU instruction tests pass, confirming correct opcode implementation, flag handling, and timing.

### Blargg instr_timing - PASSED ✅

All instruction timings verified to T-cycle accuracy.

### Blargg halt_bug - PASSED ✅

HALT bug correctly implemented (PC fails to increment when IME=0 with pending interrupt).

---

## Mooneye Test Results

**Current Status: 85/89 passing**

| Category | Passed | Total |
|----------|--------|-------|
| MBC1 | 13 | 13 ✅ |
| MBC2 | 7 | 7 ✅ |
| MBC5 | 8 | 8 ✅ |
| Timer | 13 | 13 ✅ |
| Bits | 3 | 3 ✅ |
| Halt | 4 | 4 ✅ |
| EI/DI | 4 | 4 ✅ |
| Call/Ret Timing | 8 | 8 ✅ |
| PPU | 8 | 12 |
| OAM DMA | 5 | 5 ✅ |
| Interrupts | 2 | 2 ✅ |

---

## Known Limitations

### PPU - Pixel FIFO Renderer (DMG ACID2 Passing)

The PPU uses a hardware-accurate pixel FIFO renderer:

| Feature | Status |
|---------|--------|
| Background tiles | ✅ Works |
| Sprites (8x8, 8x16) | ✅ Works |
| Sprite X-priority | ✅ Per SameBoy (insertion sort) |
| Sprite X-flip | ✅ XOR-based flip per SameBoy |
| Window layer | ✅ WX/WY trigger correct |
| Mode state machine | ✅ Correct timing |
| OAM scan | ✅ 10 sprite limit |
| Sprite FIFO overlay | ✅ 8 transparent slots + overlay |
| Fine SCX scrolling | ✅ Works |

#### Mode 3 Transition Timing

Mode 3 timing follows SameBoy's display.c implementation:

| Event | Dot Counter | SameBoy Reference |
|-------|-------------|-------------------|
| `mode_for_interrupt = 3` | dot 79 | Line 1829 |
| STAT read mode = 3 | dot 83+ | Line 1828 |
| `mode = PIXEL_TRANSFER` | dot 84 | Line 1845 (mode_3_start) |
| First pixel work | dot 85 | After mode transition |
| Mode 0 STAT/interrupt | dot 252 | After 167-cycle Mode 3 |

### Memory Access Timing

| Feature | Status |
|---------|--------|
| OAM blocked in Mode 2/3 | ✅ Per Pan Docs |
| VRAM blocked in Mode 3 | ✅ Per Pan Docs |
| OAM/VRAM accessible when LCD off | ✅ Per Pan Docs |
| OAM DMA bus conflicts | ✅ Per SameBoy |

### I/O Register Accuracy

| Register | Unused Bits | Status |
|----------|-------------|--------|
| IF ($FF0F) | Bits 7-5 read as 1 | ✅ Per Pan Docs |
| IE ($FFFF) | All 8 bits R/W | ✅ Per SameBoy |
| STAT ($FF41) | Bit 7 reads as 1 | ✅ Per Pan Docs |
| P1 ($FF00) | Bits 7-6 read as 1 | ✅ Per Pan Docs |

### STOP Instruction

Basic implementation - doesn't enter low-power mode.

### Serial

Works for internal clock mode only (Blargg tests). External clock mode untested.

---

## Code Quality

| Metric | Value |
|--------|-------|
| TODOs | 1 (STOP mode) |
| HACKs | 0 |
| FIXMEs | 0 |
| Stubs | 0 |

**The codebase is clean with no hack workarounds.**

---

## Development Roadmap

### Phase 1: CPU Completion ✅
- [x] All 512 opcodes with correct timing
- [x] Interrupt dispatch (20 T-cycles)
- [x] HALT bug implementation
- [x] EI delay / DI cancellation
- [ ] STOP low-power mode

### Phase 2: PPU Enhancement ✅
- [x] Sprite rendering (OAM scan + pixel mixing)
- [x] Window layer with correct WX/WY trigger
- [x] Pixel FIFO with sprite overlay
- [x] DMG sprite X-priority (lower X wins)
- [x] DMG ACID2 visual test passing

### Phase 3: Advanced Features
- [x] DMA bus conflicts
- [ ] CGB support (double speed, VRAM banking)
- [ ] HDMA (HBlank DMA)
- [ ] SGB support

---

## Reference Documents

| Document | Location |
|----------|----------|
| Cycle-Accurate GB Docs | `docs/cycle_accurate.txt` |
| GBCTR Reference | `docs/gbctr.txt` |
| QA Report | `docs/QA_REPORT.md` |

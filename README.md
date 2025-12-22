# GB-EMU3

**A hardware-accurate DMG Game Boy emulator written in C++**

> **Philosophy: 100% Hardware Accuracy**  
> No hacks. No "close enough." No "acceptable." Every behavior matches real hardware.

---

## Hardware Accuracy

This emulator aims to match real DMG hardware behavior exactly:

- **T-cycle accurate CPU** - Every instruction timed to the exact cycle
- **Hardware AND gates** for bank masking - Not modulo, actual bitmask emulation
- **MBC implementations verified against real hardware** via Mooneye test suite
- **All timing matches Pan Docs specifications**

### Test Results

| Test Suite | Status |
|------------|--------|
| **Blargg cpu_instrs** | 11/11 ✅ |
| **Blargg instr_timing** | PASSED ✅ |
| **DMG ACID2** | PASSED ✅ |
| **MBC1** | 13/13 ✅ |
| **MBC2** | 7/7 ✅ |
| **MBC5** | 8/8 ✅ |
| **Timer** | 13/13 ✅ |
| **Bits** | 3/3 ✅ |
| **Halt** | 4/4 ✅ |
| **EI/DI** | 4/4 ✅ |
| **Call/Ret Timing** | 8/8 ✅ |
| **Total Mooneye** | **31/35** (selected) |

---

## Features

- **Memory Bank Controllers**: MBC1, MBC1M (multicart), MBC2, MBC3 (with RTC), MBC5
- **Audio**: 4-channel APU with accurate mixing, audio-driven 59.73 Hz timing
- **Input**: Keyboard and gamepad support
- **Save States**: Battery-backed save support
- **Boot ROM**: Optional DMG boot ROM support

---

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Dependencies

- SDL2
- C++17 compiler

---

## Running

```bash
# With boot ROM (hardware accurate initialization)
./gb-emu3 --boot-rom bootroms/dmg_boot.bin game.gb

# Without boot ROM (skip to game)
./gb-emu3 game.gb

# Headless mode for testing
./gb-emu3 --headless --cycles 50000000 test.gb
```

---

## Running Tests

```bash
# Run all Mooneye tests
./mooneye_runner.sh --all

# Run specific categories
./mooneye_runner.sh --mbc1
./mooneye_runner.sh --mbc2
./mooneye_runner.sh --timer
```

> **Note**: Test ROMs must be obtained separately. Place them in `test_roms/`.

---

## Architecture

```
src/
├── cpu/        # SM83 CPU with T-cycle timing
├── ppu/        # Pixel FIFO renderer (DMG ACID2 passing)
├── apu/        # Audio Processing Unit (4 channels)
├── memory/     # Bus, DMA, and memory map
├── cartridge/  # MBC implementations
├── timer/      # DIV/TIMA hardware timer
├── input/      # Joypad controller
└── serial/     # Link cable (stub)
```

---

## Technical Reference

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for detailed implementation notes.

---

## Why Hardware Accuracy Matters

> "If it passes the tests but isn't hardware accurate, it's wrong."

Many emulators take shortcuts that work for most games but fail edge cases. This emulator:

1. **Matches hardware behavior** - Not just game compatibility
2. **Uses correct formulas** - `bank & mask` not `bank % size`
3. **Passes rigorous test suites** - Mooneye, Blargg
4. **Documents every decision** - With Pan Docs references

---

## License

GPLv3 - See [LICENSE](LICENSE) for details.

---

## Acknowledgments

- [Pan Docs](https://gbdev.io/pandocs/) - Definitive Game Boy hardware reference
- [SameBoy](https://sameboy.github.io/) - Reference implementation
- [Mooneye Test Suite](https://github.com/Gekkio/mooneye-test-suite) - Hardware verification
- [Blargg's Test ROMs](https://github.com/retrio/gb-test-roms) - CPU/APU verification

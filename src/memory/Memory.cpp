#include "Memory.hpp"

Memory::Memory() {
    Reset();
}

void Memory::Reset() {
    wram.fill(0);
    hram.fill(0);
}

uint8_t Memory::ReadWRAM(uint16_t addr) const {
    return wram[addr & 0x1FFF];  // 8KB, mask to valid range
}

void Memory::WriteWRAM(uint16_t addr, uint8_t value) {
    wram[addr & 0x1FFF] = value;
}

uint8_t Memory::ReadHRAM(uint16_t addr) const {
    return hram[addr - 0xFF80];
}

void Memory::WriteHRAM(uint16_t addr, uint8_t value) {
    hram[addr - 0xFF80] = value;
}

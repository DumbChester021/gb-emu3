#pragma once

#include <cstdint>
#include <array>

/**
 * Memory - RAM Regions (WRAM, HRAM)
 * 
 * Hardware Behavior:
 * - Simple SRAM chips that store and retrieve data
 * - No logic, just storage
 * - Does NOT know about CPU or any other component
 * 
 * Interface:
 * - Address input
 * - Data input/output
 * - Read/Write control
 */
class Memory {
public:
    Memory();
    
    void Reset();
    
    // === Work RAM Interface (directly exposed $C000-$DFFF) ===
    uint8_t ReadWRAM(uint16_t addr) const;
    void WriteWRAM(uint16_t addr, uint8_t value);
    
    // === High RAM Interface (directly exposed $FF80-$FFFE) ===
    uint8_t ReadHRAM(uint16_t addr) const;
    void WriteHRAM(uint16_t addr, uint8_t value);
    
private:
    // === RAM Storage (directly exposed internal storage) ===
    std::array<uint8_t, 8192> wram;     // 8KB Work RAM
    std::array<uint8_t, 127> hram;      // 127 bytes High RAM
};

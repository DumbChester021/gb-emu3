#include "Bus.hpp"

// Bus types for conflict detection (per SameBoy)
enum class BusType { EXTERNAL, VRAM, INTERNAL };

static BusType GetBusForAddress(uint16_t addr) {
    if (addr < 0x8000) return BusType::EXTERNAL;       // ROM
    if (addr < 0xA000) return BusType::VRAM;           // VRAM
    if (addr < 0xFE00) return BusType::EXTERNAL;       // External RAM, WRAM, Echo
    return BusType::INTERNAL;                          // OAM, IO, HRAM, IE
}

Bus::Bus()
    : bootrom_enabled(false)
{
    Reset();
}

void Bus::Reset() {
    bootrom_enabled = false;
}

uint8_t Bus::Read(uint16_t addr) {
    // === OAM DMA Bus Conflict Detection ===
    // During DMA, CPU reads from the same bus as DMA source return $FF
    // Only HRAM/IO (internal bus) are accessible during DMA
    if (dma_active && dma_active() && dma_source && addr < 0xFE00) {
        uint16_t source_addr = dma_source();
        // Check if both addresses are on the same bus type
        BusType cpu_bus = GetBusForAddress(addr);
        BusType dma_bus = GetBusForAddress(source_addr);
        
        if (cpu_bus == dma_bus) {
            return OPEN_BUS;  // Bus conflict - return $FF
        }
    }
    
    // Boot ROM overlay ($0000-$00FF)
    if (bootrom_enabled && addr < 0x0100 && bootrom_read) {
        return bootrom_read(addr);
    }
    
    // Cartridge ROM ($0000-$7FFF)
    if (addr < 0x8000) {
        if (cart_read) return cart_read(addr);
        return OPEN_BUS;
    }
    
    // VRAM ($8000-$9FFF)
    if (addr < 0xA000) {
        if (vram_read) return vram_read(addr);
        return OPEN_BUS;
    }
    
    // External RAM ($A000-$BFFF)
    if (addr < 0xC000) {
        if (cart_read) return cart_read(addr);
        return OPEN_BUS;
    }
    
    // Work RAM ($C000-$DFFF)
    if (addr < 0xE000) {
        if (wram_read) return wram_read(addr);
        return OPEN_BUS;
    }
    
    // Echo RAM ($E000-$FDFF) - mirror of WRAM
    if (addr < 0xFE00) {
        if (wram_read) return wram_read(addr - 0x2000);
        return OPEN_BUS;
    }
    
    // OAM ($FE00-$FE9F)
    if (addr < 0xFEA0) {
        // During OAM DMA, CPU reads from OAM return $FF
        if (dma_blocking_oam && dma_blocking_oam()) {
            return OPEN_BUS;
        }
        if (oam_read) return oam_read(addr);
        return OPEN_BUS;
    }
    
    // Unusable ($FEA0-$FEFF)
    if (addr < 0xFF00) {
        return OPEN_BUS;
    }
    
    // I/O Registers ($FF00-$FF7F)
    if (addr < 0xFF80) {
        if (io_read) return io_read(addr);
        return OPEN_BUS;
    }
    
    // HRAM ($FF80-$FFFE)
    if (addr < 0xFFFF) {
        if (hram_read) return hram_read(addr);
        return OPEN_BUS;
    }
    
    // IE Register ($FFFF)
    if (ie_read) return ie_read(addr);
    return OPEN_BUS;
}

void Bus::Write(uint16_t addr, uint8_t value) {
    // Cartridge ROM ($0000-$7FFF) - MBC register writes
    if (addr < 0x8000) {
        if (cart_write) cart_write(addr, value);
        return;
    }
    
    // VRAM ($8000-$9FFF)
    if (addr < 0xA000) {
        if (vram_write) vram_write(addr, value);
        return;
    }
    
    // External RAM ($A000-$BFFF)
    if (addr < 0xC000) {
        if (cart_write) cart_write(addr, value);
        return;
    }
    
    // Work RAM ($C000-$DFFF)
    if (addr < 0xE000) {
        if (wram_write) wram_write(addr, value);
        return;
    }
    
    // Echo RAM ($E000-$FDFF) - mirror of WRAM
    if (addr < 0xFE00) {
        if (wram_write) wram_write(addr - 0x2000, value);
        return;
    }
    
    // OAM ($FE00-$FE9F)
    if (addr < 0xFEA0) {
        // During OAM DMA, CPU writes to OAM are blocked/ignored
        if (dma_blocking_oam && dma_blocking_oam()) {
            return;  // Write blocked - value is lost
        }
        if (oam_write) oam_write(addr, value);
        return;
    }
    
    // Unusable ($FEA0-$FEFF)
    if (addr < 0xFF00) {
        return;
    }
    
    // I/O Registers ($FF00-$FF7F)
    if (addr < 0xFF80) {
        if (io_write) io_write(addr, value);
        return;
    }
    
    // HRAM ($FF80-$FFFE)
    if (addr < 0xFFFF) {
        if (hram_write) hram_write(addr, value);
        return;
    }
    
    // IE Register ($FFFF)
    if (ie_write) ie_write(addr, value);
}

uint8_t Bus::DMARead(uint16_t addr) {
    // DMA bypasses some restrictions
    if (addr < 0x8000) {
        if (cart_read) return cart_read(addr);
    } else if (addr < 0xA000) {
        if (vram_read) return vram_read(addr);
    } else if (addr < 0xC000) {
        if (cart_read) return cart_read(addr);
    } else if (addr < 0xE000) {
        if (wram_read) return wram_read(addr);
    } else {
        // Per SameBoy memory.c line 1881:
        // DMG: $E000-$FFFF mirrors to WRAM via (addr & ~0x2000)
        // This maps: $E000->$C000, $FE00->$DE00, $FF00->$DF00
        if (wram_read) return wram_read(addr & ~0x2000);
    }
    
    return OPEN_BUS;
}


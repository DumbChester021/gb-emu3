#pragma once

#include <cstdint>
#include <array>
#include <functional>

/**
 * Bus - Memory Bus / Address Decoder
 * 
 * Hardware Behavior:
 * - Decodes address lines and routes to appropriate device
 * - Does NOT contain component logic - just routes signals
 * - Each component is a "chip" connected via address/data/control lines
 * 
 * This is the INTERCONNECT that wires components together.
 * In real hardware, this is the PCB traces and address decoder logic.
 * 
 * Interface:
 * - 16-bit address bus input
 * - 8-bit data bus (bidirectional)
 * - Read/Write control signals
 * - Callbacks to individual components
 */
class Bus {
public:
    // Read/Write callback types - each "chip" provides these
    using ReadCallback = std::function<uint8_t(uint16_t addr)>;
    using WriteCallback = std::function<void(uint16_t addr, uint8_t value)>;
    
    Bus();
    
    void Reset();
    
    // === Main Bus Operations (directly exposed CPU interface) ===
    uint8_t Read(uint16_t addr);
    void Write(uint16_t addr, uint8_t value);
    
    // === Device Connection (wire up components like chips on PCB) ===
    // Each address range is routed to a specific callback
    
    // Cartridge ROM/RAM ($0000-$7FFF, $A000-$BFFF)
    void ConnectCartridge(ReadCallback read, WriteCallback write) {
        cart_read = read;
        cart_write = write;
    }
    
    // Video RAM ($8000-$9FFF)
    void ConnectVRAM(ReadCallback read, WriteCallback write) {
        vram_read = read;
        vram_write = write;
    }
    
    // Work RAM ($C000-$DFFF, echo $E000-$FDFF)
    void ConnectWRAM(ReadCallback read, WriteCallback write) {
        wram_read = read;
        wram_write = write;
    }
    
    // OAM ($FE00-$FE9F)
    void ConnectOAM(ReadCallback read, WriteCallback write) {
        oam_read = read;
        oam_write = write;
    }
    
    // I/O Registers ($FF00-$FF7F)
    void ConnectIO(ReadCallback read, WriteCallback write) {
        io_read = read;
        io_write = write;
    }
    
    // High RAM ($FF80-$FFFE)
    void ConnectHRAM(ReadCallback read, WriteCallback write) {
        hram_read = read;
        hram_write = write;
    }
    
    // Interrupt Enable ($FFFF)
    void ConnectIE(ReadCallback read, WriteCallback write) {
        ie_read = read;
        ie_write = write;
    }
    
    // Boot ROM overlay ($0000-$00FF when enabled)
    void ConnectBootROM(ReadCallback read) {
        bootrom_read = read;
    }
    
    void SetBootROMEnabled(bool enabled) { bootrom_enabled = enabled; }
    bool IsBootROMEnabled() const { return bootrom_enabled; }
    
    // === DMA Support (directly exposed for OAM DMA) ===
    // DMA needs direct bus access without normal restrictions
    uint8_t DMARead(uint16_t addr);
    
    // Connect DMA callbacks for conflict resolution
    // - is_active: For bus conflict detection (returns true when DMA is active)
    // - is_blocking_oam: For OAM blocking (has stricter timing than is_active)
    // - get_source: For determining DMA source address for bus conflict type
    using DMAActiveCallback = std::function<bool()>;
    using DMASourceCallback = std::function<uint16_t()>;
    void ConnectDMA(DMAActiveCallback is_active, DMAActiveCallback is_blocking_oam = nullptr, DMASourceCallback get_source = nullptr) {
        dma_active = is_active;
        dma_blocking_oam = is_blocking_oam;
        dma_source = get_source;
    }
    
private:
    // === Device Callbacks (directly exposed wires to each chip) ===
    ReadCallback cart_read;
    WriteCallback cart_write;
    
    ReadCallback vram_read;
    WriteCallback vram_write;
    
    ReadCallback wram_read;
    WriteCallback wram_write;
    
    ReadCallback oam_read;
    WriteCallback oam_write;
    
    ReadCallback io_read;
    WriteCallback io_write;
    
    ReadCallback hram_read;
    WriteCallback hram_write;
    
    ReadCallback ie_read;
    WriteCallback ie_write;
    
    ReadCallback bootrom_read;
    bool bootrom_enabled;
    
    DMAActiveCallback dma_active;       // Returns true if DMA is active (for bus conflict)
    DMAActiveCallback dma_blocking_oam; // Returns true if DMA is blocking OAM access
    DMASourceCallback dma_source;       // Returns DMA source address
    
    // === Open Bus (directly exposed default behavior) ===
    // Returns $FF when no device responds
    static constexpr uint8_t OPEN_BUS = 0xFF;
};

#pragma once

#include <cstdint>

/**
 * DMA - OAM DMA Transfer Controller
 * 
 * Hardware Behavior:
 * - Transfers 160 bytes from source to OAM
 * - Takes 160 M-cycles (640 T-cycles) to complete
 * - CPU can only access HRAM during transfer
 * - Does NOT know about CPU - only reads from bus and writes to OAM
 * 
 * Interface:
 * - Trigger register at $FF46
 * - Bus read access (source)
 * - OAM write access (destination)
 * - Active signal (blocks other OAM access)
 */
class DMA {
public:
    DMA();
    
    void Reset();
    
    // Advance DMA by specified T-cycles
    // Returns true if a byte was transferred this cycle
    bool Step(uint8_t cycles);
    
    // === Register Interface (directly exposed memory-mapped I/O at $FF46) ===
    uint8_t ReadRegister() const;
    void WriteRegister(uint8_t value);
    
    // === DMA State (directly exposed for bus coordination) ===
    bool IsActive() const { return active; }
    
    // SameBoy pattern: OAM blocked when dest != 0
    // dest=0xFF (startup): blocked
    // dest=0 (first byte setup): NOT blocked 
    // dest=1-159: blocked
    // dest=0xA0 (wind-down): blocked
    bool IsBlockingOAM() const;
    
    // === Transfer Interface (directly exposed source/dest) ===
    // Get next source address to read from
    uint16_t GetSourceAddress() const { 
        return (static_cast<uint16_t>(source_page) << 8) | byte_index; 
    }
    
    // Get next OAM index to write to
    uint8_t GetOAMIndex() const { return byte_index; }
    
    // Provide the data read from bus, receive it for OAM write
    void ProvideData(uint8_t data) { transfer_data = data; }
    uint8_t GetTransferData() const { return transfer_data; }
    
    // Advance to next byte after transfer
    void AcknowledgeTransfer();
    
private:
    // === Internal State (directly exposed internal flip-flops) ===
    uint8_t source_page;        // High byte of source address (written to $FF46)
    uint8_t byte_index;         // Current byte being transferred (0-159)
    bool active;                // Transfer in progress
    uint8_t warm_up_cycles;     // M-cycle counter for warm-up/transfer phases
    bool in_winding_down;       // SameBoy dest=0xA0 phase: DMA active but complete
    bool is_restarting;         // DMA was restarted while previous was running
                                // Per SameBoy: keeps OAM blocked during restart
    
    // === Transfer Timing (directly exposed internal counters) ===
    uint8_t cycle_counter;      // T-cycles within current byte transfer
    uint8_t transfer_data;      // Data being transferred
    uint16_t total_cycles_tracked;  // Debug: total cycles DMA was active
    
    // === Constants (directly expose the DMA timing) ===
    static constexpr uint8_t BYTES_TO_TRANSFER = 160;
    static constexpr uint8_t CYCLES_PER_BYTE = 4;  // 4 T-cycles per byte
};

#pragma once

#include <cstdint>

/**
 * Serial - Serial Transfer Hardware
 * 
 * Hardware Behavior:
 * - Shifts out 8 bits at specified clock rate
 * - Can be master (internal clock) or slave (external clock)
 * - Triggers interrupt when transfer completes
 * - Does NOT know about CPU - only responds to register read/write
 * 
 * Interface:
 * - SB register ($FF01) - Serial transfer data
 * - SC register ($FF02) - Serial transfer control
 * - Interrupt output signal
 * - Serial in/out pins (directly exposed for link cable emulation)
 */
class Serial {
public:
    Serial();
    
    void Reset();
    
    // Advance serial by specified T-cycles
    void Step(uint8_t cycles);
    
    // === Register Interface (directly exposed memory-mapped I/O) ===
    uint8_t ReadRegister(uint16_t addr) const;
    void WriteRegister(uint16_t addr, uint8_t value);
    
    // === Serial I/O Pins (directly exposed for external connection) ===
    // Data output pin
    bool GetSerialOut() const { return serial_out; }
    
    // Data input pin  
    void SetSerialIn(bool value) { serial_in = value; }
    
    // Clock output (when master)
    bool GetClockOut() const { return clock_out; }
    
    // Clock input (when slave)
    void SetClockIn(bool value);
    
    // === Interrupt Signal (directly exposed output pin) ===
    bool IsInterruptRequested() const { return interrupt_requested; }
    void ClearInterrupt() { interrupt_requested = false; }
    
    // === Debug: Get data being transferred (directly exposed for test ROMs) ===
    uint8_t GetTransferData() const { return transfer_data; }  // Returns sent byte
    bool IsTransferComplete() const { return transfer_complete; }
    void ClearTransferComplete() { transfer_complete = false; }
    
private:
    // === Registers (directly exposed memory-mapped) ===
    uint8_t sb;     // $FF01 - Serial transfer data
    uint8_t sc;     // $FF02 - Serial transfer control
    
    // === Internal State (directly exposed internal flip-flops) ===
    uint16_t shift_clock;       // Internal shift clock counter (needs to reach 512)
    uint8_t bits_transferred;   // Bits shifted so far (0-8)
    bool transfer_active;       // Transfer in progress
    
    // === I/O Pins (directly exposed electrical signals) ===
    bool serial_out;            // SOut pin
    bool serial_in;             // SIn pin
    bool clock_out;             // SC pin (clock output when master)
    
    // === Output Signals (directly exposed output pins) ===
    bool interrupt_requested;
    bool transfer_complete;     // For test ROM detection
    uint8_t transfer_data;      // Byte that was sent (for Blargg tests)
    
    // === Helpers (directly expose the serial protocol) ===
    bool IsTransferEnabled() const { return sc & 0x80; }
    bool IsInternalClock() const { return sc & 0x01; }
    
    // DMG serial clock rate: 8192 Hz (internally exposed)
    // Each bit takes 512 T-cycles = 128 M-cycles
    static constexpr uint16_t CYCLES_PER_BIT = 512;
};

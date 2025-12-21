#pragma once

#include <cstdint>

/**
 * InterruptController - Interrupt Flag and Enable Registers
 * 
 * Hardware Behavior:
 * - IF register ($FF0F) - Interrupt Flags (pending interrupts)
 * - IE register ($FFFF) - Interrupt Enable (which interrupts are enabled)
 * - Each interrupt source sets its bit in IF
 * - CPU checks IF & IE to determine if interrupt should fire
 * 
 * This represents the interrupt latch/enable logic in the LR35902.
 * Does NOT contain CPU logic - just the flag storage.
 * 
 * Interrupt bits:
 * - Bit 0: VBlank (highest priority)
 * - Bit 1: LCD STAT
 * - Bit 2: Timer
 * - Bit 3: Serial
 * - Bit 4: Joypad (lowest priority)
 */
class InterruptController {
public:
    InterruptController();
    
    void Reset();
    
    // === Interrupt Bit Definitions (directly exposed constants) ===
    static constexpr uint8_t VBLANK  = 0x01;  // Bit 0
    static constexpr uint8_t STAT    = 0x02;  // Bit 1
    static constexpr uint8_t TIMER   = 0x04;  // Bit 2
    static constexpr uint8_t SERIAL  = 0x08;  // Bit 3
    static constexpr uint8_t JOYPAD  = 0x10;  // Bit 4
    
    // === IF Register Interface (directly exposed $FF0F) ===
    uint8_t ReadIF() const { return interrupt_flag | 0xE0; }  // Upper bits always 1
    void WriteIF(uint8_t value) { interrupt_flag = value & 0x1F; }
    
    // === IE Register Interface (directly exposed $FFFF) ===
    // Note: Unlike IF, ALL 8 bits of IE are R/W (per Mooneye unused_hwio test)
    uint8_t ReadIE() const { return interrupt_enable; }
    void WriteIE(uint8_t value) { interrupt_enable = value; }
    
    // === Request Interrupt (directly exposed input from peripheral) ===
    void RequestInterrupt(uint8_t bit) { interrupt_flag |= bit; }
    
    // === Clear Interrupt (directly exposed for CPU acknowledgment) ===
    void ClearInterrupt(uint8_t bit) { interrupt_flag &= ~bit; }
    
    // === Check for Pending Interrupts (directly exposed for CPU) ===
    uint8_t GetPendingInterrupts() const { 
        return interrupt_flag & interrupt_enable & 0x1F; 
    }
    
    // Returns the highest priority pending interrupt (0-4), or -1 if none
    int8_t GetHighestPriorityInterrupt() const;
    
    // Get the vector address for an interrupt bit
    static uint16_t GetInterruptVector(uint8_t bit);
    
private:
    // === Registers (directly exposed internal flip-flops) ===
    uint8_t interrupt_flag;     // IF ($FF0F) - which interrupts are pending
    uint8_t interrupt_enable;   // IE ($FFFF) - which interrupts are enabled
};

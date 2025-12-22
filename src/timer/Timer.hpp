#pragma once

#include <cstdint>

/**
 * Timer - DIV and TIMA Timer Hardware
 * 
 * Hardware Behavior:
 * - DIV is upper 8 bits of a 16-bit counter, increments every T-cycle
 * - TIMA increments on falling edge of selected DIV bit
 * - TIMA overflow triggers interrupt after 4 T-cycle delay, then reloads TMA
 * - Does NOT know about CPU/APU - only outputs interrupt signal and DIV bits
 * 
 * State Machine (per SameBoy):
 * - TIMA_RUNNING: Normal operation
 * - TIMA_RELOADING: Overflow just happened, TIMA already = TMA, waiting to set IF
 * - TIMA_RELOADED: Interrupt set, TIMA = TMA visible, writes to TIMA ignored
 * 
 * Interface:
 * - Register access: DIV ($FF04), TIMA ($FF05), TMA ($FF06), TAC ($FF07)
 * - Interrupt output signal
 * - DIV bit 12 output (for APU frame sequencer at 512 Hz)
 */
class Timer {
public:
    Timer();
    
    void Reset();
    
    // Advance timer by specified T-cycles
    void Step(uint8_t cycles);
    
    // === Register Interface (directly exposed memory-mapped I/O) ===
    uint8_t ReadRegister(uint16_t addr) const;
    void WriteRegister(uint16_t addr, uint8_t value);
    
    // === Interrupt Signal (directly exposed output pin) ===
    bool IsInterruptRequested() const { return interrupt_requested; }
    void ClearInterrupt() { interrupt_requested = false; }
    
    // === DIV Bit Output (directly exposed for APU frame sequencer) ===
    // Returns true when DIV bit 12 had a falling edge (512 Hz = 4194304 / 8192)
    bool DidDivBit12Fall() const { return div_bit12_fell; }
    void ClearDivBit12Fall() { div_bit12_fell = false; }
    
    // Full DIV counter for precise timing queries
    uint16_t GetDIVCounter() const { return div_counter; }
    
private:
    // === TIMA Reload State Machine (per SameBoy) ===
    enum TimaReloadState {
        TIMA_RUNNING,   // Normal operation
        TIMA_RELOADING, // Overflow happened, TMA loaded, waiting for interrupt
        TIMA_RELOADED   // Interrupt set, in the M-cycle after reload
    };
    
    // === Internal 16-bit Counter (directly exposed internal) ===
    // DIV register ($FF04) is bits 8-15 of this counter
    uint16_t div_counter;
    
    // === Registers (directly exposed memory-mapped) ===
    uint8_t tima;   // $FF05 - Timer Counter
    uint8_t tma;    // $FF06 - Timer Modulo  
    uint8_t tac;    // $FF07 - Timer Control
    
    // === State Machine (hardware-accurate TIMA reload behavior) ===
    TimaReloadState tima_reload_state;
    
    // === Output Signals (directly exposed output pins) ===
    bool interrupt_requested;
    bool div_bit12_fell;        // For APU frame sequencer (bit 12 = 512 Hz)
    
    // === Hardware-Accurate State Machine Methods ===
    void AdvanceTimaStateMachine();
    void IncreaseTima();
    
    // === Internal Helpers (directly expose timing logic) ===
    bool IsTimerEnabled() const { return tac & 0x04; }
    
    // Returns the bit position in div_counter that clocks TIMA
    uint16_t GetTimerBit() const {
        return GetTimerBitForTAC(tac);
    }
    
    uint16_t GetTimerBitForTAC(uint8_t tac_value) const {
        // TAC bits 1-0 select the frequency
        // 00: bit 9  (4096 Hz)
        // 01: bit 3  (262144 Hz)
        // 10: bit 5  (65536 Hz)
        // 11: bit 7  (16384 Hz)
        static constexpr uint16_t BIT_SELECT[4] = {
            1 << 9,   // 4096 Hz
            1 << 3,   // 262144 Hz
            1 << 5,   // 65536 Hz
            1 << 7    // 16384 Hz
        };
        return BIT_SELECT[tac_value & 0x03];
    }
};

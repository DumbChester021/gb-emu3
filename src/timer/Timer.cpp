#include "Timer.hpp"

Timer::Timer() {
    Reset();
}

void Timer::Reset() {
    div_counter = 0;
    tima = 0;
    tma = 0;
    tac = 0;
    tima_reload_state = TIMA_RUNNING;
    interrupt_requested = false;
    div_bit4_fell = false;
}

void Timer::Step(uint8_t cycles) {
    for (int i = 0; i < cycles; i++) {
        uint16_t old_div = div_counter;
        div_counter++;
        
        // Check DIV bit 4 falling edge for APU (512 Hz)
        if ((old_div & 0x10) && !(div_counter & 0x10)) {
            div_bit4_fell = true;
        }
        
        // Advance TIMA reload state machine every M-cycle (4 T-cycles)
        // Per SameBoy: state machine advances once per M-cycle
        if ((div_counter & 0x03) == 0) {
            AdvanceTimaStateMachine();
        }
        
        // TIMA increments on falling edge of selected bit
        if (IsTimerEnabled()) {
            uint16_t bit = GetTimerBit();
            if ((old_div & bit) && !(div_counter & bit)) {
                IncreaseTima();
            }
        }
    }
}

void Timer::AdvanceTimaStateMachine() {
    // State machine for hardware-accurate TIMA reload behavior:
    // RELOADING -> RELOADED: Load TMA into TIMA, set interrupt
    // RELOADED -> RUNNING: Return to normal operation
    if (tima_reload_state == TIMA_RELOADED) {
        tima_reload_state = TIMA_RUNNING;
    }
    else if (tima_reload_state == TIMA_RELOADING) {
        // This is when TMA is actually loaded into TIMA (4 T-cycles after overflow)
        tima = tma;
        interrupt_requested = true;
        tima_reload_state = TIMA_RELOADED;
    }
}

void Timer::IncreaseTima() {
    tima++;
    if (tima == 0) {
        // Overflow: TIMA stays at $00, enter RELOADING state
        // Per Mooneye tima_reload test: "TIMA register contains 00 for 4 cycles
        // before being reloaded with the value from the TMA register"
        // The reload happens in AdvanceTimaStateMachine on the next M-cycle
        tima_reload_state = TIMA_RELOADING;
    }
}

uint8_t Timer::ReadRegister(uint16_t addr) const {
    switch (addr) {
        case 0xFF04: return div_counter >> 8;  // DIV = upper 8 bits
        case 0xFF05: return tima;
        case 0xFF06: return tma;
        case 0xFF07: return tac | 0xF8;  // Upper bits always 1
        default: return 0xFF;
    }
}

void Timer::WriteRegister(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0xFF04:
            // Writing any value resets DIV to 0
            // This can cause TIMA increment if selected bit was 1
            {
                uint16_t bit = GetTimerBit();
                if (IsTimerEnabled() && (div_counter & bit)) {
                    IncreaseTima();
                }
            }
            div_counter = 0;
            break;
            
        case 0xFF05:
            // TIMA write behavior per SameBoy:
            // - If state == RELOADED: Write is ignored (TMA was just copied)
            // - Otherwise (RUNNING or RELOADING): Write takes effect
            //   If RELOADING, this cancels the pending reload
            if (tima_reload_state != TIMA_RELOADED) {
                tima = value;
                // Writing to TIMA during RELOADING cancels the reload
                if (tima_reload_state == TIMA_RELOADING) {
                    tima_reload_state = TIMA_RUNNING;
                }
            }
            break;
            
        case 0xFF06:
            // TMA write behavior per SameBoy:
            // - Always write to TMA
            // - If state != RUNNING: Also write to TIMA (TMA and TIMA are "connected")
            tma = value;
            if (tima_reload_state != TIMA_RUNNING) {
                tima = value;
            }
            break;
            
        case 0xFF07:
            {
                // Changing TAC can cause TIMA increment (falling edge glitch)
                uint8_t old_tac = tac;
                tac = value;
                
                bool old_enabled = (old_tac & 0x04);
                uint16_t old_bit = GetTimerBitForTAC(old_tac);
                bool old_signal = old_enabled && (div_counter & old_bit);
                
                bool new_enabled = (value & 0x04);
                uint16_t new_bit = GetTimerBit();
                bool new_signal = new_enabled && (div_counter & new_bit);
                
                if (old_signal && !new_signal) {
                    IncreaseTima();
                }
            }
            break;
    }
}


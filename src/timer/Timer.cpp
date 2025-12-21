#include "Timer.hpp"

Timer::Timer() {
    Reset();
}

void Timer::Reset() {
    div_counter = 0;
    tima = 0;
    tma = 0;
    tac = 0;
    overflow_pending = false;
    overflow_cycles = 0;
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
        
        // Handle TIMA overflow reload delay
        if (overflow_pending) {
            if (--overflow_cycles == 0) {
                tima = tma;
                interrupt_requested = true;
                overflow_pending = false;
            }
        }
        
        // TIMA increments on falling edge of selected bit
        if (IsTimerEnabled()) {
            uint16_t bit = GetTimerBit();
            if ((old_div & bit) && !(div_counter & bit)) {
                if (++tima == 0) {
                    // Overflow - TMA reload delayed by 4 cycles
                    overflow_pending = true;
                    overflow_cycles = 4;
                }
            }
        }
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
                    if (++tima == 0) {
                        overflow_pending = true;
                        overflow_cycles = 4;
                    }
                }
            }
            div_counter = 0;
            break;
        case 0xFF05:
            // Writing to TIMA during overflow delay cancels reload
            if (!overflow_pending) {
                tima = value;
            }
            break;
        case 0xFF06:
            tma = value;
            break;
        case 0xFF07:
            {
                // Changing TAC can cause TIMA increment
                uint8_t old_tac = tac;
                tac = value;
                
                uint16_t old_bit = GetTimerBit();
                bool old_enabled = (old_tac & 0x04);
                bool new_enabled = (value & 0x04);
                
                // Falling edge detection on timer signal
                bool old_signal = old_enabled && (div_counter & old_bit);
                uint16_t new_bit = GetTimerBit();
                bool new_signal = new_enabled && (div_counter & new_bit);
                
                if (old_signal && !new_signal) {
                    if (++tima == 0) {
                        overflow_pending = true;
                        overflow_cycles = 4;
                    }
                }
            }
            break;
    }
}

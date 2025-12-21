#include "Serial.hpp"

Serial::Serial() {
    Reset();
}

void Serial::Reset() {
    sb = 0;
    sc = 0;
    shift_clock = 0;
    bits_transferred = 0;
    transfer_active = false;
    serial_out = true;
    serial_in = true;
    clock_out = false;
    interrupt_requested = false;
    transfer_complete = false;
    transfer_data = 0;
}

void Serial::Step(uint8_t cycles) {
    if (!transfer_active || !IsInternalClock()) {
        return;
    }
    
    shift_clock += cycles;
    
    // DMG: 8192 Hz = 512 T-cycles per bit
    while (shift_clock >= CYCLES_PER_BIT) {
        shift_clock -= CYCLES_PER_BIT;
        
        // Shift out MSB, shift in from serial_in
        serial_out = (sb >> 7) & 1;
        sb = (sb << 1) | (serial_in ? 1 : 0);
        bits_transferred++;
        
        if (bits_transferred >= 8) {
            // Transfer complete
            transfer_active = false;
            bits_transferred = 0;
            sc &= ~0x80;  // Clear transfer flag
            interrupt_requested = true;
            transfer_complete = true;
        }
    }
}

void Serial::SetClockIn(bool value) {
    // External clock (slave mode)
    if (!IsInternalClock() && transfer_active) {
        // Rising edge of external clock
        if (value && !clock_out) {
            serial_out = (sb >> 7) & 1;
            sb = (sb << 1) | (serial_in ? 1 : 0);
            bits_transferred++;
            
            if (bits_transferred >= 8) {
                transfer_active = false;
                bits_transferred = 0;
                sc &= ~0x80;
                interrupt_requested = true;
                transfer_complete = true;
            }
        }
        clock_out = value;
    }
}

uint8_t Serial::ReadRegister(uint16_t addr) const {
    switch (addr) {
        case 0xFF01: return sb;
        case 0xFF02: return sc | 0x7E;  // Bits 1-6 always 1
        default: return 0xFF;
    }
}

void Serial::WriteRegister(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0xFF01:
            sb = value;
            break;
        case 0xFF02:
            sc = value;
            if (value & 0x80) {
                // Start transfer - save the byte being sent
                transfer_data = sb;  // Capture before shift
                transfer_active = true;
                bits_transferred = 0;
                shift_clock = 0;
            }
            break;
    }
}

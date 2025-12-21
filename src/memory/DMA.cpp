#include "DMA.hpp"

DMA::DMA() {
    Reset();
}

void DMA::Reset() {
    source_page = 0;
    byte_index = 0;
    active = false;
    in_startup = false;
    cycle_counter = 0;
    transfer_data = 0;
}

bool DMA::Step(uint8_t cycles) {
    if (!active) return false;
    
    cycle_counter += cycles;
    
    // 4-cycle startup delay before first byte transfer
    if (in_startup) {
        if (cycle_counter >= STARTUP_DELAY) {
            cycle_counter -= STARTUP_DELAY;
            in_startup = false;
            // Continue to check if we can transfer
        } else {
            return false;  // Still in startup
        }
    }
    
    // One byte transferred per 4 T-cycles
    if (cycle_counter >= CYCLES_PER_BYTE) {
        cycle_counter -= CYCLES_PER_BYTE;
        return true;  // Ready to transfer a byte
    }
    
    return false;
}

uint8_t DMA::ReadRegister() const {
    return source_page;
}

void DMA::WriteRegister(uint8_t value) {
    source_page = value;
    byte_index = 0;
    cycle_counter = 0;
    active = true;
    in_startup = true;  // Start with 4-cycle delay
}

void DMA::AcknowledgeTransfer() {
    byte_index++;
    
    if (byte_index >= BYTES_TO_TRANSFER) {
        active = false;
        byte_index = 0;
    }
}

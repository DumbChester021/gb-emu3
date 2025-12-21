#include "DMA.hpp"

// Hardware-accurate DMA implementation per Pan Docs:
// "160 × 4 + 4 clocks" = 644 cycles for the TRANSFER
//
// However, OAM blocking extends slightly beyond the transfer
// due to hardware timing (bus release latency). OAM remains
// blocked for ~648 cycles total (1 extra M-cycle for bus release).

DMA::DMA() {
    Reset();
}

void DMA::Reset() {
    source_page = 0;
    byte_index = 0;
    active = false;
    in_startup = false;
    in_winding_down = false;
    cycle_counter = 0;
    transfer_data = 0;
    total_cycles_tracked = 0;
}

bool DMA::Step(uint8_t cycles) {
    if (!active) return false;
    
    total_cycles_tracked += cycles;
    cycle_counter += cycles;
    
    // Wind-down phase: DMA finished transferring, still blocking OAM
    if (in_winding_down) {
        if (cycle_counter >= CYCLES_PER_BYTE) {
            active = false;
            in_winding_down = false;
            cycle_counter = 0;
            byte_index = 0;
        }
        return false;
    }
    
    // Process cycles
    while (cycle_counter >= CYCLES_PER_BYTE) {
        cycle_counter -= CYCLES_PER_BYTE;
        
        // Startup phase
        if (in_startup) {
            in_startup = false;
            continue;
        }
        
        // Ready to transfer next byte
        return true;
    }
    
    return false;
}

uint8_t DMA::ReadRegister() const {
    return source_page;
}

void DMA::WriteRegister(uint8_t value) {
    source_page = value;
    byte_index = 0;
    cycle_counter = 2;
    active = true;
    in_startup = true;
    in_winding_down = false;
    total_cycles_tracked = 0;
}

void DMA::AcknowledgeTransfer() {
    byte_index++;
    
    if (byte_index >= BYTES_TO_TRANSFER) {
        in_winding_down = true;
        cycle_counter = 0;
    }
}

bool DMA::IsBlockingOAM() const {
    return active && (in_startup || in_winding_down || byte_index > 0);
}

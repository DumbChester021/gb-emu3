#include "DMA.hpp"

// Hardware-accurate DMA implementation per SameBoy memory.c:
//
// State Machine (using dma_current_dest):
//   0xFF: Initial warm-up state (OAM accessible)
//   0x00: Second warm-up state (OAM accessible)  
//   0x01-0x9F: Transfer in progress (OAM blocked)
//   0xA0: Wind-down state (OAM blocked)
//   0xA1: DMA complete (inactive)
//
// Per SameBoy line 256: is_addr_in_dma_use() returns FALSE when
// dma_current_dest == 0xFF || dma_current_dest == 0x0

DMA::DMA() {
    Reset();
}

void DMA::Reset() {
    source_page = 0;
    byte_index = 0;
    active = false;
    warm_up_cycles = 0;
    in_winding_down = false;
    is_restarting = false;
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
        
        // Track M-cycle progression (maps to SameBoy's dest value)
        // Per SameBoy GB_dma_run:
        //   - dest = 0xFF on write
        //   - First dma_run: 0xFF >= 0xA0, so dest++→0x00, break (1 warm-up cycle)
        //   - Second dma_run: 0x00 < 0xA0, so normal transfer, dest++→0x01
        // So there's ONLY 1 warm-up M-cycle (dest = 0xFF → 0x00)
        if (warm_up_cycles < 1) {
            warm_up_cycles++;
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
    // Per SameBoy line 1617:
    // dma_restarting = (dest != 0xA1 && dest != 0xA0)
    // i.e., restarting if previous DMA was actively transferring
    is_restarting = active && !in_winding_down && warm_up_cycles > 0;
    
    source_page = value;
    byte_index = 0;
    cycle_counter = 2;  // SameBoy: dma_cycles_modulo = 2
    active = true;
    warm_up_cycles = 0;  // Start warm-up phase
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
    // Per SameBoy memory.c line 555 (read_high_memory):
    // if (GB_is_dma_active(gb) && (gb->dma_current_dest != 0 || gb->dma_restarting))
    //     return 0xFF;  // blocked
    //
    // Dest state progression:
    //   Write:     dest = 0xFF  (blocked: 0xFF != 0)
    //   1st step:  dest = 0x00  (NOT blocked: 0x00 == 0, unless restarting)  
    //   2nd step:  dest = 0x01  (blocked: 0x01 != 0)
    //
    // Our state mapping:
    //   warm_up = 0: dest = 0xFF (just wrote, blocked)
    //   warm_up = 1 && byte_index = 0: dest = 0x00 (after warm-up, NOT blocked)
    //   byte_index >= 1: dest >= 0x01 (during transfer, blocked)
    //   in_winding_down: dest = 0xA0 (blocked)
    //
    if (!active) return false;
    
    // If DMA was restarted, OAM is blocked even during the warm-up window
    if (is_restarting) return true;
    
    // During wind-down, OAM is blocked
    if (in_winding_down) return true;
    
    // During transfer (byte_index >= 1), OAM is blocked
    if (byte_index > 0) return true;
    
    // OAM is NOT blocked when warm_up == 1 AND byte_index == 0
    // This is the brief window after warm-up, before first byte is transferred
    return warm_up_cycles == 0;
}


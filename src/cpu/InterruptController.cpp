#include "InterruptController.hpp"

InterruptController::InterruptController() {
    Reset();
}

void InterruptController::Reset() {
    interrupt_flag = 0;
    interrupt_enable = 0;
}

int8_t InterruptController::GetHighestPriorityInterrupt() const {
    uint8_t pending = interrupt_flag & interrupt_enable & 0x1F;
    
    if (!pending) return -1;
    
    // Check in priority order (bit 0 = highest)
    for (int i = 0; i < 5; i++) {
        if (pending & (1 << i)) {
            return i;
        }
    }
    
    return -1;
}

uint16_t InterruptController::GetInterruptVector(uint8_t bit) {
    // Vectors: VBlank=$40, STAT=$48, Timer=$50, Serial=$58, Joypad=$60
    switch (bit) {
        case VBLANK: return 0x0040;
        case STAT:   return 0x0048;
        case TIMER:  return 0x0050;
        case SERIAL: return 0x0058;
        case JOYPAD: return 0x0060;
        default:     return 0x0000;
    }
}

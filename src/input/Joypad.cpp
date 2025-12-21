#include "Joypad.hpp"

Joypad::Joypad() {
    Reset();
}

void Joypad::Reset() {
    select = 0x30;  // Both rows deselected
    for (int i = 0; i < 8; i++) {
        buttons[i] = false;
    }
    interrupt_requested = false;
    previous_state = 0x0F;
}

uint8_t Joypad::ReadRegister() const {
    uint8_t result = select | 0xC0;  // Upper bits always 1
    
    // P14 selects direction buttons, P15 selects action buttons
    if (!(select & 0x10)) {
        // Direction buttons (directly exposed)
        if (buttons[BUTTON_RIGHT]) result &= ~0x01;
        if (buttons[BUTTON_LEFT])  result &= ~0x02;
        if (buttons[BUTTON_UP])    result &= ~0x04;
        if (buttons[BUTTON_DOWN])  result &= ~0x08;
    }
    
    if (!(select & 0x20)) {
        // Action buttons (directly exposed)
        if (buttons[BUTTON_A])      result &= ~0x01;
        if (buttons[BUTTON_B])      result &= ~0x02;
        if (buttons[BUTTON_SELECT]) result &= ~0x04;
        if (buttons[BUTTON_START])  result &= ~0x08;
    }
    
    return result;
}

void Joypad::WriteRegister(uint8_t value) {
    select = value & 0x30;
}

void Joypad::SetButton(uint8_t button, bool pressed) {
    if (button >= 8) return;
    
    // Check for high-to-low transition (button press)
    if (pressed && !buttons[button]) {
        // Determine if this button is currently selected
        bool selected = false;
        if (button >= 4) {
            // Direction button
            selected = !(select & 0x10);
        } else {
            // Action button
            selected = !(select & 0x20);
        }
        
        if (selected) {
            interrupt_requested = true;
        }
    }
    
    buttons[button] = pressed;
}

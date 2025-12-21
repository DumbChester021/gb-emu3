#include "Joypad.hpp"

Joypad::Joypad() {
    Reset();
}

void Joypad::Reset() {
    select = 0x30;  // Both rows deselected (bits 4-5 set high)
    for (int i = 0; i < 8; i++) {
        buttons[i] = false;
    }
    interrupt_requested = false;
}

uint8_t Joypad::GetP10_P13_State() const {
    uint8_t lines = 0x0F; // Default high (released)

    // P14 = Bit 4 (Direction buttons) - Active Low (0=Select)
    // If bit 4 is 0, we check directions
    if (!(select & 0x10)) {
        if (buttons[BUTTON_RIGHT]) lines &= ~0x01;
        if (buttons[BUTTON_LEFT])  lines &= ~0x02;
        if (buttons[BUTTON_UP])    lines &= ~0x04;
        if (buttons[BUTTON_DOWN])  lines &= ~0x08;
        
        // Per SameBoy joypad.c lines 109-116:
        // Hardware prevents opposing keys from both registering
        // If right is pressed, left cannot be pressed (and vice versa)
        // If up is pressed, down cannot be pressed (and vice versa)
        if (!(lines & 0x01)) lines |= 0x02;  // Right pressed -> force Left released
        if (!(lines & 0x04)) lines |= 0x08;  // Up pressed -> force Down released
    }

    // P15 = Bit 5 (Action buttons) - Active Low (0=Select)
    // If bit 5 is 0, we check actions
    if (!(select & 0x20)) {
        if (buttons[BUTTON_A])      lines &= ~0x01;
        if (buttons[BUTTON_B])      lines &= ~0x02;
        if (buttons[BUTTON_SELECT]) lines &= ~0x04;
        if (buttons[BUTTON_START])  lines &= ~0x08;
    }

    return lines; // Returns 0-F (4 bits)
}

uint8_t Joypad::ReadRegister() const {
    // Return:
    // Bit 7,6: Always 1
    // Bit 5,4: Select bits (from register write)
    // Bit 3-0: Input lines (from GetP10_P13_State)
    return 0xC0 | (select & 0x30) | GetP10_P13_State();
}

void Joypad::WriteRegister(uint8_t value) {
    uint8_t old_lines = GetP10_P13_State();
    
    // Only bits 4-5 are writable
    select = value & 0x30;
    
    uint8_t new_lines = GetP10_P13_State();
    
    // Check for High-to-Low transition on any input line
    // (old=1, new=0) -> interrupt
    if ((old_lines & ~new_lines) & 0x0F) {
        interrupt_requested = true;
    }
}

void Joypad::SetButton(uint8_t button, bool pressed) {
    if (button >= 8) return;
    
    uint8_t old_lines = GetP10_P13_State();
    
    buttons[button] = pressed;
    
    uint8_t new_lines = GetP10_P13_State();
    
    // Check for High-to-Low transition
    if ((old_lines & ~new_lines) & 0x0F) {
        interrupt_requested = true;
    }
}

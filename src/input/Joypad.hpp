#pragma once

#include <cstdint>

/**
 * Joypad - Button Input Hardware
 * 
 * Hardware Behavior:
 * - 8 buttons arranged in 2x4 matrix
 * - Select line chooses which row to read
 * - Active-low logic (0 = pressed, 1 = released)
 * - Does NOT know about CPU - only responds to register read/write and button state
 * 
 * Interface:
 * - Single register at $FF00
 * - Button state input from frontend
 * - Interrupt output signal
 */
class Joypad {
public:
    Joypad();
    
    void Reset();
    
    // === Register Interface (directly exposed memory-mapped I/O at $FF00) ===
    uint8_t ReadRegister() const;
    void WriteRegister(uint8_t value);
    
    // === Button Input (directly exposed from external input) ===
    // Each button is a separate input signal
    void SetButton(uint8_t button, bool pressed);
    
    // Button indices
    static constexpr uint8_t BUTTON_A      = 0;
    static constexpr uint8_t BUTTON_B      = 1;
    static constexpr uint8_t BUTTON_SELECT = 2;
    static constexpr uint8_t BUTTON_START  = 3;
    static constexpr uint8_t BUTTON_RIGHT  = 4;
    static constexpr uint8_t BUTTON_LEFT   = 5;
    static constexpr uint8_t BUTTON_UP     = 6;
    static constexpr uint8_t BUTTON_DOWN   = 7;
    
    // === Interrupt Signal (directly exposed output pin) ===
    bool IsInterruptRequested() const { return interrupt_requested; }
    void ClearInterrupt() { interrupt_requested = false; }
    
private:
    // === Internal State (directly exposed internal flip-flops) ===
    uint8_t select;             // P14/P15 select lines (bits 4-5 of $FF00)
    
    // Button states (directly exposed, directly set by external input)
    // Active-low: false = released, true = pressed
    bool buttons[8];
    
    // === Output Signals (directly exposed output pin) ===
    bool interrupt_requested;
    
    // Previous state for edge detection
    uint8_t previous_state;
};

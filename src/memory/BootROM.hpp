#pragma once

#include <cstdint>
#include <array>
#include <string>

/**
 * BootROM - DMG Boot ROM (256 bytes)
 * 
 * Hardware Behavior:
 * - Mapped to $0000-$00FF when console powers on
 * - Unmapped when $FF50 is written (any value)
 * - Contains Nintendo logo check and initialization
 * - Does NOT know about other components
 * 
 * Interface:
 * - Address input ($00-$FF)
 * - Data output
 */
class BootROM {
public:
    BootROM();
    
    // Load boot ROM from file
    bool Load(const std::string& path);
    
    // === ROM Interface (directly exposed $0000-$00FF) ===
    uint8_t Read(uint16_t addr) const;
    
    // === Enable/Disable (directly exposed via $FF50) ===
    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool value) { enabled = value; }
    
    // Check if boot ROM is loaded
    bool IsLoaded() const { return loaded; }
    
private:
    // === ROM Storage (directly exposed internal storage) ===
    std::array<uint8_t, 256> rom;
    
    bool enabled;   // Mapped to address space
    bool loaded;    // ROM file was loaded successfully
};

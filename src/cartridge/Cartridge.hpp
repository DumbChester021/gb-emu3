#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

/**
 * Cartridge - Game Cartridge Interface
 * 
 * Hardware Behavior:
 * - ROM storage (read-only game data)
 * - Optional RAM (battery-backed saves)
 * - Optional MBC (Memory Bank Controller) for banking
 * - Does NOT know about CPU - only responds to address/data signals
 * 
 * The cartridge is a separate PCB that plugs into the console.
 * It contains its own chips (ROM, RAM, MBC controller).
 * 
 * Interface:
 * - Address input (directly exposed A0-A15)
 * - Data output (directly exposed D0-D7)
 * - Read/Write control
 */
class Cartridge {
public:
    Cartridge();
    ~Cartridge();
    
    // Load ROM from file
    bool LoadROM(const std::string& path);
    
    // Load battery-backed save
    bool LoadSave(const std::string& path);
    bool SaveRAM(const std::string& path) const;
    
    // === Cartridge Pins (directly exposed address/data interface) ===
    uint8_t Read(uint16_t addr) const;
    void Write(uint16_t addr, uint8_t value);
    
    // === Cartridge Info (directly exposed from ROM header) ===
    std::string GetTitle() const { return title; }
    uint8_t GetCartridgeType() const { return cartridge_type; }
    uint8_t GetROMSizeCode() const { return rom_size_code; }
    uint8_t GetRAMSizeCode() const { return ram_size_code; }
    bool HasBattery() const { return has_battery; }
    bool HasTimer() const { return has_timer; }
    bool IsLoaded() const { return rom_loaded; }
    bool IsDirty() const { return ram_dirty; }  // RAM modified since last save
    void ClearDirty() { ram_dirty = false; }    // Call after successful save
    
    // Get detailed ROM information for display
    std::string GetDetailedInfo() const;
    
private:
    // === ROM Storage (directly exposed internal storage) ===
    std::vector<uint8_t> rom;
    
    // === External RAM (directly exposed, battery-backed) ===
    mutable std::vector<uint8_t> ram;  // mutable for const Read with banking
    bool ram_enabled;
    mutable bool ram_dirty;  // Track if RAM was modified since last save
    
    // === MBC State (directly exposed internal MBC registers) ===
    uint8_t mbc_type;           // MBC variant (0 = none, 1, 2, 3, 5)
    uint16_t rom_bank;          // Current ROM bank (directly exposed)
    uint8_t ram_bank;           // Current RAM bank (directly exposed)
    bool ram_bank_mode;         // MBC1: RAM banking mode (directly exposed)
    bool mbc1_multicart;        // MBC1M: Multicart mode (different BANK2 wiring)
    
    // MBC3 RTC (directly exposed)
    // Per hardware: RTC runs from real system time, latched values frozen when latch triggered
    struct RTCRegisters {
        uint8_t seconds;    // 0-59
        uint8_t minutes;    // 0-59
        uint8_t hours;      // 0-23
        uint8_t days_low;   // Lower 8 bits of day counter
        uint8_t days_high;  // Bit 0: Day counter MSB, Bit 6: Halt, Bit 7: Day carry
    };
    mutable RTCRegisters rtc_real;      // Current RTC values (synced with system time)
    mutable RTCRegisters rtc_latched;   // Latched values (frozen on latch)
    mutable int64_t last_rtc_second;    // Unix timestamp when RTC was last synced
    uint8_t rtc_latch_register;         // For detecting 0->1 latch transition
    
    // Update RTC from system time
    void UpdateRTC() const;
    
    // === ROM Header Info (directly exposed, parsed on load) ===
    std::string title;
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    bool has_battery;
    bool has_timer;
    bool rom_loaded;
    
    // === MBC Operations (directly expose banking logic) ===
    uint8_t ReadROM(uint16_t addr) const;
    void WriteROM(uint16_t addr, uint8_t value);
    uint8_t ReadRAM(uint16_t addr) const;
    void WriteRAM(uint16_t addr, uint8_t value);
    
    // Bank number calculations (directly exposed)
    uint32_t GetROMOffset(uint16_t addr) const;
    uint32_t GetRAMOffset(uint16_t addr) const;
    
    void ParseHeader();
    size_t GetRAMSize() const;
};

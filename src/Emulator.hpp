#pragma once

#include <memory>
#include <string>
#include <functional>

// Forward declarations - components don't know each other
class CPU;
class PPU;
class APU;
class Timer;
class Joypad;
class Serial;
class Bus;
class Memory;
class DMA;
class Cartridge;
class InterruptController;
class BootROM;

/**
 * Emulator - The "Motherboard" / LR35902 SoC Simulation
 * 
 * Hardware Accuracy Model:
 * This class represents the PHYSICAL INTERCONNECTION of components,
 * like the silicon die of the LR35902 or the PCB traces on the motherboard.
 * 
 * In real hardware:
 * - A master clock (4.194304 MHz) drives everything
 * - Components are connected via address/data buses and control lines
 * - The CPU drives the clock, but other components run in parallel
 * 
 * This orchestrator:
 * - Wires components together via the Bus
 * - Distributes clock signals to all components
 * - Routes interrupt signals from peripherals to CPU
 * - Does NOT contain emulation logic - that's in the components
 */
class Emulator {
public:
    Emulator();
    ~Emulator();
    
    // === Initialization (like powering on the console) ===
    bool LoadROM(const std::string& path);
    bool LoadBootROM(const std::string& path);
    void Reset();
    
    // === Clock Distribution (master clock drives everything) ===
    // Step by one CPU instruction (returns T-cycles consumed)
    // This is NOT hardware-accurate for mid-instruction timing,
    // but matches the instruction-level timing of real hardware
    uint8_t Step();
    
    // Step by exactly N T-cycles (for precise timing tests)
    void StepCycles(uint32_t cycles);
    
    // Run until frame complete (~70224 T-cycles)
    void RunFrame();
    
    // === Hardware Signal Access (directly exposed outputs) ===
    
    // Display output (directly exposed from PPU)
    const uint8_t* GetFramebuffer() const;
    bool IsFrameComplete() const;
    void ClearFrameComplete();
    
    // Audio output (directly exposed from APU)
    void GetAudioSample(float& left, float& right) const;
    bool HasAudioSample() const;
    void ClearAudioSample();
    
    // === Input (directly exposed to Joypad) ===
    void SetButton(uint8_t button, bool pressed);
    
    // === Serial Link (directly exposed for link cable) ===
    bool GetSerialOut() const;
    void SetSerialIn(bool value);
    uint8_t GetSerialData() const;
    bool IsSerialTransferComplete() const;
    void ClearSerialTransferComplete();
    
    // === Debug Access (directly exposed for debugging) ===
    uint16_t GetPC() const;
    uint16_t GetSP() const;
    uint16_t GetAF() const;
    uint16_t GetBC() const;
    uint16_t GetDE() const;
    uint16_t GetHL() const;
    uint8_t GetPPUMode() const;
    uint8_t GetLY() const;
    uint64_t GetTotalCycles() const { return total_cycles; }
    
    // Direct memory read (for debuggers, bypasses normal restrictions)
    uint8_t DebugRead(uint16_t addr) const;
    
    // Check if boot ROM is still running
    bool IsBootROMActive() const;
    
    // Set Mooneye test result callback (passes to CPU)
    void SetMooneyeCallback(std::function<void(bool)> callback);
    
private:
    // === Hardware Components (like chips on the motherboard) ===
    std::unique_ptr<CPU> cpu;
    std::unique_ptr<PPU> ppu;
    std::unique_ptr<APU> apu;
    std::unique_ptr<Timer> timer;
    std::unique_ptr<Joypad> joypad;
    std::unique_ptr<Serial> serial;
    std::unique_ptr<Bus> bus;
    std::unique_ptr<Memory> memory;
    std::unique_ptr<DMA> dma;
    std::unique_ptr<Cartridge> cartridge;
    std::unique_ptr<InterruptController> interrupts;
    std::unique_ptr<BootROM> bootrom;
    
    // === Clock Counter (directly exposed master clock) ===
    uint64_t total_cycles;
    
    // === Internal Wiring (connecting components like PCB traces) ===
    void WireComponents();
    
    // Called after each T-cycle block to synchronize components
    void TickComponents(uint8_t cycles);
    
    // Route interrupt signals from peripherals to CPU
    void UpdateInterrupts();
    
    // Handle OAM DMA transfers
    void ProcessDMA(uint8_t cycles);
    
    // === I/O Register Router (the I/O decoder in LR35902) ===
    // Routes $FF00-$FF7F to appropriate component
    uint8_t ReadIO(uint16_t addr);
    void WriteIO(uint16_t addr, uint8_t value);
};

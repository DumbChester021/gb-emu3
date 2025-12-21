#pragma once

#include <cstdint>
#include <array>
#include <functional>

/**
 * SM83 CPU - Sharp LR35902 CPU Core
 * 
 * Hardware Behavior:
 * - Runs at 4.194304 MHz (T-cycles)
 * - Exposes address bus (A0-A15), data bus (D0-D7), and control signals
 * - Does NOT know about PPU, APU, Timer - only sees memory through the bus
 * 
 * Interface:
 * - Provides address and data for memory operations
 * - Receives data from memory reads
 * - Signals: RD (read), WR (write), interrupt lines
 */
class CPU {
public:
    CPU();
    
    // Reset to initial state (after boot ROM or direct boot)
    void Reset(bool bootRomEnabled = false);
    
    // Execute one instruction, returns T-cycles consumed
    uint8_t Step();
    
    // Interrupt request lines (directly exposed pins)
    void RequestInterrupt(uint8_t bit);
    
    // === Memory Interface (directly exposed pins) ===
    uint16_t GetAddressBus() const { return address_bus; }
    uint8_t GetDataBus() const { return data_bus; }
    void SetDataBus(uint8_t value) { data_bus = value; }
    bool IsReading() const { return read_signal; }
    bool IsWriting() const { return write_signal; }
    void AcknowledgeMemoryOperation() { 
        read_signal = false; 
        write_signal = false; 
    }
    
    // === Bus Connection ===
    using ReadCallback = std::function<uint8_t(uint16_t)>;
    using WriteCallback = std::function<void(uint16_t, uint8_t)>;
    using TickCallback = std::function<void(uint8_t)>;
    using MooneyeCallback = std::function<void(bool)>;  // bool = passed
    
    void ConnectBus(ReadCallback read, WriteCallback write, TickCallback tick = nullptr) {
        bus_read = read;
        bus_write = write;
        tick_callback = tick;
    }
    
    void SetMooneyeCallback(MooneyeCallback cb) { mooneye_callback = cb; }
    
    // === Register Access (for instruction implementations) ===
    // Getters
    uint8_t GetA() const { return a; }
    uint8_t GetF() const { return f; }
    uint8_t GetB() const { return b; }
    uint8_t GetC() const { return c; }
    uint8_t GetD() const { return d; }
    uint8_t GetE() const { return e; }
    uint8_t GetH() const { return h; }
    uint8_t GetL() const { return l; }
    uint16_t GetPC() const { return pc; }
    uint16_t GetSP() const { return sp; }
    uint16_t GetAF() const { return (static_cast<uint16_t>(a) << 8) | f; }
    uint16_t GetBC() const { return (static_cast<uint16_t>(b) << 8) | c; }
    uint16_t GetDE() const { return (static_cast<uint16_t>(d) << 8) | e; }
    uint16_t GetHL() const { return (static_cast<uint16_t>(h) << 8) | l; }
    
    // Setters  
    void SetA(uint8_t v) { a = v; }
    void SetF(uint8_t v) { f = v & 0xF0; }  // Lower 4 bits always 0
    void SetB(uint8_t v) { b = v; }
    void SetC(uint8_t v) { c = v; }
    void SetD(uint8_t v) { d = v; }
    void SetE(uint8_t v) { e = v; }
    void SetH(uint8_t v) { h = v; }
    void SetL(uint8_t v) { l = v; }
    void SetPC(uint16_t v) { pc = v; }
    void SetSP(uint16_t v) { sp = v; }
    void SetAF(uint16_t v) { a = v >> 8; f = v & 0xF0; }
    void SetBC(uint16_t v) { b = v >> 8; c = v & 0xFF; }
    void SetDE(uint16_t v) { d = v >> 8; e = v & 0xFF; }
    void SetHL(uint16_t v) { h = v >> 8; l = v & 0xFF; }
    
    // Flag access
    bool GetFlagZ() const { return (f & FLAG_Z) != 0; }
    bool GetFlagN() const { return (f & FLAG_N) != 0; }
    bool GetFlagH() const { return (f & FLAG_H) != 0; }
    bool GetFlagC() const { return (f & FLAG_C) != 0; }
    void SetFlagZ(bool v) { if (v) f |= FLAG_Z; else f &= ~FLAG_Z; }
    void SetFlagN(bool v) { if (v) f |= FLAG_N; else f &= ~FLAG_N; }
    void SetFlagH(bool v) { if (v) f |= FLAG_H; else f &= ~FLAG_H; }
    void SetFlagC(bool v) { if (v) f |= FLAG_C; else f &= ~FLAG_C; }
    
    // State access
    bool IsHalted() const { return halted; }
    void SetHalted(bool v) { halted = v; }
    bool InterruptsEnabled() const { return ime; }
    bool GetIME() const { return ime; }
    void SetIME(bool v) { ime = v; }
    void ScheduleIME() { ime_scheduled = true; }
    void CancelScheduledIME() { ime_scheduled = false; }  // For DI instruction
    void SetHaltBug(bool v) { halt_bug = v; }
    
    // === Memory Operations (public for instruction use) ===
    uint8_t FetchByte();
    uint16_t FetchWord();
    uint8_t ReadByte(uint16_t addr);
    void WriteByte(uint16_t addr, uint8_t value);
    
    // Internal delay - accumulates pending cycles (SameBoy pattern: no memory access)
    void InternalDelay() { pending_cycles += 4; }
    
    // Peek memory without ticking (for internal checks like HALT bug)
    uint8_t PeekByte(uint16_t addr) { return bus_read ? bus_read(addr) : 0xFF; }
    
private:
    // === Registers (internal flip-flops) ===
    uint8_t a, f;           // Accumulator and flags
    uint8_t b, c;           // BC register pair
    uint8_t d, e;           // DE register pair
    uint8_t h, l;           // HL register pair
    uint16_t sp;            // Stack pointer
    uint16_t pc;            // Program counter
    
    // === Internal State (internal latches) ===
    bool ime;               // Interrupt master enable
    bool ime_scheduled;     // EI enables IME after next instruction
    bool halted;            // CPU halted, waiting for interrupt
    bool halt_bug;          // HALT bug trigger
    
    // === Bus Interface ===
    uint16_t address_bus;
    uint8_t data_bus;
    bool read_signal;
    bool write_signal;
    
    // Bus callbacks (wired by Emulator)
    ReadCallback bus_read;
    WriteCallback bus_write;
    TickCallback tick_callback;  // Called each M-cycle (4 T-cycles)
    MooneyeCallback mooneye_callback;  // Called on LD B,B with test result
    
    // === Pending Cycles (SameBoy pattern for hardware accuracy) ===
    uint8_t pending_cycles = 0;  // Deferred cycles, flushed before next memory op
    
    // === Flag Constants ===
    static constexpr uint8_t FLAG_Z = 0x80;
    static constexpr uint8_t FLAG_N = 0x40;
    static constexpr uint8_t FLAG_H = 0x20;
    static constexpr uint8_t FLAG_C = 0x10;
    
    // === Internal operations ===
    void Push(uint16_t value);
    uint16_t Pop();
    void HandleInterrupts();
    void FlushPendingCycles();  // Flush deferred cycles to components
};

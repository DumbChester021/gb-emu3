#include "CPU.hpp"
#include "Instructions.hpp"
#include <cstdio>

CPU::CPU() {
    Reset(true);
}

void CPU::Reset(bool bootRomEnabled) {
    // Clear registers
    a = f = b = c = d = e = h = l = 0;
    sp = 0;
    pc = 0;
    
    // Internal state
    ime = false;
    ime_scheduled = false;
    halted = false;
    halt_bug = false;
    
    // Bus signals
    address_bus = 0;
    data_bus = 0;
    read_signal = false;
    write_signal = false;
    
    if (!bootRomEnabled) {
        // Post-boot state (skipping boot ROM)
        a = 0x01;
        f = 0xB0;  // Z=1, N=0, H=1, C=1
        b = 0x00;
        c = 0x13;
        d = 0x00;
        e = 0xD8;
        h = 0x01;
        l = 0x4D;
        sp = 0xFFFE;
        pc = 0x0100;
    }
}

uint8_t CPU::Step() {
    // Handle interrupts first (uses IF from previous Step)
    // For HALT wake, we jump here after detecting interrupt mid-M-cycle
handle_interrupts:
    HandleInterrupts();
    
    if (halted) {
        // HALT mode: CPU stopped, but clocks keep running
        // Per SameBoy sm83_cpu.c L1625-1632: DMG HALT advances 2 cycles,
        // checks IF, then advances 2 more. This allows interrupt detection
        // in the middle of an M-cycle for proper M-cycle bucket alignment.
        
        // Flush any pending cycles first
        FlushPendingCycles();
        
        // Step 1: Advance 2 T-cycles (per SameBoy L1626)
        if (tick_callback) {
            tick_callback(2);
        }
        
        // Step 2: Check IF NOW (per SameBoy L1629)
        // This happens BETWEEN the two 2-cycle advances
        uint8_t if_reg = bus_read ? bus_read(0xFF0F) : 0;
        uint8_t ie_reg = bus_read ? bus_read(0xFFFF) : 0;
        if ((if_reg & ie_reg & 0x1F) != 0) {
            halted = false;  // Wake from HALT at mid-M-cycle
            // Still need to complete the M-cycle
            if (tick_callback) {
                tick_callback(2);  // Remaining 2 cycles
            }
            // Per SameBoy L1643-1700: Interrupt dispatch happens in SAME function call
            // Don't return - jump to handle_interrupts to dispatch immediately
            goto handle_interrupts;
        }
        
        // Step 3: Advance 2 more T-cycles (per SameBoy L1632)
        if (tick_callback) {
            tick_callback(2);
        }
        
        return 4;
    }
    
    // Fetch and execute instruction
    uint8_t opcode = FetchByte();
    
    if (halt_bug) {
        // HALT bug: PC doesn't increment for next fetch
        pc--;
        halt_bug = false;
    }
    
    // Execute via instruction dispatcher
    // Note: ExecuteOpcode returns total T-cycles including the fetch we already did
    uint8_t cycles = ExecuteOpcode(*this, opcode);
    
    // Flush any remaining pending cycles from instruction
    FlushPendingCycles();
    
    // Mooneye test detection: LD B,B (0x40) signals test completion
    if (opcode == 0x40 && mooneye_callback) {
        // Check for Fibonacci sequence: B=3, C=5, D=8, E=13, H=21, L=34
        bool pass = (b == 3 && c == 5 && d == 8 && e == 13 && h == 21 && l == 34);
        // Check for fail: all = 0x42
        bool fail = (b == 0x42 && c == 0x42 && d == 0x42 && e == 0x42 && h == 0x42 && l == 0x42);
        if (pass || fail) {
            mooneye_callback(pass);
        }
    }
    
    return cycles;
}

void CPU::RequestInterrupt(uint8_t bit) {
    // Set IF bit via bus (hardware accurate)
    if (bus_read && bus_write) {
        uint8_t if_reg = bus_read(0xFF0F);
        bus_write(0xFF0F, if_reg | bit);
    }
}

void CPU::HandleInterrupts() {
    // NOTE: ime_scheduled is processed in FetchByte() per GBCTR spec
    // IME=1 must happen AT M2/M1 (during fetch), not before
    
    // Read IF and IE from bus (hardware accurate)
    uint8_t if_reg = bus_read ? bus_read(0xFF0F) : 0;
    uint8_t ie_reg = bus_read ? bus_read(0xFFFF) : 0;
    uint8_t pending = if_reg & ie_reg & 0x1F;
    
    if (pending) {
        halted = false;  // Wake from HALT
        
        if (ime) {
            ime = false;
            
            // Find highest priority interrupt
            for (int i = 0; i < 5; i++) {
                if (pending & (1 << i)) {
                    uint8_t int_bit = 1 << i;
                    
                    // === Interrupt Dispatch: 20 T-cycles (5 M-cycles) ===
                    // Per Cycle-Accurate GB Docs:
                    // 1. Two wait states (2 M-cycles = 8T)
                    InternalDelay();  // M1: NOP/wait
                    InternalDelay();  // M2: NOP/wait
                    
                    // 2. Push PC high byte to stack (M3)
                    // Per ie_push test: if this push writes to $FFFF (IE),
                    // the interrupt may be cancelled
                    sp--;
                    WriteByte(sp, pc >> 8);  // High byte push (may write to $FFFF = IE)
                    
                    // Re-read IE after high byte push (per ie_push test)
                    ie_reg = bus_read ? bus_read(0xFFFF) : 0;
                    
                    // 3. Push PC low byte to stack (M4)
                    sp--;
                    WriteByte(sp, pc & 0xFF);  // Low byte push
                    
                    // 4. Set PC (M5) - re-evaluate interrupt selection
                    InternalDelay();
                    
                    // Per ie_push test Round 4: after modifying IE, re-check which
                    // interrupt is now highest priority pending. If the original was
                    // cancelled, a different one may be dispatched.
                    uint8_t new_pending = if_reg & ie_reg & 0x1F;
                    
                    if (!new_pending) {
                        // No interrupts pending anymore - dispatch cancelled
                        // PC = $0000 (full cancellation case)
                        pc = 0x0000;
                    } else {
                        // Find highest priority of remaining pending interrupts
                        for (int j = 0; j < 5; j++) {
                            if (new_pending & (1 << j)) {
                                // Clear IF bit and jump to this vector
                                if (bus_write) {
                                    bus_write(0xFF0F, if_reg & ~(1 << j));
                                }
                                pc = 0x40 + (j * 8);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}

// === Memory Operations (use bus callbacks) ===

uint8_t CPU::FetchByte() {
    // SameBoy pattern: flush pending cycles BEFORE memory operation
    FlushPendingCycles();
    
    // Per GBCTR: EI sets IME=1 at M2/M1 (during fetch of next instruction)
    // This is the moment when the scheduled IME enable takes effect
    if (ime_scheduled) {
        ime = true;
        ime_scheduled = false;
    }
    
    address_bus = pc++;
    read_signal = true;
    if (bus_read) {
        data_bus = bus_read(address_bus);
    }
    
    // Defer this cycle until next memory op
    pending_cycles = 4;
    return data_bus;
}

uint16_t CPU::FetchWord() {
    uint8_t lo = FetchByte();
    uint8_t hi = FetchByte();
    return (static_cast<uint16_t>(hi) << 8) | lo;
}

uint8_t CPU::ReadByte(uint16_t addr) {
    // SameBoy pattern: flush pending cycles BEFORE memory operation
    FlushPendingCycles();
    
    address_bus = addr;
    read_signal = true;
    if (bus_read) {
        data_bus = bus_read(addr);
    }
    
    // Defer this cycle until next memory op
    pending_cycles = 4;
    return data_bus;
}

void CPU::WriteByte(uint16_t addr, uint8_t value) {
    // SameBoy pattern: flush pending cycles BEFORE memory operation
    FlushPendingCycles();
    
    address_bus = addr;
    data_bus = value;
    write_signal = true;
    if (bus_write) {
        bus_write(addr, value);
    }
    
    // Defer this cycle until next memory op
    pending_cycles = 4;
}

void CPU::Push(uint16_t value) {
    sp--;
    WriteByte(sp, value >> 8);
    sp--;
    WriteByte(sp, value & 0xFF);
}

uint16_t CPU::Pop() {
    uint8_t lo = ReadByte(sp);
    sp++;
    uint8_t hi = ReadByte(sp);
    sp++;
    return (static_cast<uint16_t>(hi) << 8) | lo;
}

void CPU::FlushPendingCycles() {
    if (pending_cycles > 0 && tick_callback) {
        tick_callback(pending_cycles);
    }
    pending_cycles = 0;
}

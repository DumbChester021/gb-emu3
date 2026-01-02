#include "Emulator.hpp"

#include "cpu/CPU.hpp"
#include "cpu/InterruptController.hpp"
#include "ppu/PPU.hpp"
#include "apu/APU.hpp"
#include "timer/Timer.hpp"
#include "input/Joypad.hpp"
#include "serial/Serial.hpp"
#include "memory/Bus.hpp"
#include "memory/Memory.hpp"
#include "memory/DMA.hpp"
#include "memory/BootROM.hpp"
#include "cartridge/Cartridge.hpp"

Emulator::Emulator()
    : cpu(std::make_unique<CPU>())
    , ppu(std::make_unique<PPU>())
    , apu(std::make_unique<APU>())
    , timer(std::make_unique<Timer>())
    , joypad(std::make_unique<Joypad>())
    , serial(std::make_unique<Serial>())
    , bus(std::make_unique<Bus>())
    , memory(std::make_unique<Memory>())
    , dma(std::make_unique<DMA>())
    , cartridge(std::make_unique<Cartridge>())
    , interrupts(std::make_unique<InterruptController>())
    , bootrom(std::make_unique<BootROM>())
    , total_cycles(0)
{
    WireComponents();
}

Emulator::~Emulator() = default;

/**
 * Wire Components Together
 * 
 * This is like soldering the PCB traces that connect all chips.
 * Each component exposes its pins, and we connect them through the Bus.
 */
void Emulator::WireComponents() {
    // === Connect Cartridge ($0000-$7FFF ROM, $A000-$BFFF RAM) ===
    bus->ConnectCartridge(
        [this](uint16_t addr) { return cartridge->Read(addr); },
        [this](uint16_t addr, uint8_t val) { cartridge->Write(addr, val); }
    );
    
    // === Connect Boot ROM ($0000-$00FF when enabled) ===
    bus->ConnectBootROM(
        [this](uint16_t addr) { return bootrom->Read(addr); }
    );
    
    // === Connect VRAM ($8000-$9FFF) ===
    bus->ConnectVRAM(
        [this](uint16_t addr) { return ppu->ReadVRAM(addr); },
        [this](uint16_t addr, uint8_t val) { ppu->WriteVRAM(addr, val); }
    );
    
    // === Connect WRAM ($C000-$DFFF, echo $E000-$FDFF) ===
    bus->ConnectWRAM(
        [this](uint16_t addr) { return memory->ReadWRAM(addr); },
        [this](uint16_t addr, uint8_t val) { memory->WriteWRAM(addr, val); }
    );
    
    // === Connect OAM ($FE00-$FE9F) ===
    bus->ConnectOAM(
        [this](uint16_t addr) { return ppu->ReadOAM(addr); },
        [this](uint16_t addr, uint8_t val) { ppu->WriteOAM(addr, val); }
    );
    
    // === Connect I/O Registers ($FF00-$FF7F) ===
    bus->ConnectIO(
        [this](uint16_t addr) { return ReadIO(addr); },
        [this](uint16_t addr, uint8_t val) { WriteIO(addr, val); }
    );
    
    // === Connect HRAM ($FF80-$FFFE) ===
    bus->ConnectHRAM(
        [this](uint16_t addr) { return memory->ReadHRAM(addr); },
        [this](uint16_t addr, uint8_t val) { memory->WriteHRAM(addr, val); }
    );
    
    // === Connect IE Register ($FFFF) ===
    bus->ConnectIE(
        [this](uint16_t) { return interrupts->ReadIE(); },
        [this](uint16_t, uint8_t val) { interrupts->WriteIE(val); }
    );
    
    // === Connect DMA (for bus conflict and OAM blocking) ===
    // - IsActive: For bus conflict detection
    // - IsBlockingOAM: For OAM blocking (stricter timing, blocks during startup)
    // - GetSourceAddress: For determining which bus DMA is using
    bus->ConnectDMA(
        [this]() { return dma->IsActive(); },
        [this]() { return dma->IsBlockingOAM(); },
        [this]() { return dma->GetSourceAddress(); }
    );
    
    // === Connect CPU to Bus (wire the address/data lines) ===
    cpu->ConnectBus(
        [this](uint16_t addr) { return bus->Read(addr); },
        [this](uint16_t addr, uint8_t val) { bus->Write(addr, val); },
        [this](uint8_t cycles) { TickComponents(cycles); }  // Tick per M-cycle
    );
    
    // === Connect PPU Interrupt Callback (Per SameBoy L558: immediate IF bit set) ===
    // Real hardware sets IF bit at exact cycle, not batched after M-cycle
    ppu->SetInterruptCallback([this](uint8_t bit) {
        interrupts->RequestInterrupt(bit);
    });
}

/**
 * I/O Register Router ($FF00-$FF7F)
 * 
 * This is the I/O decoder logic inside the LR35902.
 * It routes register accesses to the appropriate peripheral.
 */
uint8_t Emulator::ReadIO(uint16_t addr) {
    uint8_t result;
    switch (addr) {
        // Joypad
        case 0xFF00: result = joypad->ReadRegister(); break;
        
        // Serial
        case 0xFF01: 
        case 0xFF02: result = serial->ReadRegister(addr); break;
        
        // Timer
        case 0xFF04: 
        case 0xFF05: 
        case 0xFF06: 
        case 0xFF07: result = timer->ReadRegister(addr); break;
        
        // Interrupt Flag
        case 0xFF0F: result = interrupts->ReadIF(); break;
        
        // APU (NR10-NR52 including unused registers)
        case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:  // CH1
        case 0xFF15:                                                       // Unused
        case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:               // CH2
        case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:  // CH3
        case 0xFF1F:                                                       // Unused
        case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:               // CH4
        case 0xFF24: case 0xFF25: case 0xFF26:                            // Control
        case 0xFF27: case 0xFF28: case 0xFF29: case 0xFF2A:               // Unused
        case 0xFF2B: case 0xFF2C: case 0xFF2D: case 0xFF2E: case 0xFF2F:  // Unused
            result = apu->ReadRegister(addr); break;
        
        // Wave RAM
        case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
        case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
        case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
        case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
            result = apu->ReadWaveRAM(addr - 0xFF30); break;
        
        // PPU Registers
        case 0xFF40: case 0xFF41: case 0xFF42: case 0xFF43:
        case 0xFF44: case 0xFF45: case 0xFF47: case 0xFF48:
        case 0xFF49: case 0xFF4A: case 0xFF4B:
            result = ppu->ReadRegister(addr); break;
        
        // DMA Register
        case 0xFF46: result = dma->ReadRegister(); break;
        
        // Boot ROM disable
        case 0xFF50: result = bootrom->IsEnabled() ? 0x00 : 0xFF; break;
        
        default:
            // Unmapped I/O returns $FF
            result = 0xFF; break;
    }
    
    return result;
}

void Emulator::WriteIO(uint16_t addr, uint8_t value) {
    // Just remove debug for now
    switch (addr) {
        // Joypad
        case 0xFF00: joypad->WriteRegister(value); break;
        
        // Serial
        case 0xFF01:
        case 0xFF02: serial->WriteRegister(addr, value); break;
        
        // Timer
        case 0xFF04:
        case 0xFF05:
        case 0xFF06:
        case 0xFF07: timer->WriteRegister(addr, value); break;
        
        // Interrupt Flag
        case 0xFF0F: interrupts->WriteIF(value); break;
        
        // APU (NR10-NR52 including unused registers)
        case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
        case 0xFF15:  // Unused
        case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
        case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
        case 0xFF1F:  // Unused
        case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
        case 0xFF24: case 0xFF25: case 0xFF26:
        case 0xFF27: case 0xFF28: case 0xFF29: case 0xFF2A:  // Unused
        case 0xFF2B: case 0xFF2C: case 0xFF2D: case 0xFF2E: case 0xFF2F:  // Unused
            // Update DIV bit 12 signal to APU (for skip_div_event glitch on power-on)
            apu->SetDivBit12High((timer->GetDIVCounter() & 0x1000) != 0);
            apu->WriteRegister(addr, value);
            break;
        
        // Wave RAM
        case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
        case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
        case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
        case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
            apu->WriteWaveRAM(addr - 0xFF30, value);
            break;
        
        // PPU Registers
        case 0xFF40: case 0xFF41: case 0xFF42: case 0xFF43:
        case 0xFF44: case 0xFF45: case 0xFF47: case 0xFF48:
        case 0xFF49: case 0xFF4A: case 0xFF4B:
            ppu->WriteRegister(addr, value);
            break;
        
        // DMA Register - triggers OAM DMA
        case 0xFF46: dma->WriteRegister(value); break;
        
        // Boot ROM disable
        case 0xFF50:
            if (value != 0) {
                bootrom->SetEnabled(false);
                bus->SetBootROMEnabled(false);
            }
            break;
        
        default:
            // Unmapped I/O writes are ignored
            break;
    }
}

// === Initialization ===

bool Emulator::LoadROM(const std::string& path) {
    return cartridge->LoadROM(path);
}

bool Emulator::LoadBootROM(const std::string& path) {
    if (bootrom->Load(path)) {
        bootrom->SetEnabled(true);
        bus->SetBootROMEnabled(true);
        return true;
    }
    return false;
}

bool Emulator::LoadSave(const std::string& path) {
    return cartridge->LoadSave(path);
}

bool Emulator::SaveRAM(const std::string& path) const {
    return cartridge->SaveRAM(path);
}

bool Emulator::HasBattery() const {
    return cartridge->HasBattery();
}

void Emulator::Reset() {
    // Reset CPU with proper boot ROM state
    // If boot ROM is enabled, PC starts at 0; otherwise PC starts at $0100
    cpu->Reset(bootrom->IsEnabled());
    ppu->Reset(bootrom->IsEnabled());  // LCD disabled when boot ROM runs
    apu->Reset();
    timer->Reset();
    joypad->Reset();
    serial->Reset();
    memory->Reset();
    dma->Reset();
    interrupts->Reset();
    
    total_cycles = 0;
    
    // If no boot ROM, start from $0100 with post-boot state
    if (!bootrom->IsEnabled()) {
        bus->SetBootROMEnabled(false);
    }
}

// === Clock Distribution ===

/**
 * Step - Execute one CPU instruction
 * 
 * In real hardware, the CPU doesn't "step" - everything runs in parallel.
 * But for emulation, we execute one instruction then sync other components.
 * 
 * This matches the instruction-level timing of real hardware.
 */
uint8_t Emulator::Step() {
    // Execute one CPU instruction
    // Components tick during each M-cycle via tick_callback (hardware accurate)
    uint8_t cycles = cpu->Step();
    
    total_cycles += cycles;
    
    return cycles;
}

/**
 * Tick Components - Synchronize all hardware
 * 
 * This simulates how all components receive the same clock signal
 * and advance in parallel.
 */
void Emulator::TickComponents(uint8_t cycles) {
    // Process OAM DMA (highest priority, happens each cycle)
    ProcessDMA(cycles);
    
    // Step PPU (runs every T-cycle in real hardware)
    ppu->Step(cycles);
    
    // Step Timer
    timer->Step(cycles);
    
    // Check if DIV bit 12 fell (512 Hz signal for APU frame sequencer)
    // Per SameBoy: apu_bit = 0x1000 for normal speed DMG
    if (timer->DidDivBit12Fall()) {
        apu->ClockFrameSequencer();
        timer->ClearDivBit12Fall();
    }
    
    // Step APU
    apu->Step(cycles);
    
    // Step Serial
    serial->Step(cycles);
    
    // Route interrupt signals from peripherals to interrupt controller
    UpdateInterrupts();
}

/**
 * Update Interrupts - Route interrupt signals
 * 
 * In real hardware, interrupt lines are directly wired from peripherals
 * to the interrupt controller. We simulate this by checking each peripheral's
 * interrupt output and setting the corresponding IF bit.
 */
void Emulator::UpdateInterrupts() {
    // VBlank interrupt from PPU
    if (ppu->IsVBlankInterruptRequested()) {
        interrupts->RequestInterrupt(InterruptController::VBLANK);
        ppu->ClearVBlankInterrupt();
    }
    
    // STAT interrupt from PPU
    if (ppu->IsStatInterruptRequested()) {
        interrupts->RequestInterrupt(InterruptController::STAT);
        ppu->ClearStatInterrupt();
    }
    
    // Timer interrupt
    if (timer->IsInterruptRequested()) {
        interrupts->RequestInterrupt(InterruptController::TIMER);
        timer->ClearInterrupt();
    }
    
    // Serial interrupt
    if (serial->IsInterruptRequested()) {
        interrupts->RequestInterrupt(InterruptController::SERIAL);
        serial->ClearInterrupt();
    }
    
    // Joypad interrupt
    if (joypad->IsInterruptRequested()) {
        interrupts->RequestInterrupt(InterruptController::JOYPAD);
        joypad->ClearInterrupt();
    }
}

/**
 * Process DMA - Handle OAM DMA transfers
 * 
 * OAM DMA transfers one byte per M-cycle (4 T-cycles).
 * During DMA, CPU can only access HRAM.
 */
void Emulator::ProcessDMA(uint8_t cycles) {
    if (!dma->IsActive()) return;
    
    // DMA transfers during these cycles
    if (dma->Step(cycles)) {
        // Read from source address via bus
        uint16_t src = dma->GetSourceAddress();
        uint8_t data = bus->DMARead(src);
        
        // Write directly to OAM (bypassing normal access)
        ppu->DMAWriteOAM(dma->GetOAMIndex(), data);
        
        // Advance to next byte
        dma->AcknowledgeTransfer();
    }
}

void Emulator::StepCycles(uint32_t cycles) {
    uint32_t executed = 0;
    while (executed < cycles) {
        executed += Step();
    }
}

void Emulator::RunFrame() {
    // One frame = 70224 T-cycles (154 scanlines * 456 dots)
    constexpr uint32_t FRAME_CYCLES = 70224;
    
    ppu->ClearFrameComplete();
    uint32_t cycles_this_frame = 0;
    
    // Run until frame complete OR we've run enough cycles
    // The cycle count is needed when LCD is disabled (e.g., during boot ROM)
    while (!ppu->IsFrameComplete() && cycles_this_frame < FRAME_CYCLES) {
        cycles_this_frame += Step();
    }
}

// === Hardware Signal Access ===

const uint8_t* Emulator::GetFramebuffer() const {
    return ppu->GetFramebuffer().data();
}

bool Emulator::IsFrameComplete() const {
    return ppu->IsFrameComplete();
}

void Emulator::ClearFrameComplete() {
    ppu->ClearFrameComplete();
}

void Emulator::GetAudioSample(float& left, float& right) const {
    apu->GetSample(left, right);
}

bool Emulator::HasAudioSample() const {
    return apu->HasSample();
}

void Emulator::ClearAudioSample() {
    apu->ClearSampleReady();
}

void Emulator::ConnectAudioBuffer(AudioBuffer* buffer) {
    apu->SetAudioBuffer(buffer);
}

void Emulator::SetButton(uint8_t button, bool pressed) {
    joypad->SetButton(button, pressed);
}

bool Emulator::GetSerialOut() const {
    return serial->GetSerialOut();
}

void Emulator::SetSerialIn(bool value) {
    serial->SetSerialIn(value);
}

uint8_t Emulator::GetSerialData() const {
    return serial->GetTransferData();
}

bool Emulator::IsSerialTransferComplete() const {
    return serial->IsTransferComplete();
}

void Emulator::ClearSerialTransferComplete() {
    serial->ClearTransferComplete();
}

// === Debug Access ===

uint16_t Emulator::GetPC() const { return cpu->GetPC(); }
uint16_t Emulator::GetSP() const { return cpu->GetSP(); }
uint16_t Emulator::GetAF() const { return cpu->GetAF(); }
uint16_t Emulator::GetBC() const { return cpu->GetBC(); }
uint16_t Emulator::GetDE() const { return cpu->GetDE(); }
uint16_t Emulator::GetHL() const { return cpu->GetHL(); }
uint8_t Emulator::GetPPUMode() const { return ppu->GetMode(); }
uint8_t Emulator::GetLY() const { return ppu->GetLY(); }

uint8_t Emulator::DebugRead(uint16_t addr) const {
    return bus->Read(addr);
}

bool Emulator::IsBootROMActive() const {
    return bootrom->IsEnabled();
}

void Emulator::SetMooneyeCallback(std::function<void(bool)> callback) {
    cpu->SetMooneyeCallback(callback);
}

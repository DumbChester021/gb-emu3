/**
 * SM83 Instruction Implementation
 * 
 * Hardware-Accurate T-Cycle Timing
 * 
 * Key Principles:
 * 1. Each memory access (fetch, read, write) = 4 T-cycles
 * 2. Instruction timing = sum of all memory accesses
 * 3. ALU operations happen during memory access cycles (no extra time)
 * 4. Conditional branches: shorter if not taken
 * 
 * Register Encoding (for opcodes):
 * 0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A
 * 
 * 16-bit register encoding:
 * 0=BC, 1=DE, 2=HL, 3=SP (or AF for PUSH/POP)
 * 
 * Condition codes:
 * 0=NZ, 1=Z, 2=NC, 3=C
 */

#include "Instructions.hpp"
#include "CPU.hpp"

// === Helper: Read/Write 8-bit register by index ===
static uint8_t GetReg8(CPU& cpu, uint8_t reg) {
    switch (reg) {
        case 0: return cpu.GetB();
        case 1: return cpu.GetC();
        case 2: return cpu.GetD();
        case 3: return cpu.GetE();
        case 4: return cpu.GetH();
        case 5: return cpu.GetL();
        case 6: return cpu.ReadByte(cpu.GetHL());  // (HL) - adds 4 T-cycles
        case 7: return cpu.GetA();
        default: return 0;
    }
}

static void SetReg8(CPU& cpu, uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0: cpu.SetB(value); break;
        case 1: cpu.SetC(value); break;
        case 2: cpu.SetD(value); break;
        case 3: cpu.SetE(value); break;
        case 4: cpu.SetH(value); break;
        case 5: cpu.SetL(value); break;
        case 6: cpu.WriteByte(cpu.GetHL(), value); break;  // (HL) - adds 4 T-cycles
        case 7: cpu.SetA(value); break;
    }
}

// === Helper: Read/Write 16-bit register by index ===
static uint16_t GetReg16(CPU& cpu, uint8_t reg) {
    switch (reg) {
        case 0: return cpu.GetBC();
        case 1: return cpu.GetDE();
        case 2: return cpu.GetHL();
        case 3: return cpu.GetSP();
        default: return 0;
    }
}

static void SetReg16(CPU& cpu, uint8_t reg, uint16_t value) {
    switch (reg) {
        case 0: cpu.SetBC(value); break;
        case 1: cpu.SetDE(value); break;
        case 2: cpu.SetHL(value); break;
        case 3: cpu.SetSP(value); break;
    }
}

// For PUSH/POP, index 3 = AF
static uint16_t GetReg16AF(CPU& cpu, uint8_t reg) {
    switch (reg) {
        case 0: return cpu.GetBC();
        case 1: return cpu.GetDE();
        case 2: return cpu.GetHL();
        case 3: return cpu.GetAF();
        default: return 0;
    }
}

static void SetReg16AF(CPU& cpu, uint8_t reg, uint16_t value) {
    switch (reg) {
        case 0: cpu.SetBC(value); break;
        case 1: cpu.SetDE(value); break;
        case 2: cpu.SetHL(value); break;
        case 3: cpu.SetAF(value); break;
    }
}

// === Helper: Check condition ===
static bool CheckCondition(CPU& cpu, uint8_t cc) {
    switch (cc) {
        case 0: return !cpu.GetFlagZ();  // NZ
        case 1: return cpu.GetFlagZ();   // Z
        case 2: return !cpu.GetFlagC();  // NC
        case 3: return cpu.GetFlagC();   // C
        default: return false;
    }
}

// =============================================================================
// 8-BIT LOAD INSTRUCTIONS
// =============================================================================

// LD r,r' - 4 T-cycles (1 fetch)
uint8_t LD_r_r(CPU& cpu, uint8_t dest, uint8_t src) {
    uint8_t value = GetReg8(cpu, src);
    SetReg8(cpu, dest, value);
    // If either is (HL), timing is already included
    return 4;
}

// LD r,n - 8 T-cycles (2 fetches)
uint8_t LD_r_n(CPU& cpu, uint8_t dest) {
    uint8_t n = cpu.FetchByte();
    SetReg8(cpu, dest, n);
    return 8;
}

// LD r,(HL) - 8 T-cycles (1 fetch + 1 read)
uint8_t LD_r_HL(CPU& cpu, uint8_t dest) {
    uint8_t value = cpu.ReadByte(cpu.GetHL());
    SetReg8(cpu, dest, value);
    return 8;
}

// LD (HL),r - 8 T-cycles (1 fetch + 1 write)
uint8_t LD_HL_r(CPU& cpu, uint8_t src) {
    cpu.WriteByte(cpu.GetHL(), GetReg8(cpu, src));
    return 8;
}

// LD (HL),n - 12 T-cycles (2 fetches + 1 write)
uint8_t LD_HL_n(CPU& cpu) {
    uint8_t n = cpu.FetchByte();
    cpu.WriteByte(cpu.GetHL(), n);
    return 12;
}

// LD A,(BC) - 8 T-cycles
uint8_t LD_A_BC(CPU& cpu) {
    cpu.SetA(cpu.ReadByte(cpu.GetBC()));
    return 8;
}

// LD A,(DE) - 8 T-cycles
uint8_t LD_A_DE(CPU& cpu) {
    cpu.SetA(cpu.ReadByte(cpu.GetDE()));
    return 8;
}

// LD A,(nn) - 16 T-cycles (3 fetches + 1 read)
uint8_t LD_A_nn(CPU& cpu) {
    uint16_t addr = cpu.FetchWord();
    cpu.SetA(cpu.ReadByte(addr));
    return 16;
}

// LD (BC),A - 8 T-cycles
uint8_t LD_BC_A(CPU& cpu) {
    cpu.WriteByte(cpu.GetBC(), cpu.GetA());
    return 8;
}

// LD (DE),A - 8 T-cycles
uint8_t LD_DE_A(CPU& cpu) {
    cpu.WriteByte(cpu.GetDE(), cpu.GetA());
    return 8;
}

// LD (nn),A - 16 T-cycles
uint8_t LD_nn_A(CPU& cpu) {
    uint16_t addr = cpu.FetchWord();
    cpu.WriteByte(addr, cpu.GetA());
    return 16;
}

// LDH A,(n) - 12 T-cycles (2 fetches + 1 read)
uint8_t LDH_A_n(CPU& cpu) {
    uint8_t n = cpu.FetchByte();
    cpu.SetA(cpu.ReadByte(0xFF00 + n));
    return 12;
}

// LDH (n),A - 12 T-cycles
uint8_t LDH_n_A(CPU& cpu) {
    uint8_t n = cpu.FetchByte();
    cpu.WriteByte(0xFF00 + n, cpu.GetA());
    return 12;
}

// LDH A,(C) - 8 T-cycles
uint8_t LDH_A_C(CPU& cpu) {
    cpu.SetA(cpu.ReadByte(0xFF00 + cpu.GetC()));
    return 8;
}

// LDH (C),A - 8 T-cycles
uint8_t LDH_C_A(CPU& cpu) {
    cpu.WriteByte(0xFF00 + cpu.GetC(), cpu.GetA());
    return 8;
}

// LD A,(HL+) - 8 T-cycles
uint8_t LD_A_HLI(CPU& cpu) {
    cpu.SetA(cpu.ReadByte(cpu.GetHL()));
    cpu.SetHL(cpu.GetHL() + 1);
    return 8;
}

// LD A,(HL-) - 8 T-cycles
uint8_t LD_A_HLD(CPU& cpu) {
    cpu.SetA(cpu.ReadByte(cpu.GetHL()));
    cpu.SetHL(cpu.GetHL() - 1);
    return 8;
}

// LD (HL+),A - 8 T-cycles
uint8_t LD_HLI_A(CPU& cpu) {
    cpu.WriteByte(cpu.GetHL(), cpu.GetA());
    cpu.SetHL(cpu.GetHL() + 1);
    return 8;
}

// LD (HL-),A - 8 T-cycles
uint8_t LD_HLD_A(CPU& cpu) {
    cpu.WriteByte(cpu.GetHL(), cpu.GetA());
    cpu.SetHL(cpu.GetHL() - 1);
    return 8;
}

// =============================================================================
// 16-BIT LOAD INSTRUCTIONS
// =============================================================================

// LD rr,nn - 12 T-cycles (3 fetches)
uint8_t LD_rr_nn(CPU& cpu, uint8_t reg) {
    uint16_t nn = cpu.FetchWord();
    SetReg16(cpu, reg, nn);
    return 12;
}

// LD (nn),SP - 20 T-cycles (3 fetches + 2 writes)
uint8_t LD_nn_SP(CPU& cpu) {
    uint16_t addr = cpu.FetchWord();
    cpu.WriteByte(addr, cpu.GetSP() & 0xFF);
    cpu.WriteByte(addr + 1, cpu.GetSP() >> 8);
    return 20;
}

// LD SP,HL - 8 T-cycles (1 fetch + 1 internal)
uint8_t LD_SP_HL(CPU& cpu) {
    cpu.SetSP(cpu.GetHL());
    cpu.InternalDelay();  // 4T internal
    return 8;
}

// LD HL,SP+n - 12 T-cycles (2 fetches + 1 internal)
uint8_t LD_HL_SP_n(CPU& cpu) {
    int8_t n = static_cast<int8_t>(cpu.FetchByte());  // 4T
    uint16_t sp = cpu.GetSP();
    uint32_t result = sp + n;
    
    cpu.SetHL(result & 0xFFFF);
    
    // Flags: use lower 8 bits for H and C
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(((sp & 0x0F) + (n & 0x0F)) > 0x0F);
    cpu.SetFlagC(((sp & 0xFF) + (n & 0xFF)) > 0xFF);
    cpu.InternalDelay();  // 4T internal
    
    return 12;
}

// PUSH rr - 16 T-cycles (1 fetch + 1 internal + 2 writes)
uint8_t PUSH_rr(CPU& cpu, uint8_t reg) {
    uint16_t value = GetReg16AF(cpu, reg);
    cpu.InternalDelay();                    // 4T internal
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), value >> 8);   // 4T
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), value & 0xFF); // 4T
    return 16;
}

// POP rr - 12 T-cycles (1 fetch + 2 reads)
uint8_t POP_rr(CPU& cpu, uint8_t reg) {
    uint8_t lo = cpu.ReadByte(cpu.GetSP());
    cpu.SetSP(cpu.GetSP() + 1);
    uint8_t hi = cpu.ReadByte(cpu.GetSP());
    cpu.SetSP(cpu.GetSP() + 1);
    SetReg16AF(cpu, reg, (static_cast<uint16_t>(hi) << 8) | lo);
    return 12;
}

// =============================================================================
// 8-BIT ALU INSTRUCTIONS
// =============================================================================

// ADD A,r - 4 T-cycles
uint8_t ADD_A_r(CPU& cpu, uint8_t src) {
    uint8_t a = cpu.GetA();
    uint8_t value = GetReg8(cpu, src);
    uint16_t result = a + value;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH((a & 0x0F) + (value & 0x0F) > 0x0F);
    cpu.SetFlagC(result > 0xFF);
    
    return (src == 6) ? 8 : 4;  // (HL) takes extra 4 T-cycles
}

// ADD A,(HL) - 8 T-cycles
uint8_t ADD_A_HL(CPU& cpu) {
    return ADD_A_r(cpu, 6);
}

// ADD A,n - 8 T-cycles
uint8_t ADD_A_n(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t n = cpu.FetchByte();
    uint16_t result = a + n;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH((a & 0x0F) + (n & 0x0F) > 0x0F);
    cpu.SetFlagC(result > 0xFF);
    
    return 8;
}

// ADC A,r
uint8_t ADC_A_r(CPU& cpu, uint8_t src) {
    uint8_t a = cpu.GetA();
    uint8_t value = GetReg8(cpu, src);
    uint8_t carry = cpu.GetFlagC() ? 1 : 0;
    uint16_t result = a + value + carry;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH((a & 0x0F) + (value & 0x0F) + carry > 0x0F);
    cpu.SetFlagC(result > 0xFF);
    
    return (src == 6) ? 8 : 4;
}

// ADC A,(HL) - 8 T-cycles
uint8_t ADC_A_HL(CPU& cpu) {
    return ADC_A_r(cpu, 6);
}

// ADC A,n - 8 T-cycles
uint8_t ADC_A_n(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t n = cpu.FetchByte();
    uint8_t carry = cpu.GetFlagC() ? 1 : 0;
    uint16_t result = a + n + carry;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH((a & 0x0F) + (n & 0x0F) + carry > 0x0F);
    cpu.SetFlagC(result > 0xFF);
    
    return 8;
}

// SUB r
uint8_t SUB_r(CPU& cpu, uint8_t src) {
    uint8_t a = cpu.GetA();
    uint8_t value = GetReg8(cpu, src);
    uint8_t result = a - value;
    
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (value & 0x0F));
    cpu.SetFlagC(a < value);
    
    return (src == 6) ? 8 : 4;
}

// SUB (HL)
uint8_t SUB_HL(CPU& cpu) {
    return SUB_r(cpu, 6);
}

// SUB n
uint8_t SUB_n(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t n = cpu.FetchByte();
    uint8_t result = a - n;
    
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (n & 0x0F));
    cpu.SetFlagC(a < n);
    
    return 8;
}

// SBC A,r
uint8_t SBC_A_r(CPU& cpu, uint8_t src) {
    uint8_t a = cpu.GetA();
    uint8_t value = GetReg8(cpu, src);
    uint8_t carry = cpu.GetFlagC() ? 1 : 0;
    int result = a - value - carry;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (value & 0x0F) + carry);
    cpu.SetFlagC(result < 0);
    
    return (src == 6) ? 8 : 4;
}

// SBC A,(HL)
uint8_t SBC_A_HL(CPU& cpu) {
    return SBC_A_r(cpu, 6);
}

// SBC A,n
uint8_t SBC_A_n(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t n = cpu.FetchByte();
    uint8_t carry = cpu.GetFlagC() ? 1 : 0;
    int result = a - n - carry;
    
    cpu.SetA(result & 0xFF);
    cpu.SetFlagZ((result & 0xFF) == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (n & 0x0F) + carry);
    cpu.SetFlagC(result < 0);
    
    return 8;
}

// AND r
uint8_t AND_r(CPU& cpu, uint8_t src) {
    uint8_t result = cpu.GetA() & GetReg8(cpu, src);
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(true);
    cpu.SetFlagC(false);
    return (src == 6) ? 8 : 4;
}

// AND (HL)
uint8_t AND_HL(CPU& cpu) {
    return AND_r(cpu, 6);
}

// AND n
uint8_t AND_n(CPU& cpu) {
    uint8_t result = cpu.GetA() & cpu.FetchByte();
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(true);
    cpu.SetFlagC(false);
    return 8;
}

// XOR r
uint8_t XOR_r(CPU& cpu, uint8_t src) {
    uint8_t result = cpu.GetA() ^ GetReg8(cpu, src);
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(false);
    return (src == 6) ? 8 : 4;
}

// XOR (HL)
uint8_t XOR_HL(CPU& cpu) {
    return XOR_r(cpu, 6);
}

// XOR n
uint8_t XOR_n(CPU& cpu) {
    uint8_t result = cpu.GetA() ^ cpu.FetchByte();
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(false);
    return 8;
}

// OR r
uint8_t OR_r(CPU& cpu, uint8_t src) {
    uint8_t result = cpu.GetA() | GetReg8(cpu, src);
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(false);
    return (src == 6) ? 8 : 4;
}

// OR (HL)
uint8_t OR_HL(CPU& cpu) {
    return OR_r(cpu, 6);
}

// OR n
uint8_t OR_n(CPU& cpu) {
    uint8_t result = cpu.GetA() | cpu.FetchByte();
    cpu.SetA(result);
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(false);
    return 8;
}

// CP r - compare (SUB without storing result)
uint8_t CP_r(CPU& cpu, uint8_t src) {
    uint8_t a = cpu.GetA();
    uint8_t value = GetReg8(cpu, src);
    uint8_t result = a - value;
    
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (value & 0x0F));
    cpu.SetFlagC(a < value);
    
    return (src == 6) ? 8 : 4;
}

// CP (HL)
uint8_t CP_HL(CPU& cpu) {
    return CP_r(cpu, 6);
}

// CP n
uint8_t CP_n(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t n = cpu.FetchByte();
    uint8_t result = a - n;
    
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((a & 0x0F) < (n & 0x0F));
    cpu.SetFlagC(a < n);
    
    return 8;
}

// INC r
uint8_t INC_r(CPU& cpu, uint8_t reg) {
    uint8_t value = GetReg8(cpu, reg);
    uint8_t result = value + 1;
    SetReg8(cpu, reg, result);
    
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH((value & 0x0F) == 0x0F);
    // C not affected
    
    return (reg == 6) ? 12 : 4;  // (HL) = read + write + fetch
}

// INC (HL) - 12 T-cycles
uint8_t INC_HL(CPU& cpu) {
    return INC_r(cpu, 6);
}

// DEC r
uint8_t DEC_r(CPU& cpu, uint8_t reg) {
    uint8_t value = GetReg8(cpu, reg);
    uint8_t result = value - 1;
    SetReg8(cpu, reg, result);
    
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(true);
    cpu.SetFlagH((value & 0x0F) == 0x00);
    // C not affected
    
    return (reg == 6) ? 12 : 4;
}

// DEC (HL) - 12 T-cycles
uint8_t DEC_HL(CPU& cpu) {
    return DEC_r(cpu, 6);
}

// DAA - Decimal Adjust Accumulator
uint8_t DAA(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t correction = 0;
    bool setC = false;
    
    if (cpu.GetFlagH() || (!cpu.GetFlagN() && (a & 0x0F) > 9)) {
        correction |= 0x06;
    }
    
    if (cpu.GetFlagC() || (!cpu.GetFlagN() && a > 0x99)) {
        correction |= 0x60;
        setC = true;
    }
    
    if (cpu.GetFlagN()) {
        a -= correction;
    } else {
        a += correction;
    }
    
    cpu.SetA(a);
    cpu.SetFlagZ(a == 0);
    cpu.SetFlagH(false);
    cpu.SetFlagC(setC);
    
    return 4;
}

// CPL - Complement A
uint8_t CPL(CPU& cpu) {
    cpu.SetA(~cpu.GetA());
    cpu.SetFlagN(true);
    cpu.SetFlagH(true);
    return 4;
}

// SCF - Set Carry Flag
uint8_t SCF(CPU& cpu) {
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(true);
    return 4;
}

// CCF - Complement Carry Flag
uint8_t CCF(CPU& cpu) {
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(!cpu.GetFlagC());
    return 4;
}

// =============================================================================
// 16-BIT ALU INSTRUCTIONS
// =============================================================================

// ADD HL,rr - 8 T-cycles (1 fetch + 1 internal)
uint8_t ADD_HL_rr(CPU& cpu, uint8_t reg) {
    uint16_t hl = cpu.GetHL();
    uint16_t value = GetReg16(cpu, reg);
    uint32_t result = hl + value;
    
    cpu.SetHL(result & 0xFFFF);
    cpu.SetFlagN(false);
    cpu.SetFlagH((hl & 0x0FFF) + (value & 0x0FFF) > 0x0FFF);
    cpu.SetFlagC(result > 0xFFFF);
    cpu.InternalDelay();  // 4T internal
    
    return 8;
}

// ADD SP,n - 16 T-cycles (2 fetches + 2 internal)
uint8_t ADD_SP_n(CPU& cpu) {
    int8_t n = static_cast<int8_t>(cpu.FetchByte());  // 4T
    uint16_t sp = cpu.GetSP();
    uint32_t result = sp + n;
    
    cpu.InternalDelay();  // 4T internal
    cpu.InternalDelay();  // 4T internal
    cpu.SetSP(result & 0xFFFF);
    
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(((sp & 0x0F) + (n & 0x0F)) > 0x0F);
    cpu.SetFlagC(((sp & 0xFF) + (n & 0xFF)) > 0xFF);
    
    return 16;
}

// INC rr - 8 T-cycles (1 fetch + 1 internal)
uint8_t INC_rr(CPU& cpu, uint8_t reg) {
    SetReg16(cpu, reg, GetReg16(cpu, reg) + 1);
    cpu.InternalDelay();  // 4T internal
    return 8;
}

// DEC rr - 8 T-cycles (1 fetch + 1 internal)
uint8_t DEC_rr(CPU& cpu, uint8_t reg) {
    SetReg16(cpu, reg, GetReg16(cpu, reg) - 1);
    cpu.InternalDelay();  // 4T internal
    return 8;
}

// =============================================================================
// ROTATE/SHIFT (Non-CB prefix)
// =============================================================================

// RLCA - Rotate Left Circular A
uint8_t RLCA(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t bit7 = (a >> 7) & 1;
    a = (a << 1) | bit7;
    cpu.SetA(a);
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(bit7);
    return 4;
}

// RLA - Rotate Left through carry A  
uint8_t RLA(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t bit7 = (a >> 7) & 1;
    a = (a << 1) | (cpu.GetFlagC() ? 1 : 0);
    cpu.SetA(a);
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(bit7);
    return 4;
}

// RRCA - Rotate Right Circular A
uint8_t RRCA(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t bit0 = a & 1;
    a = (a >> 1) | (bit0 << 7);
    cpu.SetA(a);
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(bit0);
    return 4;
}

// RRA - Rotate Right through carry A
uint8_t RRA(CPU& cpu) {
    uint8_t a = cpu.GetA();
    uint8_t bit0 = a & 1;
    a = (a >> 1) | (cpu.GetFlagC() ? 0x80 : 0);
    cpu.SetA(a);
    cpu.SetFlagZ(false);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(bit0);
    return 4;
}

// =============================================================================
// CONTROL FLOW
// =============================================================================

// JP nn - 16 T-cycles (3 fetches + 1 internal)
uint8_t JP_nn(CPU& cpu) {
    uint16_t addr = cpu.FetchWord();  // 8T
    cpu.InternalDelay();               // 4T internal
    cpu.SetPC(addr);
    return 16;
}

// JP cc,nn - 16/12 T-cycles
uint8_t JP_cc_nn(CPU& cpu, uint8_t cc) {
    uint16_t addr = cpu.FetchWord();  // 8T
    if (CheckCondition(cpu, cc)) {
        cpu.InternalDelay();           // 4T internal (only when taken)
        cpu.SetPC(addr);
        return 16;
    }
    return 12;
}

// JP (HL) - 4 T-cycles
uint8_t JP_HL(CPU& cpu) {
    cpu.SetPC(cpu.GetHL());
    return 4;
}

// JR n - 12 T-cycles (2 fetches + 1 internal)
uint8_t JR_n(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.FetchByte());  // 4T
    cpu.InternalDelay();                                    // 4T internal
    cpu.SetPC(cpu.GetPC() + offset);
    return 12;
}

// JR cc,n - 12/8 T-cycles
uint8_t JR_cc_n(CPU& cpu, uint8_t cc) {
    int8_t offset = static_cast<int8_t>(cpu.FetchByte());  // 4T
    if (CheckCondition(cpu, cc)) {
        cpu.InternalDelay();                                // 4T internal (only when taken)
        cpu.SetPC(cpu.GetPC() + offset);
        return 12;
    }
    return 8;
}

// CALL nn - 24 T-cycles (3 fetches + 1 internal + 2 writes)
uint8_t CALL_nn(CPU& cpu) {
    uint16_t addr = cpu.FetchWord();           // 8T
    cpu.InternalDelay();                        // 4T internal
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), cpu.GetPC() >> 8);   // 4T
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), cpu.GetPC() & 0xFF); // 4T
    cpu.SetPC(addr);
    return 24;
}

// CALL cc,nn - 24/12 T-cycles
uint8_t CALL_cc_nn(CPU& cpu, uint8_t cc) {
    uint16_t addr = cpu.FetchWord();           // 8T
    if (CheckCondition(cpu, cc)) {
        cpu.InternalDelay();                    // 4T internal (only when taken)
        cpu.SetSP(cpu.GetSP() - 1);
        cpu.WriteByte(cpu.GetSP(), cpu.GetPC() >> 8);   // 4T
        cpu.SetSP(cpu.GetSP() - 1);
        cpu.WriteByte(cpu.GetSP(), cpu.GetPC() & 0xFF); // 4T
        cpu.SetPC(addr);
        return 24;
    }
    return 12;
}

// RET - 16 T-cycles (1 fetch + 2 reads + 1 internal)
uint8_t RET(CPU& cpu) {
    uint8_t lo = cpu.ReadByte(cpu.GetSP());   // 4T
    cpu.SetSP(cpu.GetSP() + 1);
    uint8_t hi = cpu.ReadByte(cpu.GetSP());   // 4T
    cpu.SetSP(cpu.GetSP() + 1);
    cpu.InternalDelay();                       // 4T internal
    cpu.SetPC((static_cast<uint16_t>(hi) << 8) | lo);
    return 16;
}

// RET cc - 20/8 T-cycles (1 fetch + 1 internal + 2 reads + 1 internal when taken)
uint8_t RET_cc(CPU& cpu, uint8_t cc) {
    cpu.InternalDelay();  // 4T internal (condition check)
    if (CheckCondition(cpu, cc)) {
        uint8_t lo = cpu.ReadByte(cpu.GetSP());   // 4T
        cpu.SetSP(cpu.GetSP() + 1);
        uint8_t hi = cpu.ReadByte(cpu.GetSP());   // 4T
        cpu.SetSP(cpu.GetSP() + 1);
        cpu.InternalDelay();                       // 4T internal
        cpu.SetPC((static_cast<uint16_t>(hi) << 8) | lo);
        return 20;
    }
    return 8;
}

// RETI - 16 T-cycles (1 fetch + 2 reads + 1 internal)
uint8_t RETI(CPU& cpu) {
    uint8_t lo = cpu.ReadByte(cpu.GetSP());   // 4T
    cpu.SetSP(cpu.GetSP() + 1);
    uint8_t hi = cpu.ReadByte(cpu.GetSP());   // 4T
    cpu.SetSP(cpu.GetSP() + 1);
    cpu.InternalDelay();                       // 4T internal
    cpu.SetPC((static_cast<uint16_t>(hi) << 8) | lo);
    cpu.SetIME(true);
    return 16;
}

// RST n - 16 T-cycles (1 fetch + 1 internal + 2 writes)
uint8_t RST(CPU& cpu, uint8_t vec) {
    cpu.InternalDelay();                               // 4T internal
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), cpu.GetPC() >> 8);      // 4T
    cpu.SetSP(cpu.GetSP() - 1);
    cpu.WriteByte(cpu.GetSP(), cpu.GetPC() & 0xFF);    // 4T
    cpu.SetPC(vec);
    return 16;
}

// =============================================================================
// MISC
// =============================================================================

// NOP - 4 T-cycles
uint8_t NOP(CPU& cpu) {
    (void)cpu;
    return 4;
}

// HALT - 4 T-cycles
// HALT bug: If IME=0 and (IE & IF) != 0, HALT exits immediately 
// but PC fails to increment for the next instruction fetch
uint8_t HALT(CPU& cpu) {
    // Check for HALT bug condition: IME=0 and pending interrupt
    if (!cpu.GetIME()) {
        // Peek IF and IE (no timing impact - internal check)
        uint8_t if_reg = cpu.PeekByte(0xFF0F);
        uint8_t ie_reg = cpu.PeekByte(0xFFFF);
        
        if ((if_reg & ie_reg & 0x1F) != 0) {
            // HALT bug: don't actually halt, but trigger the PC bug
            cpu.SetHaltBug(true);
            return 4;
        }
    }
    
    // Normal HALT behavior
    cpu.SetHalted(true);
    return 4;
}

// STOP - 4 T-cycles (TODO: proper STOP behavior)
uint8_t STOP(CPU& cpu) {
    cpu.FetchByte();  // STOP is 2-byte instruction (00 follows)
    // In real hardware, this switches speed mode on CGB
    return 4;
}

// DI - 4 T-cycles (immediate disable, cancels any scheduled EI)
uint8_t DI(CPU& cpu) {
    cpu.SetIME(false);
    cpu.CancelScheduledIME();  // Per hardware docs: DI cancels pending EI
    return 4;
}

// EI - 4 T-cycles (delayed enable - after next instruction)
uint8_t EI(CPU& cpu) {
    cpu.ScheduleIME();
    return 4;
}

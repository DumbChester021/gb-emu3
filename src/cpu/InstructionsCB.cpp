/**
 * SM83 CB-Prefix Instructions and Opcode Dispatch
 * 
 * Hardware-Accurate T-Cycle Timing
 */

#include "Instructions.hpp"
#include "CPU.hpp"

// =============================================================================
// CB-PREFIX ROTATE/SHIFT OPERATIONS
// =============================================================================

// Helper: Apply rotate/shift and set flags
static void SetRotateFlags(CPU& cpu, uint8_t result, bool carry) {
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(carry);
}

// RLC r - Rotate Left Circular
uint8_t RLC_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit7 = (value >> 7) & 1;
    uint8_t result = (value << 1) | bit7;
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit7);
    return (reg == 6) ? 16 : 8;
}

uint8_t RLC_HL(CPU& cpu) { return RLC_r(cpu, 6); }

// RRC r - Rotate Right Circular
uint8_t RRC_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit0 = value & 1;
    uint8_t result = (value >> 1) | (bit0 << 7);
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit0);
    return (reg == 6) ? 16 : 8;
}

uint8_t RRC_HL(CPU& cpu) { return RRC_r(cpu, 6); }

// RL r - Rotate Left through carry
uint8_t RL_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit7 = (value >> 7) & 1;
    uint8_t result = (value << 1) | (cpu.GetFlagC() ? 1 : 0);
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit7);
    return (reg == 6) ? 16 : 8;
}

uint8_t RL_HL(CPU& cpu) { return RL_r(cpu, 6); }

// RR r - Rotate Right through carry
uint8_t RR_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit0 = value & 1;
    uint8_t result = (value >> 1) | (cpu.GetFlagC() ? 0x80 : 0);
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit0);
    return (reg == 6) ? 16 : 8;
}

uint8_t RR_HL(CPU& cpu) { return RR_r(cpu, 6); }

// SLA r - Shift Left Arithmetic
uint8_t SLA_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit7 = (value >> 7) & 1;
    uint8_t result = value << 1;
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit7);
    return (reg == 6) ? 16 : 8;
}

uint8_t SLA_HL(CPU& cpu) { return SLA_r(cpu, 6); }

// SRA r - Shift Right Arithmetic (preserve bit 7)
uint8_t SRA_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit0 = value & 1;
    uint8_t result = (value >> 1) | (value & 0x80);  // Preserve bit 7
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit0);
    return (reg == 6) ? 16 : 8;
}

uint8_t SRA_HL(CPU& cpu) { return SRA_r(cpu, 6); }

// SWAP r - Swap nibbles
uint8_t SWAP_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t result = ((value & 0x0F) << 4) | ((value & 0xF0) >> 4);
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    cpu.SetFlagZ(result == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(false);
    cpu.SetFlagC(false);
    return (reg == 6) ? 16 : 8;
}

uint8_t SWAP_HL(CPU& cpu) { return SWAP_r(cpu, 6); }

// SRL r - Shift Right Logical
uint8_t SRL_r(CPU& cpu, uint8_t reg) {
    uint8_t value;
    if (reg == 6) {
        value = cpu.ReadByte(cpu.GetHL());
    } else {
        switch (reg) {
            case 0: value = cpu.GetB(); break;
            case 1: value = cpu.GetC(); break;
            case 2: value = cpu.GetD(); break;
            case 3: value = cpu.GetE(); break;
            case 4: value = cpu.GetH(); break;
            case 5: value = cpu.GetL(); break;
            case 7: value = cpu.GetA(); break;
            default: value = 0; break;
        }
    }
    
    uint8_t bit0 = value & 1;
    uint8_t result = value >> 1;
    
    if (reg == 6) {
        cpu.WriteByte(cpu.GetHL(), result);
    } else {
        switch (reg) {
            case 0: cpu.SetB(result); break;
            case 1: cpu.SetC(result); break;
            case 2: cpu.SetD(result); break;
            case 3: cpu.SetE(result); break;
            case 4: cpu.SetH(result); break;
            case 5: cpu.SetL(result); break;
            case 7: cpu.SetA(result); break;
        }
    }
    
    SetRotateFlags(cpu, result, bit0);
    return (reg == 6) ? 16 : 8;
}

uint8_t SRL_HL(CPU& cpu) { return SRL_r(cpu, 6); }

// =============================================================================
// CB-PREFIX BIT OPERATIONS
// =============================================================================

// Helper to get register value for bit operations  
static uint8_t GetRegValue(CPU& cpu, uint8_t reg) {
    switch (reg) {
        case 0: return cpu.GetB();
        case 1: return cpu.GetC();
        case 2: return cpu.GetD();
        case 3: return cpu.GetE();
        case 4: return cpu.GetH();
        case 5: return cpu.GetL();
        case 6: return cpu.ReadByte(cpu.GetHL());
        case 7: return cpu.GetA();
        default: return 0;
    }
}

static void SetRegValue(CPU& cpu, uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0: cpu.SetB(value); break;
        case 1: cpu.SetC(value); break;
        case 2: cpu.SetD(value); break;
        case 3: cpu.SetE(value); break;
        case 4: cpu.SetH(value); break;
        case 5: cpu.SetL(value); break;
        case 6: cpu.WriteByte(cpu.GetHL(), value); break;
        case 7: cpu.SetA(value); break;
    }
}

// BIT b,r - Test bit
uint8_t BIT_b_r(CPU& cpu, uint8_t bit, uint8_t reg) {
    uint8_t value = GetRegValue(cpu, reg);
    cpu.SetFlagZ((value & (1 << bit)) == 0);
    cpu.SetFlagN(false);
    cpu.SetFlagH(true);
    return (reg == 6) ? 12 : 8;
}

uint8_t BIT_b_HL(CPU& cpu, uint8_t bit) { return BIT_b_r(cpu, bit, 6); }

// RES b,r - Reset bit
uint8_t RES_b_r(CPU& cpu, uint8_t bit, uint8_t reg) {
    uint8_t value = GetRegValue(cpu, reg);
    value &= ~(1 << bit);
    SetRegValue(cpu, reg, value);
    return (reg == 6) ? 16 : 8;
}

uint8_t RES_b_HL(CPU& cpu, uint8_t bit) { return RES_b_r(cpu, bit, 6); }

// SET b,r - Set bit
uint8_t SET_b_r(CPU& cpu, uint8_t bit, uint8_t reg) {
    uint8_t value = GetRegValue(cpu, reg);
    value |= (1 << bit);
    SetRegValue(cpu, reg, value);
    return (reg == 6) ? 16 : 8;
}

uint8_t SET_b_HL(CPU& cpu, uint8_t bit) { return SET_b_r(cpu, bit, 6); }

// =============================================================================
// CB-PREFIX OPCODE DISPATCH
// =============================================================================

uint8_t ExecuteCBOpcode(CPU& cpu, uint8_t opcode) {
    uint8_t reg = opcode & 0x07;
    uint8_t bit = (opcode >> 3) & 0x07;
    uint8_t op = opcode >> 6;
    
    switch (op) {
        case 0:  // Rotate/Shift
            switch (bit) {
                case 0: return RLC_r(cpu, reg);
                case 1: return RRC_r(cpu, reg);
                case 2: return RL_r(cpu, reg);
                case 3: return RR_r(cpu, reg);
                case 4: return SLA_r(cpu, reg);
                case 5: return SRA_r(cpu, reg);
                case 6: return SWAP_r(cpu, reg);
                case 7: return SRL_r(cpu, reg);
            }
            break;
        case 1:  // BIT
            return BIT_b_r(cpu, bit, reg);
        case 2:  // RES
            return RES_b_r(cpu, bit, reg);
        case 3:  // SET
            return SET_b_r(cpu, bit, reg);
    }
    
    return 8;  // Should never reach here
}

// =============================================================================
// MAIN OPCODE DISPATCH TABLE
// =============================================================================

uint8_t ExecuteOpcode(CPU& cpu, uint8_t opcode) {
    // Decode and execute based on opcode patterns
    switch (opcode) {
        // === Row 0x ===
        case 0x00: return NOP(cpu);
        case 0x01: return LD_rr_nn(cpu, 0);  // LD BC,nn
        case 0x02: return LD_BC_A(cpu);
        case 0x03: return INC_rr(cpu, 0);    // INC BC
        case 0x04: return INC_r(cpu, 0);     // INC B
        case 0x05: return DEC_r(cpu, 0);     // DEC B
        case 0x06: return LD_r_n(cpu, 0);    // LD B,n
        case 0x07: return RLCA(cpu);
        case 0x08: return LD_nn_SP(cpu);
        case 0x09: return ADD_HL_rr(cpu, 0); // ADD HL,BC
        case 0x0A: return LD_A_BC(cpu);
        case 0x0B: return DEC_rr(cpu, 0);    // DEC BC
        case 0x0C: return INC_r(cpu, 1);     // INC C
        case 0x0D: return DEC_r(cpu, 1);     // DEC C
        case 0x0E: return LD_r_n(cpu, 1);    // LD C,n
        case 0x0F: return RRCA(cpu);
        
        // === Row 1x ===
        case 0x10: return STOP(cpu);
        case 0x11: return LD_rr_nn(cpu, 1);  // LD DE,nn
        case 0x12: return LD_DE_A(cpu);
        case 0x13: return INC_rr(cpu, 1);    // INC DE
        case 0x14: return INC_r(cpu, 2);     // INC D
        case 0x15: return DEC_r(cpu, 2);     // DEC D
        case 0x16: return LD_r_n(cpu, 2);    // LD D,n
        case 0x17: return RLA(cpu);
        case 0x18: return JR_n(cpu);
        case 0x19: return ADD_HL_rr(cpu, 1); // ADD HL,DE
        case 0x1A: return LD_A_DE(cpu);
        case 0x1B: return DEC_rr(cpu, 1);    // DEC DE
        case 0x1C: return INC_r(cpu, 3);     // INC E
        case 0x1D: return DEC_r(cpu, 3);     // DEC E
        case 0x1E: return LD_r_n(cpu, 3);    // LD E,n
        case 0x1F: return RRA(cpu);
        
        // === Row 2x ===
        case 0x20: return JR_cc_n(cpu, 0);   // JR NZ,n
        case 0x21: return LD_rr_nn(cpu, 2);  // LD HL,nn
        case 0x22: return LD_HLI_A(cpu);
        case 0x23: return INC_rr(cpu, 2);    // INC HL
        case 0x24: return INC_r(cpu, 4);     // INC H
        case 0x25: return DEC_r(cpu, 4);     // DEC H
        case 0x26: return LD_r_n(cpu, 4);    // LD H,n
        case 0x27: return DAA(cpu);
        case 0x28: return JR_cc_n(cpu, 1);   // JR Z,n
        case 0x29: return ADD_HL_rr(cpu, 2); // ADD HL,HL
        case 0x2A: return LD_A_HLI(cpu);
        case 0x2B: return DEC_rr(cpu, 2);    // DEC HL
        case 0x2C: return INC_r(cpu, 5);     // INC L
        case 0x2D: return DEC_r(cpu, 5);     // DEC L
        case 0x2E: return LD_r_n(cpu, 5);    // LD L,n
        case 0x2F: return CPL(cpu);
        
        // === Row 3x ===
        case 0x30: return JR_cc_n(cpu, 2);   // JR NC,n
        case 0x31: return LD_rr_nn(cpu, 3);  // LD SP,nn
        case 0x32: return LD_HLD_A(cpu);
        case 0x33: return INC_rr(cpu, 3);    // INC SP
        case 0x34: return INC_HL(cpu);
        case 0x35: return DEC_HL(cpu);
        case 0x36: return LD_HL_n(cpu);
        case 0x37: return SCF(cpu);
        case 0x38: return JR_cc_n(cpu, 3);   // JR C,n
        case 0x39: return ADD_HL_rr(cpu, 3); // ADD HL,SP
        case 0x3A: return LD_A_HLD(cpu);
        case 0x3B: return DEC_rr(cpu, 3);    // DEC SP
        case 0x3C: return INC_r(cpu, 7);     // INC A
        case 0x3D: return DEC_r(cpu, 7);     // DEC A
        case 0x3E: return LD_r_n(cpu, 7);    // LD A,n
        case 0x3F: return CCF(cpu);
        
        // === Rows 4x-7x: LD r,r' ===
        case 0x40: return LD_r_r(cpu, 0, 0); // LD B,B
        case 0x41: return LD_r_r(cpu, 0, 1); // LD B,C
        case 0x42: return LD_r_r(cpu, 0, 2); // LD B,D
        case 0x43: return LD_r_r(cpu, 0, 3); // LD B,E
        case 0x44: return LD_r_r(cpu, 0, 4); // LD B,H
        case 0x45: return LD_r_r(cpu, 0, 5); // LD B,L
        case 0x46: return LD_r_HL(cpu, 0);   // LD B,(HL)
        case 0x47: return LD_r_r(cpu, 0, 7); // LD B,A
        case 0x48: return LD_r_r(cpu, 1, 0); // LD C,B
        case 0x49: return LD_r_r(cpu, 1, 1); // LD C,C
        case 0x4A: return LD_r_r(cpu, 1, 2); // LD C,D
        case 0x4B: return LD_r_r(cpu, 1, 3); // LD C,E
        case 0x4C: return LD_r_r(cpu, 1, 4); // LD C,H
        case 0x4D: return LD_r_r(cpu, 1, 5); // LD C,L
        case 0x4E: return LD_r_HL(cpu, 1);   // LD C,(HL)
        case 0x4F: return LD_r_r(cpu, 1, 7); // LD C,A
        
        case 0x50: return LD_r_r(cpu, 2, 0); // LD D,B
        case 0x51: return LD_r_r(cpu, 2, 1); // LD D,C
        case 0x52: return LD_r_r(cpu, 2, 2); // LD D,D
        case 0x53: return LD_r_r(cpu, 2, 3); // LD D,E
        case 0x54: return LD_r_r(cpu, 2, 4); // LD D,H
        case 0x55: return LD_r_r(cpu, 2, 5); // LD D,L
        case 0x56: return LD_r_HL(cpu, 2);   // LD D,(HL)
        case 0x57: return LD_r_r(cpu, 2, 7); // LD D,A
        case 0x58: return LD_r_r(cpu, 3, 0); // LD E,B
        case 0x59: return LD_r_r(cpu, 3, 1); // LD E,C
        case 0x5A: return LD_r_r(cpu, 3, 2); // LD E,D
        case 0x5B: return LD_r_r(cpu, 3, 3); // LD E,E
        case 0x5C: return LD_r_r(cpu, 3, 4); // LD E,H
        case 0x5D: return LD_r_r(cpu, 3, 5); // LD E,L
        case 0x5E: return LD_r_HL(cpu, 3);   // LD E,(HL)
        case 0x5F: return LD_r_r(cpu, 3, 7); // LD E,A
        
        case 0x60: return LD_r_r(cpu, 4, 0); // LD H,B
        case 0x61: return LD_r_r(cpu, 4, 1); // LD H,C
        case 0x62: return LD_r_r(cpu, 4, 2); // LD H,D
        case 0x63: return LD_r_r(cpu, 4, 3); // LD H,E
        case 0x64: return LD_r_r(cpu, 4, 4); // LD H,H
        case 0x65: return LD_r_r(cpu, 4, 5); // LD H,L
        case 0x66: return LD_r_HL(cpu, 4);   // LD H,(HL)
        case 0x67: return LD_r_r(cpu, 4, 7); // LD H,A
        case 0x68: return LD_r_r(cpu, 5, 0); // LD L,B
        case 0x69: return LD_r_r(cpu, 5, 1); // LD L,C
        case 0x6A: return LD_r_r(cpu, 5, 2); // LD L,D
        case 0x6B: return LD_r_r(cpu, 5, 3); // LD L,E
        case 0x6C: return LD_r_r(cpu, 5, 4); // LD L,H
        case 0x6D: return LD_r_r(cpu, 5, 5); // LD L,L
        case 0x6E: return LD_r_HL(cpu, 5);   // LD L,(HL)
        case 0x6F: return LD_r_r(cpu, 5, 7); // LD L,A
        
        case 0x70: return LD_HL_r(cpu, 0);   // LD (HL),B
        case 0x71: return LD_HL_r(cpu, 1);   // LD (HL),C
        case 0x72: return LD_HL_r(cpu, 2);   // LD (HL),D
        case 0x73: return LD_HL_r(cpu, 3);   // LD (HL),E
        case 0x74: return LD_HL_r(cpu, 4);   // LD (HL),H
        case 0x75: return LD_HL_r(cpu, 5);   // LD (HL),L
        case 0x76: return HALT(cpu);
        case 0x77: return LD_HL_r(cpu, 7);   // LD (HL),A
        case 0x78: return LD_r_r(cpu, 7, 0); // LD A,B
        case 0x79: return LD_r_r(cpu, 7, 1); // LD A,C
        case 0x7A: return LD_r_r(cpu, 7, 2); // LD A,D
        case 0x7B: return LD_r_r(cpu, 7, 3); // LD A,E
        case 0x7C: return LD_r_r(cpu, 7, 4); // LD A,H
        case 0x7D: return LD_r_r(cpu, 7, 5); // LD A,L
        case 0x7E: return LD_r_HL(cpu, 7);   // LD A,(HL)
        case 0x7F: return LD_r_r(cpu, 7, 7); // LD A,A
        
        // === Rows 8x-Bx: ALU ===
        case 0x80: return ADD_A_r(cpu, 0);
        case 0x81: return ADD_A_r(cpu, 1);
        case 0x82: return ADD_A_r(cpu, 2);
        case 0x83: return ADD_A_r(cpu, 3);
        case 0x84: return ADD_A_r(cpu, 4);
        case 0x85: return ADD_A_r(cpu, 5);
        case 0x86: return ADD_A_HL(cpu);
        case 0x87: return ADD_A_r(cpu, 7);
        case 0x88: return ADC_A_r(cpu, 0);
        case 0x89: return ADC_A_r(cpu, 1);
        case 0x8A: return ADC_A_r(cpu, 2);
        case 0x8B: return ADC_A_r(cpu, 3);
        case 0x8C: return ADC_A_r(cpu, 4);
        case 0x8D: return ADC_A_r(cpu, 5);
        case 0x8E: return ADC_A_HL(cpu);
        case 0x8F: return ADC_A_r(cpu, 7);
        
        case 0x90: return SUB_r(cpu, 0);
        case 0x91: return SUB_r(cpu, 1);
        case 0x92: return SUB_r(cpu, 2);
        case 0x93: return SUB_r(cpu, 3);
        case 0x94: return SUB_r(cpu, 4);
        case 0x95: return SUB_r(cpu, 5);
        case 0x96: return SUB_HL(cpu);
        case 0x97: return SUB_r(cpu, 7);
        case 0x98: return SBC_A_r(cpu, 0);
        case 0x99: return SBC_A_r(cpu, 1);
        case 0x9A: return SBC_A_r(cpu, 2);
        case 0x9B: return SBC_A_r(cpu, 3);
        case 0x9C: return SBC_A_r(cpu, 4);
        case 0x9D: return SBC_A_r(cpu, 5);
        case 0x9E: return SBC_A_HL(cpu);
        case 0x9F: return SBC_A_r(cpu, 7);
        
        case 0xA0: return AND_r(cpu, 0);
        case 0xA1: return AND_r(cpu, 1);
        case 0xA2: return AND_r(cpu, 2);
        case 0xA3: return AND_r(cpu, 3);
        case 0xA4: return AND_r(cpu, 4);
        case 0xA5: return AND_r(cpu, 5);
        case 0xA6: return AND_HL(cpu);
        case 0xA7: return AND_r(cpu, 7);
        case 0xA8: return XOR_r(cpu, 0);
        case 0xA9: return XOR_r(cpu, 1);
        case 0xAA: return XOR_r(cpu, 2);
        case 0xAB: return XOR_r(cpu, 3);
        case 0xAC: return XOR_r(cpu, 4);
        case 0xAD: return XOR_r(cpu, 5);
        case 0xAE: return XOR_HL(cpu);
        case 0xAF: return XOR_r(cpu, 7);
        
        case 0xB0: return OR_r(cpu, 0);
        case 0xB1: return OR_r(cpu, 1);
        case 0xB2: return OR_r(cpu, 2);
        case 0xB3: return OR_r(cpu, 3);
        case 0xB4: return OR_r(cpu, 4);
        case 0xB5: return OR_r(cpu, 5);
        case 0xB6: return OR_HL(cpu);
        case 0xB7: return OR_r(cpu, 7);
        case 0xB8: return CP_r(cpu, 0);
        case 0xB9: return CP_r(cpu, 1);
        case 0xBA: return CP_r(cpu, 2);
        case 0xBB: return CP_r(cpu, 3);
        case 0xBC: return CP_r(cpu, 4);
        case 0xBD: return CP_r(cpu, 5);
        case 0xBE: return CP_HL(cpu);
        case 0xBF: return CP_r(cpu, 7);
        
        // === Row Cx ===
        case 0xC0: return RET_cc(cpu, 0);    // RET NZ
        case 0xC1: return POP_rr(cpu, 0);    // POP BC
        case 0xC2: return JP_cc_nn(cpu, 0);  // JP NZ,nn
        case 0xC3: return JP_nn(cpu);
        case 0xC4: return CALL_cc_nn(cpu, 0);// CALL NZ,nn
        case 0xC5: return PUSH_rr(cpu, 0);   // PUSH BC
        case 0xC6: return ADD_A_n(cpu);
        case 0xC7: return RST(cpu, 0x00);
        case 0xC8: return RET_cc(cpu, 1);    // RET Z
        case 0xC9: return RET(cpu);
        case 0xCA: return JP_cc_nn(cpu, 1);  // JP Z,nn
        case 0xCB: {  // CB prefix
            uint8_t cb_opcode = cpu.FetchByte();  // Ticks 4 cycles
            return ExecuteCBOpcode(cpu, cb_opcode);  // No +4, FetchByte already ticked
        }
        case 0xCC: return CALL_cc_nn(cpu, 1);// CALL Z,nn
        case 0xCD: return CALL_nn(cpu);
        case 0xCE: return ADC_A_n(cpu);
        case 0xCF: return RST(cpu, 0x08);
        
        // === Row Dx ===        
        case 0xD0: return RET_cc(cpu, 2);    // RET NC
        case 0xD1: return POP_rr(cpu, 1);    // POP DE
        case 0xD2: return JP_cc_nn(cpu, 2);  // JP NC,nn
        // 0xD3 is unused
        case 0xD4: return CALL_cc_nn(cpu, 2);// CALL NC,nn
        case 0xD5: return PUSH_rr(cpu, 1);   // PUSH DE
        case 0xD6: return SUB_n(cpu);
        case 0xD7: return RST(cpu, 0x10);
        case 0xD8: return RET_cc(cpu, 3);    // RET C
        case 0xD9: return RETI(cpu);
        case 0xDA: return JP_cc_nn(cpu, 3);  // JP C,nn
        // 0xDB is unused
        case 0xDC: return CALL_cc_nn(cpu, 3);// CALL C,nn
        // 0xDD is unused
        case 0xDE: return SBC_A_n(cpu);
        case 0xDF: return RST(cpu, 0x18);
        
        // === Row Ex ===
        case 0xE0: return LDH_n_A(cpu);
        case 0xE1: return POP_rr(cpu, 2);    // POP HL
        case 0xE2: return LDH_C_A(cpu);
        // 0xE3 is unused
        // 0xE4 is unused
        case 0xE5: return PUSH_rr(cpu, 2);   // PUSH HL
        case 0xE6: return AND_n(cpu);
        case 0xE7: return RST(cpu, 0x20);
        case 0xE8: return ADD_SP_n(cpu);
        case 0xE9: return JP_HL(cpu);
        case 0xEA: return LD_nn_A(cpu);
        // 0xEB is unused
        // 0xEC is unused
        // 0xED is unused
        case 0xEE: return XOR_n(cpu);
        case 0xEF: return RST(cpu, 0x28);
        
        // === Row Fx ===
        case 0xF0: return LDH_A_n(cpu);
        case 0xF1: return POP_rr(cpu, 3);    // POP AF
        case 0xF2: return LDH_A_C(cpu);
        case 0xF3: return DI(cpu);
        // 0xF4 is unused
        case 0xF5: return PUSH_rr(cpu, 3);   // PUSH AF
        case 0xF6: return OR_n(cpu);
        case 0xF7: return RST(cpu, 0x30);
        case 0xF8: return LD_HL_SP_n(cpu);
        case 0xF9: return LD_SP_HL(cpu);
        case 0xFA: return LD_A_nn(cpu);
        case 0xFB: return EI(cpu);
        // 0xFC is unused
        // 0xFD is unused
        case 0xFE: return CP_n(cpu);
        case 0xFF: return RST(cpu, 0x38);
        
        default:
            // Unused opcodes - act as NOP on DMG
            return 4;
    }
}

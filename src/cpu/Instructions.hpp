#pragma once

#include <cstdint>

/**
 * SM83 Instruction Definitions
 * 
 * Hardware Accuracy Notes:
 * - Each memory access = 4 T-cycles
 * - Instruction timing is the SUM of all memory accesses
 * - Internal operations (ALU) happen during the last memory access
 * 
 * T-Cycle Breakdown Examples:
 * - NOP: 1 fetch = 4 T-cycles
 * - LD r,n: 2 fetches = 8 T-cycles  
 * - LD (HL),n: 2 fetches + 1 write = 12 T-cycles
 * - JP nn: 3 fetches + 1 internal = 16 T-cycles (but internal is overlapped)
 * 
 * Conditional instructions:
 * - JP cc,nn: 12 T-cycles if not taken, 16 if taken
 * - JR cc,n: 8 T-cycles if not taken, 12 if taken
 * - CALL cc,nn: 12 T-cycles if not taken, 24 if taken
 * - RET cc: 8 T-cycles if not taken, 20 if taken
 */

class CPU;

// Main instruction executor - dispatches by opcode
uint8_t ExecuteOpcode(CPU& cpu, uint8_t opcode);

// CB-prefix instruction executor
uint8_t ExecuteCBOpcode(CPU& cpu, uint8_t opcode);

// === Instruction Categories ===

// 8-bit Load instructions
uint8_t LD_r_r(CPU& cpu, uint8_t dest, uint8_t src);
uint8_t LD_r_n(CPU& cpu, uint8_t dest);
uint8_t LD_r_HL(CPU& cpu, uint8_t dest);
uint8_t LD_HL_r(CPU& cpu, uint8_t src);
uint8_t LD_HL_n(CPU& cpu);
uint8_t LD_A_BC(CPU& cpu);
uint8_t LD_A_DE(CPU& cpu);
uint8_t LD_A_nn(CPU& cpu);
uint8_t LD_BC_A(CPU& cpu);
uint8_t LD_DE_A(CPU& cpu);
uint8_t LD_nn_A(CPU& cpu);
uint8_t LDH_A_n(CPU& cpu);
uint8_t LDH_n_A(CPU& cpu);
uint8_t LDH_A_C(CPU& cpu);
uint8_t LDH_C_A(CPU& cpu);
uint8_t LD_A_HLI(CPU& cpu);
uint8_t LD_A_HLD(CPU& cpu);
uint8_t LD_HLI_A(CPU& cpu);
uint8_t LD_HLD_A(CPU& cpu);

// 16-bit Load instructions
uint8_t LD_rr_nn(CPU& cpu, uint8_t reg);
uint8_t LD_nn_SP(CPU& cpu);
uint8_t LD_SP_HL(CPU& cpu);
uint8_t LD_HL_SP_n(CPU& cpu);
uint8_t PUSH_rr(CPU& cpu, uint8_t reg);
uint8_t POP_rr(CPU& cpu, uint8_t reg);

// 8-bit ALU instructions
uint8_t ADD_A_r(CPU& cpu, uint8_t src);
uint8_t ADD_A_HL(CPU& cpu);
uint8_t ADD_A_n(CPU& cpu);
uint8_t ADC_A_r(CPU& cpu, uint8_t src);
uint8_t ADC_A_HL(CPU& cpu);
uint8_t ADC_A_n(CPU& cpu);
uint8_t SUB_r(CPU& cpu, uint8_t src);
uint8_t SUB_HL(CPU& cpu);
uint8_t SUB_n(CPU& cpu);
uint8_t SBC_A_r(CPU& cpu, uint8_t src);
uint8_t SBC_A_HL(CPU& cpu);
uint8_t SBC_A_n(CPU& cpu);
uint8_t AND_r(CPU& cpu, uint8_t src);
uint8_t AND_HL(CPU& cpu);
uint8_t AND_n(CPU& cpu);
uint8_t XOR_r(CPU& cpu, uint8_t src);
uint8_t XOR_HL(CPU& cpu);
uint8_t XOR_n(CPU& cpu);
uint8_t OR_r(CPU& cpu, uint8_t src);
uint8_t OR_HL(CPU& cpu);
uint8_t OR_n(CPU& cpu);
uint8_t CP_r(CPU& cpu, uint8_t src);
uint8_t CP_HL(CPU& cpu);
uint8_t CP_n(CPU& cpu);
uint8_t INC_r(CPU& cpu, uint8_t reg);
uint8_t INC_HL(CPU& cpu);
uint8_t DEC_r(CPU& cpu, uint8_t reg);
uint8_t DEC_HL(CPU& cpu);
uint8_t DAA(CPU& cpu);
uint8_t CPL(CPU& cpu);
uint8_t SCF(CPU& cpu);
uint8_t CCF(CPU& cpu);

// 16-bit ALU instructions
uint8_t ADD_HL_rr(CPU& cpu, uint8_t reg);
uint8_t ADD_SP_n(CPU& cpu);
uint8_t INC_rr(CPU& cpu, uint8_t reg);
uint8_t DEC_rr(CPU& cpu, uint8_t reg);

// Rotate/Shift instructions (non-CB)
uint8_t RLCA(CPU& cpu);
uint8_t RLA(CPU& cpu);
uint8_t RRCA(CPU& cpu);
uint8_t RRA(CPU& cpu);

// Control flow
uint8_t JP_nn(CPU& cpu);
uint8_t JP_cc_nn(CPU& cpu, uint8_t cc);
uint8_t JP_HL(CPU& cpu);
uint8_t JR_n(CPU& cpu);
uint8_t JR_cc_n(CPU& cpu, uint8_t cc);
uint8_t CALL_nn(CPU& cpu);
uint8_t CALL_cc_nn(CPU& cpu, uint8_t cc);
uint8_t RET(CPU& cpu);
uint8_t RET_cc(CPU& cpu, uint8_t cc);
uint8_t RETI(CPU& cpu);
uint8_t RST(CPU& cpu, uint8_t vec);

// Misc
uint8_t NOP(CPU& cpu);
uint8_t HALT(CPU& cpu);
uint8_t STOP(CPU& cpu);
uint8_t DI(CPU& cpu);
uint8_t EI(CPU& cpu);

// CB-prefix rotate/shift
uint8_t RLC_r(CPU& cpu, uint8_t reg);
uint8_t RLC_HL(CPU& cpu);
uint8_t RRC_r(CPU& cpu, uint8_t reg);
uint8_t RRC_HL(CPU& cpu);
uint8_t RL_r(CPU& cpu, uint8_t reg);
uint8_t RL_HL(CPU& cpu);
uint8_t RR_r(CPU& cpu, uint8_t reg);
uint8_t RR_HL(CPU& cpu);
uint8_t SLA_r(CPU& cpu, uint8_t reg);
uint8_t SLA_HL(CPU& cpu);
uint8_t SRA_r(CPU& cpu, uint8_t reg);
uint8_t SRA_HL(CPU& cpu);
uint8_t SWAP_r(CPU& cpu, uint8_t reg);
uint8_t SWAP_HL(CPU& cpu);
uint8_t SRL_r(CPU& cpu, uint8_t reg);
uint8_t SRL_HL(CPU& cpu);

// CB-prefix bit operations
uint8_t BIT_b_r(CPU& cpu, uint8_t bit, uint8_t reg);
uint8_t BIT_b_HL(CPU& cpu, uint8_t bit);
uint8_t RES_b_r(CPU& cpu, uint8_t bit, uint8_t reg);
uint8_t RES_b_HL(CPU& cpu, uint8_t bit);
uint8_t SET_b_r(CPU& cpu, uint8_t bit, uint8_t reg);
uint8_t SET_b_HL(CPU& cpu, uint8_t bit);

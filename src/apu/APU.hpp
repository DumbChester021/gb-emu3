#pragma once

#include <cstdint>
#include <array>

/**
 * APU - Audio Processing Unit
 * 
 * Hardware Behavior:
 * - 4 sound channels mixed together
 * - Frame sequencer clocked by DIV (bit 4 falling edge = 512 Hz)
 * - Outputs left/right audio samples
 * - Does NOT know about CPU/Timer - only sees register writes and DIV signal
 * 
 * Interface:
 * - Register access at $FF10-$FF3F
 * - DIV bit input (fed from Timer for frame sequencer)
 * - Audio output samples
 */
class APU {
public:
    APU();
    
    void Reset();
    
    // Advance APU by specified T-cycles
    void Step(uint8_t cycles);
    
    // === DIV Input (directly exposed from Timer - bit 4 for 512 Hz) ===
    // Call this when DIV bit 4 has a falling edge
    void ClockFrameSequencer();
    
    // === Register Interface (directly exposed memory-mapped I/O) ===
    uint8_t ReadRegister(uint16_t addr) const;
    void WriteRegister(uint16_t addr, uint8_t value);
    
    // === Wave RAM Interface (directly exposed $FF30-$FF3F) ===
    uint8_t ReadWaveRAM(uint8_t index) const;
    void WriteWaveRAM(uint8_t index, uint8_t value);
    
    // === Audio Output (directly exposed sample data) ===
    // Returns stereo sample pair (left, right) in range [-1.0, 1.0]
    void GetSample(float& left, float& right) const;
    
    // For sample buffer filling at target sample rate
    bool HasSample() const { return sample_ready; }
    void ClearSampleReady() { sample_ready = false; }
    
private:
    // === Channel 1: Pulse with Sweep ===
    struct Channel1 {
        // Registers (directly exposed NR10-NR14)
        uint8_t sweep_period;       // NR10 bits 6-4
        bool sweep_negate;          // NR10 bit 3
        uint8_t sweep_shift;        // NR10 bits 2-0
        uint8_t duty;               // NR11 bits 7-6
        uint8_t length_load;        // NR11 bits 5-0
        uint8_t volume_init;        // NR12 bits 7-4
        bool envelope_add;          // NR12 bit 3
        uint8_t envelope_period;    // NR12 bits 2-0
        uint16_t frequency;         // NR13 + NR14 bits 2-0
        bool length_enable;         // NR14 bit 6
        
        // Internal state (directly expose internal flip-flops)
        bool enabled;
        uint8_t volume;
        uint8_t envelope_timer;
        uint8_t sweep_timer;
        uint16_t shadow_freq;
        bool sweep_enabled;
        uint16_t length_counter;
        uint16_t frequency_timer;
        uint8_t duty_position;
    } ch1;
    
    // === Channel 2: Pulse (no sweep) ===
    struct Channel2 {
        uint8_t duty;
        uint8_t length_load;
        uint8_t volume_init;
        bool envelope_add;
        uint8_t envelope_period;
        uint16_t frequency;
        bool length_enable;
        
        bool enabled;
        uint8_t volume;
        uint8_t envelope_timer;
        uint16_t length_counter;
        uint16_t frequency_timer;
        uint8_t duty_position;
    } ch2;
    
    // === Channel 3: Wave ===
    struct Channel3 {
        bool dac_enabled;           // NR30 bit 7
        uint8_t length_load;        // NR31
        uint8_t volume_code;        // NR32 bits 6-5
        uint16_t frequency;         // NR33 + NR34 bits 2-0
        bool length_enable;         // NR34 bit 6
        
        bool enabled;
        uint16_t length_counter;
        uint16_t frequency_timer;
        uint8_t position;           // Wave table position (0-31)
        uint8_t sample_buffer;      // Last read sample
    } ch3;
    
    // === Channel 4: Noise ===
    struct Channel4 {
        uint8_t length_load;        // NR41 bits 5-0
        uint8_t volume_init;        // NR42 bits 7-4
        bool envelope_add;          // NR42 bit 3
        uint8_t envelope_period;    // NR42 bits 2-0
        uint8_t clock_shift;        // NR43 bits 7-4
        bool width_mode;            // NR43 bit 3 (7-bit LFSR if true)
        uint8_t divisor_code;       // NR43 bits 2-0
        bool length_enable;         // NR44 bit 6
        
        bool enabled;
        uint8_t volume;
        uint8_t envelope_timer;
        uint16_t length_counter;
        uint16_t frequency_timer;
        uint16_t lfsr;              // Linear Feedback Shift Register
    } ch4;
    
    // === Master Control (directly exposed as registers) ===
    bool power_on;                  // NR52 bit 7
    uint8_t nr50;                   // NR50 raw byte (VIN + volume bits)
    uint8_t nr51;                   // NR51 raw byte (channel enables)
    
    // Volume access (extracted from nr50)
    uint8_t GetLeftVolume() const { return (nr50 >> 4) & 7; }
    uint8_t GetRightVolume() const { return nr50 & 7; }
    // Channel access (extracted from nr51)
    uint8_t GetChannelLeft() const { return (nr51 >> 4) & 0x0F; }
    uint8_t GetChannelRight() const { return nr51 & 0x0F; }
    
    // === Wave RAM (directly exposed $FF30-$FF3F) ===
    std::array<uint8_t, 16> wave_ram;
    
    // === Frame Sequencer (clocked by DIV) ===
    uint8_t frame_sequencer_step;   // 0-7
    
    // === Sample Output (directly exposed) ===
    float left_sample;
    float right_sample;
    bool sample_ready;
    uint16_t sample_counter;        // For downsampling to target rate
    
    // === Internal Operations (directly expose the channel logic) ===
    void StepChannel1(uint8_t cycles);
    void StepChannel2(uint8_t cycles);
    void StepChannel3(uint8_t cycles);
    void StepChannel4(uint8_t cycles);
    
    void ClockLength();
    void ClockEnvelope();
    void ClockSweep();
    
    void TriggerChannel1();
    void TriggerChannel2();
    void TriggerChannel3();
    void TriggerChannel4();
    
    uint8_t GetChannel1Output() const;
    uint8_t GetChannel2Output() const;
    uint8_t GetChannel3Output() const;
    uint8_t GetChannel4Output() const;
    
    void MixChannels();
    
    // Duty cycle patterns
    static constexpr uint8_t DUTY_TABLE[4] = {
        0b00000001,  // 12.5%
        0b00000011,  // 25%
        0b00001111,  // 50%
        0b11111100   // 75%
    };
    
    // Noise divisor table
    static constexpr uint8_t DIVISORS[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
};

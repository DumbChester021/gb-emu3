#include "APU.hpp"
#include <cmath>

constexpr uint8_t APU::DUTY_TABLE[4];
constexpr uint8_t APU::DIVISORS[8];

APU::APU() {
    Reset();
}

void APU::Reset() {
    ch1 = {};
    ch2 = {};
    ch3 = {};
    ch4 = {};
    
    power_on = true;
    // Per SameBoy GB_apu_init: memset to 0 - boot ROM sets proper values
    left_volume = 0;
    right_volume = 0;
    channel_left = 0;
    channel_right = 0;
    
    wave_ram.fill(0);
    frame_sequencer_step = 0;
    
    left_sample = 0;
    right_sample = 0;
    sample_ready = false;
    sample_counter = 0;
}

void APU::Step(uint8_t cycles) {
    if (!power_on) return;
    
    // Step each channel
    StepChannel1(cycles);
    StepChannel2(cycles);
    StepChannel3(cycles);
    StepChannel4(cycles);
    
    // Downsample for audio output
    sample_counter += cycles;
    if (sample_counter >= 87) {  // ~48000 Hz at 4.19 MHz
        sample_counter -= 87;
        MixChannels();
        sample_ready = true;
    }
}

void APU::ClockFrameSequencer() {
    // Called at 512 Hz from DIV bit 4
    switch (frame_sequencer_step) {
        case 0: ClockLength(); break;
        case 1: break;
        case 2: ClockLength(); ClockSweep(); break;
        case 3: break;
        case 4: ClockLength(); break;
        case 5: break;
        case 6: ClockLength(); ClockSweep(); break;
        case 7: ClockEnvelope(); break;
    }
    
    frame_sequencer_step = (frame_sequencer_step + 1) & 7;
}

void APU::StepChannel1(uint8_t cycles) {
    if (!ch1.enabled) return;
    
    ch1.frequency_timer -= cycles;
    while (ch1.frequency_timer <= 0) {
        ch1.frequency_timer += (2048 - ch1.frequency) * 4;
        ch1.duty_position = (ch1.duty_position + 1) & 7;
    }
}

void APU::StepChannel2(uint8_t cycles) {
    if (!ch2.enabled) return;
    
    ch2.frequency_timer -= cycles;
    while (ch2.frequency_timer <= 0) {
        ch2.frequency_timer += (2048 - ch2.frequency) * 4;
        ch2.duty_position = (ch2.duty_position + 1) & 7;
    }
}

void APU::StepChannel3(uint8_t cycles) {
    if (!ch3.enabled) return;
    
    ch3.frequency_timer -= cycles;
    while (ch3.frequency_timer <= 0) {
        ch3.frequency_timer += (2048 - ch3.frequency) * 2;
        ch3.position = (ch3.position + 1) & 31;
        
        uint8_t byte = wave_ram[ch3.position / 2];
        ch3.sample_buffer = (ch3.position & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

void APU::StepChannel4(uint8_t cycles) {
    if (!ch4.enabled) return;
    
    ch4.frequency_timer -= cycles;
    while (ch4.frequency_timer <= 0) {
        ch4.frequency_timer += DIVISORS[ch4.divisor_code] << ch4.clock_shift;
        
        // Clock LFSR
        uint8_t xor_bit = (ch4.lfsr & 1) ^ ((ch4.lfsr >> 1) & 1);
        ch4.lfsr = (ch4.lfsr >> 1) | (xor_bit << 14);
        
        if (ch4.width_mode) {
            ch4.lfsr &= ~(1 << 6);
            ch4.lfsr |= xor_bit << 6;
        }
    }
}

void APU::ClockLength() {
    if (ch1.length_enable && ch1.length_counter > 0) {
        if (--ch1.length_counter == 0) ch1.enabled = false;
    }
    if (ch2.length_enable && ch2.length_counter > 0) {
        if (--ch2.length_counter == 0) ch2.enabled = false;
    }
    if (ch3.length_enable && ch3.length_counter > 0) {
        if (--ch3.length_counter == 0) ch3.enabled = false;
    }
    if (ch4.length_enable && ch4.length_counter > 0) {
        if (--ch4.length_counter == 0) ch4.enabled = false;
    }
}

void APU::ClockEnvelope() {
    // Channel 1
    if (ch1.envelope_period != 0) {
        if (--ch1.envelope_timer == 0) {
            ch1.envelope_timer = ch1.envelope_period;
            if (ch1.envelope_add && ch1.volume < 15) ch1.volume++;
            else if (!ch1.envelope_add && ch1.volume > 0) ch1.volume--;
        }
    }
    // Channel 2
    if (ch2.envelope_period != 0) {
        if (--ch2.envelope_timer == 0) {
            ch2.envelope_timer = ch2.envelope_period;
            if (ch2.envelope_add && ch2.volume < 15) ch2.volume++;
            else if (!ch2.envelope_add && ch2.volume > 0) ch2.volume--;
        }
    }
    // Channel 4
    if (ch4.envelope_period != 0) {
        if (--ch4.envelope_timer == 0) {
            ch4.envelope_timer = ch4.envelope_period;
            if (ch4.envelope_add && ch4.volume < 15) ch4.volume++;
            else if (!ch4.envelope_add && ch4.volume > 0) ch4.volume--;
        }
    }
}

void APU::ClockSweep() {
    if (ch1.sweep_timer > 0) ch1.sweep_timer--;
    
    if (ch1.sweep_timer == 0) {
        ch1.sweep_timer = ch1.sweep_period ? ch1.sweep_period : 8;
        
        if (ch1.sweep_enabled && ch1.sweep_period) {
            uint16_t new_freq = ch1.shadow_freq >> ch1.sweep_shift;
            if (ch1.sweep_negate) new_freq = ch1.shadow_freq - new_freq;
            else new_freq = ch1.shadow_freq + new_freq;
            
            if (new_freq > 2047) {
                ch1.enabled = false;
            } else if (ch1.sweep_shift) {
                ch1.shadow_freq = new_freq;
                ch1.frequency = new_freq;
            }
        }
    }
}

void APU::TriggerChannel1() {
    ch1.enabled = true;
    ch1.length_counter = 64 - ch1.length_load;
    ch1.frequency_timer = (2048 - ch1.frequency) * 4;
    ch1.envelope_timer = ch1.envelope_period;
    ch1.volume = ch1.volume_init;
    ch1.shadow_freq = ch1.frequency;
    ch1.sweep_timer = ch1.sweep_period ? ch1.sweep_period : 8;
    ch1.sweep_enabled = ch1.sweep_period || ch1.sweep_shift;
}

void APU::TriggerChannel2() {
    ch2.enabled = true;
    ch2.length_counter = 64 - ch2.length_load;
    ch2.frequency_timer = (2048 - ch2.frequency) * 4;
    ch2.envelope_timer = ch2.envelope_period;
    ch2.volume = ch2.volume_init;
}

void APU::TriggerChannel3() {
    ch3.enabled = ch3.dac_enabled;
    ch3.length_counter = 256 - ch3.length_load;
    ch3.frequency_timer = (2048 - ch3.frequency) * 2;
    ch3.position = 0;
}

void APU::TriggerChannel4() {
    ch4.enabled = true;
    ch4.length_counter = 64 - ch4.length_load;
    ch4.envelope_timer = ch4.envelope_period;
    ch4.volume = ch4.volume_init;
    ch4.lfsr = 0x7FFF;
}

uint8_t APU::GetChannel1Output() const {
    if (!ch1.enabled) return 0;
    return (DUTY_TABLE[ch1.duty] & (1 << ch1.duty_position)) ? ch1.volume : 0;
}

uint8_t APU::GetChannel2Output() const {
    if (!ch2.enabled) return 0;
    return (DUTY_TABLE[ch2.duty] & (1 << ch2.duty_position)) ? ch2.volume : 0;
}

uint8_t APU::GetChannel3Output() const {
    if (!ch3.enabled || !ch3.dac_enabled) return 0;
    uint8_t output = ch3.sample_buffer;
    switch (ch3.volume_code) {
        case 0: output = 0; break;
        case 1: break;
        case 2: output >>= 1; break;
        case 3: output >>= 2; break;
    }
    return output;
}

uint8_t APU::GetChannel4Output() const {
    if (!ch4.enabled) return 0;
    return (~ch4.lfsr & 1) ? ch4.volume : 0;
}

void APU::MixChannels() {
    float left = 0, right = 0;
    
    uint8_t ch1_out = GetChannel1Output();
    uint8_t ch2_out = GetChannel2Output();
    uint8_t ch3_out = GetChannel3Output();
    uint8_t ch4_out = GetChannel4Output();
    
    if (channel_left & 0x01) left += ch1_out;
    if (channel_left & 0x02) left += ch2_out;
    if (channel_left & 0x04) left += ch3_out;
    if (channel_left & 0x08) left += ch4_out;
    
    if (channel_right & 0x01) right += ch1_out;
    if (channel_right & 0x02) right += ch2_out;
    if (channel_right & 0x04) right += ch3_out;
    if (channel_right & 0x08) right += ch4_out;
    
    // Normalize and apply master volume
    left_sample = (left / 60.0f) * ((left_volume + 1) / 8.0f);
    right_sample = (right / 60.0f) * ((right_volume + 1) / 8.0f);
}

void APU::GetSample(float& left, float& right) const {
    left = left_sample;
    right = right_sample;
}

// === Register Access ===

uint8_t APU::ReadRegister(uint16_t addr) const {
    // Per SameBoy apu.c read_mask: unused bits read as 1
    switch (addr) {
        // NR1X - Channel 1
        case 0xFF10: return ch1.sweep_period << 4 | ch1.sweep_negate << 3 | ch1.sweep_shift | 0x80;
        case 0xFF11: return ch1.duty << 6 | 0x3F;  // Only duty readable
        case 0xFF12: return ch1.volume_init << 4 | ch1.envelope_add << 3 | ch1.envelope_period;  // 0x00 mask
        case 0xFF13: return 0xFF;  // Write-only
        case 0xFF14: return ch1.length_enable << 6 | 0xBF;
        
        // NR2X - Channel 2 (0xFF15 unused)
        case 0xFF15: return 0xFF;  // Unused register
        case 0xFF16: return ch2.duty << 6 | 0x3F;
        case 0xFF17: return ch2.volume_init << 4 | ch2.envelope_add << 3 | ch2.envelope_period;
        case 0xFF18: return 0xFF;  // Write-only
        case 0xFF19: return ch2.length_enable << 6 | 0xBF;
        
        // NR3X - Channel 3
        case 0xFF1A: return ch3.dac_enabled << 7 | 0x7F;
        case 0xFF1B: return 0xFF;  // Write-only
        case 0xFF1C: return ch3.volume_code << 5 | 0x9F;
        case 0xFF1D: return 0xFF;  // Write-only
        case 0xFF1E: return ch3.length_enable << 6 | 0xBF;
        
        // NR4X - Channel 4 (0xFF1F unused)
        case 0xFF1F: return 0xFF;  // Unused register
        case 0xFF20: return 0xFF;  // Write-only (length has 0xFF mask per SameBoy)
        case 0xFF21: return ch4.volume_init << 4 | ch4.envelope_add << 3 | ch4.envelope_period;
        case 0xFF22: return ch4.clock_shift << 4 | ch4.width_mode << 3 | ch4.divisor_code;
        case 0xFF23: return ch4.length_enable << 6 | 0xBF;
        
        // NR5X - Control
        case 0xFF24: return left_volume << 4 | right_volume;  // 0x00 mask - Vin bits not implemented
        case 0xFF25: return channel_left << 4 | channel_right;  // 0x00 mask
        case 0xFF26: return power_on << 7 | 0x70 |  // Bits 4-6 unused = 1
                     (ch4.enabled << 3) | (ch3.enabled << 2) | 
                     (ch2.enabled << 1) | ch1.enabled;
        
        // 0xFF27-0xFF2F unused
        case 0xFF27: case 0xFF28: case 0xFF29: case 0xFF2A:
        case 0xFF2B: case 0xFF2C: case 0xFF2D: case 0xFF2E:
        case 0xFF2F: return 0xFF;
        
        default: return 0xFF;
    }
}

void APU::WriteRegister(uint16_t addr, uint8_t value) {
    if (!power_on && addr != 0xFF26) return;
    
    switch (addr) {
        case 0xFF10:
            ch1.sweep_period = (value >> 4) & 7;
            ch1.sweep_negate = (value >> 3) & 1;
            ch1.sweep_shift = value & 7;
            break;
        case 0xFF11:
            ch1.duty = (value >> 6) & 3;
            ch1.length_load = value & 0x3F;
            break;
        case 0xFF12:
            ch1.volume_init = (value >> 4) & 0x0F;
            ch1.envelope_add = (value >> 3) & 1;
            ch1.envelope_period = value & 7;
            break;
        case 0xFF13:
            ch1.frequency = (ch1.frequency & 0x700) | value;
            break;
        case 0xFF14:
            ch1.frequency = (ch1.frequency & 0xFF) | ((value & 7) << 8);
            ch1.length_enable = (value >> 6) & 1;
            if (value & 0x80) TriggerChannel1();
            break;
            
        case 0xFF16:
            ch2.duty = (value >> 6) & 3;
            ch2.length_load = value & 0x3F;
            break;
        case 0xFF17:
            ch2.volume_init = (value >> 4) & 0x0F;
            ch2.envelope_add = (value >> 3) & 1;
            ch2.envelope_period = value & 7;
            break;
        case 0xFF18:
            ch2.frequency = (ch2.frequency & 0x700) | value;
            break;
        case 0xFF19:
            ch2.frequency = (ch2.frequency & 0xFF) | ((value & 7) << 8);
            ch2.length_enable = (value >> 6) & 1;
            if (value & 0x80) TriggerChannel2();
            break;
            
        case 0xFF1A:
            ch3.dac_enabled = (value >> 7) & 1;
            if (!ch3.dac_enabled) ch3.enabled = false;
            break;
        case 0xFF1B:
            ch3.length_load = value;
            break;
        case 0xFF1C:
            ch3.volume_code = (value >> 5) & 3;
            break;
        case 0xFF1D:
            ch3.frequency = (ch3.frequency & 0x700) | value;
            break;
        case 0xFF1E:
            ch3.frequency = (ch3.frequency & 0xFF) | ((value & 7) << 8);
            ch3.length_enable = (value >> 6) & 1;
            if (value & 0x80) TriggerChannel3();
            break;
            
        case 0xFF20:
            ch4.length_load = value & 0x3F;
            break;
        case 0xFF21:
            ch4.volume_init = (value >> 4) & 0x0F;
            ch4.envelope_add = (value >> 3) & 1;
            ch4.envelope_period = value & 7;
            break;
        case 0xFF22:
            ch4.clock_shift = (value >> 4) & 0x0F;
            ch4.width_mode = (value >> 3) & 1;
            ch4.divisor_code = value & 7;
            break;
        case 0xFF23:
            ch4.length_enable = (value >> 6) & 1;
            if (value & 0x80) TriggerChannel4();
            break;
            
        case 0xFF24:
            left_volume = (value >> 4) & 7;
            right_volume = value & 7;
            break;
        case 0xFF25:
            channel_left = (value >> 4) & 0x0F;
            channel_right = value & 0x0F;
            break;
        case 0xFF26:
            power_on = (value >> 7) & 1;
            if (!power_on) Reset();
            break;
    }
}

uint8_t APU::ReadWaveRAM(uint8_t index) const {
    // Per SameBoy apu.c lines 1051-1053:
    // On DMG, reading wave RAM while channel 3 is active returns 0xFF
    if (ch3.enabled) {
        return 0xFF;
    }
    return wave_ram[index];
}

void APU::WriteWaveRAM(uint8_t index, uint8_t value) {
    wave_ram[index] = value;
}

#include "APU.hpp"
#include "AudioBuffer.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>

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
    nr50 = 0;  // Raw byte storage for VIN bits
    channel_left = 0;
    channel_right = 0;
    io_registers.fill(0);  // Raw register storage
    
    wave_ram.fill(0);
    frame_sequencer_step = 0;
    skip_first_div_event = false;
    div_bit12_high = false;
    
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
        
        // Push to audio buffer for SDL playback
        if (audio_buffer) {
            audio_buffer->Push(left_sample, right_sample);
        }
        
        sample_ready = true;
    }
}

void APU::ClockFrameSequencer() {
    // Per SameBoy skip_div_event: when APU powers on with DIV bit high,
    // skip the first falling edge event
    if (skip_first_div_event) {
        skip_first_div_event = false;
        return;
    }
    
    // Called at 512 Hz from DIV bit 12 falling edge
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
    
    // Per SameBoy apu.c lines 910-929: exact wave_form_just_read timing
    // Track the T-cycle offset (0-3) when sample was read for precise timing
    ch3.wave_form_just_read = false;
    ch3.sample_read_cycle = -1;  // -1 means no sample read this step
    
    int16_t cycles_left = cycles;
    int16_t cycles_consumed = 0;  // Track total cycles consumed to know T-cycle offset
    
    while (cycles_left > ch3.frequency_timer) {
        cycles_consumed += ch3.frequency_timer + 1;
        cycles_left -= ch3.frequency_timer + 1;
        ch3.frequency_timer = (2048 - ch3.frequency) * 2 - 1;
        ch3.position = (ch3.position + 1) & 31;
        
        uint8_t byte = wave_ram[ch3.position / 2];
        ch3.sample_buffer = (ch3.position & 1) ? (byte & 0x0F) : (byte >> 4);
        
        // Record T-cycle offset when sample was read (0-3 within the M-cycle)
        ch3.sample_read_cycle = (cycles_consumed - 1) & 3;
        ch3.wave_form_just_read = true;
    }
    
    // Per SameBoy lines 926-928: if any cycles remain after sample read, window closes
    if (cycles_left > 0) {
        ch3.frequency_timer -= cycles_left;
        ch3.wave_form_just_read = false;
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
            if (ch1.sweep_negate) {
                new_freq = ch1.shadow_freq - new_freq;
                ch1.swept_negate = true;  // Mark that negate was used
            }
            else new_freq = ch1.shadow_freq + new_freq;
            
            if (new_freq > 2047) {
                ch1.enabled = false;
            } else if (ch1.sweep_shift) {
                // Update frequency with new value
                ch1.shadow_freq = new_freq;
                ch1.frequency = new_freq;
                
                // Per hardware: calculate AGAIN with the new frequency
                // This is test #5: "After updating frequency, calculates a second time"
                uint16_t second_freq = ch1.shadow_freq >> ch1.sweep_shift;
                if (ch1.sweep_negate) {
                    second_freq = ch1.shadow_freq - second_freq;
                    ch1.swept_negate = true;  // Mark that negate was used
                }
                else second_freq = ch1.shadow_freq + second_freq;
                
                if (second_freq > 2047) {
                    ch1.enabled = false;
                }
            }
        }
    }
}

void APU::TriggerChannel1() {
    // Per SameBoy: DAC must be enabled to enable channel
    // DAC is on when volume_init > 0 OR envelope_add = 1 (NR12 & 0xF8 != 0)
    bool dac_on = ch1.volume_init > 0 || ch1.envelope_add;
    if (dac_on) {
        ch1.enabled = true;
    }
    // Per SameBoy: only reload length to max if currently 0
    // AND set length_enable = false (un-freeze) per line 1459
    if (ch1.length_counter == 0) {
        ch1.length_counter = 64;
        ch1.length_enable = false;  // Un-freeze
    }
    ch1.frequency_timer = (2048 - ch1.frequency) * 4;
    ch1.envelope_timer = ch1.envelope_period;
    ch1.volume = ch1.volume_init;
    ch1.shadow_freq = ch1.frequency;
    ch1.sweep_timer = ch1.sweep_period ? ch1.sweep_period : 8;
    ch1.sweep_enabled = ch1.sweep_period || ch1.sweep_shift;
    ch1.swept_negate = false;  // Clear negate lockout flag on trigger
    
    // Per SameBoy lines 1466-1481: APU bug - if shift is nonzero,
    // overflow check also occurs on trigger
    if (ch1.sweep_shift) {
        uint16_t new_freq = ch1.shadow_freq >> ch1.sweep_shift;
        if (ch1.sweep_negate) {
            new_freq = ch1.shadow_freq - new_freq;
            ch1.swept_negate = true;  // Mark that negate was used
        }
        else new_freq = ch1.shadow_freq + new_freq;
        
        if (new_freq > 2047) {
            ch1.enabled = false;
        }
    }
}

void APU::TriggerChannel2() {
    // Per SameBoy: DAC must be enabled to enable channel
    bool dac_on = ch2.volume_init > 0 || ch2.envelope_add;
    if (dac_on) {
        ch2.enabled = true;
    }
    // Per SameBoy: only reload length to max if currently 0
    // AND set length_enable = false (un-freeze)
    if (ch2.length_counter == 0) {
        ch2.length_counter = 64;
        ch2.length_enable = false;
    }
    ch2.frequency_timer = (2048 - ch2.frequency) * 4;
    ch2.envelope_timer = ch2.envelope_period;
    ch2.volume = ch2.volume_init;
}

void APU::TriggerChannel3() {
    // Per SameBoy lines 1550-1574: DMG wave RAM corruption bug
    // If channel is retriggerred 1 cycle before APU reads from it, wave RAM gets corrupted
    if (ch3.enabled && ch3.frequency_timer == 0) {
        unsigned offset = ((ch3.position + 1) >> 1) & 0xF;
        
        // Per SameBoy: corruption behavior depends on sample position
        if (offset < 4) {
            // Copy single byte to start
            wave_ram[0] = wave_ram[offset];
        } else {
            // Copy 4-byte block to start
            uint8_t base = offset & ~3;  // Round down to 4-byte boundary
            wave_ram[0] = wave_ram[base];
            wave_ram[1] = wave_ram[base + 1];
            wave_ram[2] = wave_ram[base + 2];
            wave_ram[3] = wave_ram[base + 3];
        }
    }
    
    ch3.enabled = ch3.dac_enabled;
    // Per SameBoy: only reload length to max if currently 0
    // AND set length_enable = false (un-freeze)
    if (ch3.length_counter == 0) {
        ch3.length_counter = 256;
        ch3.length_enable = false;
    }
    // Per SameBoy line 1586: add +3 delay on trigger
    ch3.frequency_timer = (2048 - ch3.frequency) * 2 + 3;
    ch3.position = 0;
}

void APU::TriggerChannel4() {
    // Per SameBoy: DAC must be enabled to enable channel
    bool dac_on = ch4.volume_init > 0 || ch4.envelope_add;
    if (dac_on) {
        ch4.enabled = true;
    }
    // Per SameBoy: only reload length to max if currently 0
    // AND set length_enable = false (un-freeze)
    if (ch4.length_counter == 0) {
        ch4.length_counter = 64;
        ch4.length_enable = false;
    }
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
    left_sample = (left / 60.0f) * ((((nr50 >> 4) & 7) + 1) / 8.0f);
    right_sample = (right / 60.0f) * (((nr50 & 7) + 1) / 8.0f);
}

void APU::GetSample(float& left, float& right) const {
    left = left_sample;
    right = right_sample;
}

// === Register Access ===

uint8_t APU::ReadRegister(uint16_t addr) const {
    // Per SameBoy apu.c lines 1038-1061: raw value | read_mask
    // The mask bits that are 1 always read as 1 regardless of written value
    static const uint8_t read_mask[23] = {
        // NR1X: NR10, NR11, NR12, NR13, NR14
        0x80, 0x3F, 0x00, 0xFF, 0xBF,
        // NR2X: unused, NR21, NR22, NR23, NR24
        0xFF, 0x3F, 0x00, 0xFF, 0xBF,
        // NR3X: NR30, NR31, NR32, NR33, NR34
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
        // NR4X: unused, NR41, NR42, NR43, NR44
        0xFF, 0xFF, 0x00, 0x00, 0xBF,
        // NR5X: NR50, NR51, NR52 (NR52 handled specially)
        0x00, 0x00,
    };
    
    // NR52 is special - returns power + channel status
    if (addr == 0xFF26) {
        uint8_t value = power_on ? 0x80 : 0x00;
        value |= 0x70;  // Unused bits always 1
        if (ch1.enabled) value |= 0x01;
        if (ch2.enabled) value |= 0x02;
        if (ch3.enabled) value |= 0x04;
        if (ch4.enabled) value |= 0x08;
        return value;
    }
    
    // Unused registers after NR52
    if (addr >= 0xFF27 && addr <= 0xFF2F) {
        return 0xFF;
    }
    
    // Main APU registers: return raw | mask
    if (addr >= 0xFF10 && addr <= 0xFF25) {
        uint8_t idx = addr - 0xFF10;
        return io_registers[idx] | read_mask[idx];
    }
    
    return 0xFF;
}

void APU::WriteRegister(uint16_t addr, uint8_t value) {
    // Per SameBoy lines 1257-1266: on DMG, NRx1 writes are allowed when powered off
    // (allows setting length counters)
    bool is_length_reg = (addr == 0xFF11 || addr == 0xFF16 || addr == 0xFF1B || addr == 0xFF20);
    if (!power_on && addr != 0xFF26 && !is_length_reg) return;
    
    // Per SameBoy lines 1343-1345: when power is off, NRx1 only stores lower 6 bits (length)
    // This masks off the duty bits (upper 2) when off
    uint8_t store_value = value;
    if (!power_on && is_length_reg && addr != 0xFF1B && addr != 0xFF20) {
        store_value = value & 0x3F;  // Square channels: 6-bit length
    }
    // Note: NR31 (0xFF1B) and NR41 (0xFF20) use all 8 bits for length, no masking needed
    
    // Store raw byte for hardware-accurate reads (per SameBoy)
    if (addr >= 0xFF10 && addr <= 0xFF25 && (power_on || is_length_reg)) {
        io_registers[addr - 0xFF10] = store_value;
    }
    
    switch (addr) {
        case 0xFF10: {
            // Negate lockout: switching from negate to non-negate after
            // a calculation using negate mode should disable the channel
            bool old_negate = ch1.sweep_negate;
            bool new_negate = (value >> 3) & 1;
            if (old_negate && !new_negate && ch1.swept_negate) {
                ch1.enabled = false;
            }
            ch1.sweep_period = (value >> 4) & 7;
            ch1.sweep_negate = new_negate;
            ch1.sweep_shift = value & 7;
            break;
        }
        case 0xFF11:
            ch1.duty = (value >> 6) & 3;
            ch1.length_load = value & 0x3F;
            // Per SameBoy: writing to NRx1 immediately reloads the length counter
            ch1.length_counter = 64 - ch1.length_load;
            break;
        case 0xFF12:
            ch1.volume_init = (value >> 4) & 0x0F;
            ch1.envelope_add = (value >> 3) & 1;
            ch1.envelope_period = value & 7;
            // Per SameBoy: DAC is disabled when upper 5 bits are 0
            if ((value & 0xF8) == 0) {
                ch1.enabled = false;
            }
            break;
        case 0xFF13:
            ch1.frequency = (ch1.frequency & 0x700) | value;
            break;
        case 0xFF14: {
            ch1.frequency = (ch1.frequency & 0xFF) | ((value & 7) << 8);
            
            // Per SameBoy: trigger FIRST (may modify length_enabled if counter was 0)
            if (value & 0x80) TriggerChannel1();
            
            // APU glitch: if length is being enabled in first half of period, clock once
            // Per SameBoy: checks (value & 0x40) against CURRENT length_enabled state
            // (which trigger may have just set to false if counter was 0)
            bool new_length_enabled = (value >> 6) & 1;
            if (new_length_enabled && !ch1.length_enable && 
                (frame_sequencer_step & 1) && ch1.length_counter > 0) {
                ch1.length_counter--;
                if (ch1.length_counter == 0) {
                    // If also triggering, reload with max-1 (per SameBoy line 1498)
                    if (value & 0x80) {
                        ch1.length_counter = 63;
                    } else {
                        ch1.enabled = false;
                    }
                }
            }
            
            ch1.length_enable = new_length_enabled;
            break;
        }
            
        case 0xFF16:
            ch2.duty = (value >> 6) & 3;
            ch2.length_load = value & 0x3F;
            // Per SameBoy: writing to NRx1 immediately reloads the length counter
            ch2.length_counter = 64 - ch2.length_load;
            break;
        case 0xFF17:
            ch2.volume_init = (value >> 4) & 0x0F;
            ch2.envelope_add = (value >> 3) & 1;
            ch2.envelope_period = value & 7;
            // Per SameBoy: DAC is disabled when upper 5 bits are 0
            if ((value & 0xF8) == 0) {
                ch2.enabled = false;
            }
            break;
        case 0xFF18:
            ch2.frequency = (ch2.frequency & 0x700) | value;
            break;
        case 0xFF19: {
            ch2.frequency = (ch2.frequency & 0xFF) | ((value & 7) << 8);
            
            // Per SameBoy: trigger FIRST
            if (value & 0x80) TriggerChannel2();
            
            // APU glitch: if length is being enabled in first half of period, clock once
            bool new_length_enabled = (value >> 6) & 1;
            if (new_length_enabled && !ch2.length_enable && 
                (frame_sequencer_step & 1) && ch2.length_counter > 0) {
                ch2.length_counter--;
                if (ch2.length_counter == 0) {
                    if (value & 0x80) {
                        ch2.length_counter = 63;
                    } else {
                        ch2.enabled = false;
                    }
                }
            }
            
            ch2.length_enable = new_length_enabled;
            break;
        }
            
        case 0xFF1A:
            ch3.dac_enabled = (value >> 7) & 1;
            if (!ch3.dac_enabled) ch3.enabled = false;
            break;
        case 0xFF1B:
            ch3.length_load = value;
            // Per SameBoy: writing to NRx1 immediately reloads the length counter
            // Wave channel has 256 length values (8-bit)
            ch3.length_counter = 256 - ch3.length_load;
            break;
        case 0xFF1C:
            ch3.volume_code = (value >> 5) & 3;
            break;
        case 0xFF1D:
            ch3.frequency = (ch3.frequency & 0x700) | value;
            break;
        case 0xFF1E: {
            ch3.frequency = (ch3.frequency & 0xFF) | ((value & 7) << 8);
            
            // Per SameBoy: trigger FIRST
            if (value & 0x80) TriggerChannel3();
            
            // APU glitch: if length is being enabled in first half of period, clock once
            bool new_length_enabled = (value >> 6) & 1;
            if (new_length_enabled && !ch3.length_enable && 
                (frame_sequencer_step & 1) && ch3.length_counter > 0) {
                ch3.length_counter--;
                if (ch3.length_counter == 0) {
                    if (value & 0x80) {
                        ch3.length_counter = 255;
                    } else {
                        ch3.enabled = false;
                    }
                }
            }
            
            ch3.length_enable = new_length_enabled;
            break;
        }
            
        case 0xFF20:
            ch4.length_load = value & 0x3F;
            // Per SameBoy: writing to NRx1 immediately reloads the length counter
            ch4.length_counter = 64 - ch4.length_load;
            break;
        case 0xFF21:
            ch4.volume_init = (value >> 4) & 0x0F;
            ch4.envelope_add = (value >> 3) & 1;
            ch4.envelope_period = value & 7;
            // Per SameBoy: DAC is disabled when upper 5 bits are 0
            if ((value & 0xF8) == 0) {
                ch4.enabled = false;
            }
            break;
        case 0xFF22:
            ch4.clock_shift = (value >> 4) & 0x0F;
            ch4.width_mode = (value >> 3) & 1;
            ch4.divisor_code = value & 7;
            break;
        case 0xFF23: {
            // Per SameBoy: trigger FIRST
            if (value & 0x80) TriggerChannel4();
            
            // APU glitch: if length is being enabled in first half of period, clock once
            bool new_length_enabled = (value >> 6) & 1;
            if (new_length_enabled && !ch4.length_enable && 
                (frame_sequencer_step & 1) && ch4.length_counter > 0) {
                ch4.length_counter--;
                if (ch4.length_counter == 0) {
                    if (value & 0x80) {
                        ch4.length_counter = 63;
                    } else {
                        ch4.enabled = false;
                    }
                }
            }
            
            ch4.length_enable = new_length_enabled;
            break;
        }
            
        case 0xFF24:
            nr50 = value;  // Raw byte storage
            break;
        case 0xFF25:
            channel_left = (value >> 4) & 0x0F;
            channel_right = value & 0x0F;
            break;
        case 0xFF26: {
            bool old_power = power_on;
            bool new_power = (value >> 7) & 1;
            
            // Per SameBoy lines 1291-1316: on DMG, save length counters before power ops
            uint16_t saved_lengths[4] = {
                ch1.length_counter,
                ch2.length_counter,
                ch3.length_counter,
                ch4.length_counter
            };
            
            if (!new_power && old_power) {
                // Power off - clear all APU state
                ch1 = {};
                ch2 = {};
                ch3 = {};
                ch4 = {};
                nr50 = 0;
                channel_left = 0;
                channel_right = 0;
                frame_sequencer_step = 0;
                io_registers.fill(0);  // Clear raw registers for correct reads
            }
            
            power_on = new_power;
            
            // Per SameBoy GB_apu_init: on power-on, reset frame sequencer
            // AND handle skip_div_event glitch if DIV bit 12 is currently high
            if (new_power && !old_power) {
                frame_sequencer_step = 0;
                // Per SameBoy lines 1010-1015: if DIV bit is high on power-on,
                // skip the first DIV-APU event
                if (div_bit12_high) {
                    skip_first_div_event = true;
                }
                // Per SameBoy: on DMG, restore length counters
                ch1.length_counter = saved_lengths[0];
                ch2.length_counter = saved_lengths[1];
                ch3.length_counter = saved_lengths[2];
                ch4.length_counter = saved_lengths[3];
            }
            break;
        }
    }
}

uint8_t APU::ReadWaveRAM(uint8_t index) const {
    // Per SameBoy apu.c lines 1051-1058:
    // On DMG, reading wave RAM while channel 3 is active returns 0xFF
    // UNLESS we're in the wave_form_just_read window (1-cycle access window)
    if (ch3.enabled) {
        if (ch3.wave_form_just_read) {
            // During access window, return the byte at current position
            return wave_ram[ch3.position / 2];
        }
        return 0xFF;
    }
    return wave_ram[index];
}

void APU::WriteWaveRAM(uint8_t index, uint8_t value) {
    wave_ram[index] = value;
}

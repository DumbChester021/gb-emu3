#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstddef>

/**
 * AudioBuffer - Lock-free Ring Buffer for Audio Samples
 * 
 * Thread-safe producer-consumer buffer for passing audio samples
 * from the emulator thread to the SDL audio callback.
 * 
 * - Producer (emulator thread): Push()
 * - Consumer (SDL audio thread): Pop()
 * 
 * Uses atomic operations for thread safety without locks.
 */
class AudioBuffer {
public:
    // Buffer size: ~170ms at 48kHz stereo (power of 2 for fast modulo)
    static constexpr size_t CAPACITY = 8192;
    
    AudioBuffer() : write_pos(0), read_pos(0) {
        buffer.fill({0.0f, 0.0f});
    }
    
    /**
     * Push a stereo sample to the buffer.
     * Called from emulator thread.
     * Returns false if buffer is full.
     */
    bool Push(float left, float right) {
        size_t write = write_pos.load(std::memory_order_relaxed);
        size_t next_write = (write + 1) & (CAPACITY - 1);
        
        // Check if full (write catching up to read)
        if (next_write == read_pos.load(std::memory_order_acquire)) {
            return false;  // Buffer full, drop sample
        }
        
        buffer[write] = {left, right};
        write_pos.store(next_write, std::memory_order_release);
        return true;
    }
    
    /**
     * Pop samples into an interleaved float buffer.
     * Called from SDL audio callback.
     * Fills with silence if not enough samples available.
     */
    void Pop(float* output, size_t sample_count) {
        size_t read = read_pos.load(std::memory_order_relaxed);
        
        for (size_t i = 0; i < sample_count; ++i) {
            size_t write = write_pos.load(std::memory_order_acquire);
            
            if (read != write) {
                // Sample available
                output[i * 2]     = buffer[read].left;
                output[i * 2 + 1] = buffer[read].right;
                read = (read + 1) & (CAPACITY - 1);
            } else {
                // Buffer empty, output silence
                output[i * 2]     = 0.0f;
                output[i * 2 + 1] = 0.0f;
            }
        }
        
        read_pos.store(read, std::memory_order_release);
    }
    
    /**
     * Get the number of samples available for reading.
     */
    size_t Available() const {
        size_t write = write_pos.load(std::memory_order_acquire);
        size_t read = read_pos.load(std::memory_order_acquire);
        return (write - read + CAPACITY) & (CAPACITY - 1);
    }
    
    /**
     * Clear the buffer.
     */
    void Clear() {
        write_pos.store(0, std::memory_order_release);
        read_pos.store(0, std::memory_order_release);
    }

private:
    struct Sample {
        float left;
        float right;
    };
    
    std::array<Sample, CAPACITY> buffer;
    std::atomic<size_t> write_pos;
    std::atomic<size_t> read_pos;
};

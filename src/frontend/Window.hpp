#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include <array>
#include <future>
#include <atomic>

class AudioBuffer;

/**
 * Frontend Window - SDL2 Window and Rendering
 * 
 * Handles:
 * - Window creation and management
 * - Framebuffer display (160x144 scaled)
 * - Audio output via SDL
 * - File dialog for ROM loading
 * - ROM info display
 */
class Window {
public:
    Window();
    ~Window();
    
    // Initialize SDL2 and create window
    bool Init(const std::string& title, int scale = 4);
    
    // Initialize audio with buffer connection
    bool InitAudio(AudioBuffer* buffer);
    void CloseAudio();
    
    // Display framebuffer (2-bit color indices)
    void RenderFrame(const uint8_t* framebuffer);
    
    // Display ROM info screen
    void DisplayROMInfo(const std::string& info);
    
    // Handle events, returns false if quit requested
    bool ProcessEvents();
    
    // Start async file dialog (non-blocking)
    void StartFileDialog();
    
    // Check if file dialog is still running
    bool IsFileDialogOpen() const;
    
    // Get file dialog result (empty if cancelled or still running)
    std::string GetFileDialogResult();
    
    // Check if window is open
    bool IsOpen() const { return window != nullptr; }
    
    // Get key states
    bool IsKeyPressed(SDL_Scancode key) const;
    bool IsKeyJustPressed(SDL_Scancode key) const;
    
    // Get window dimensions
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    
    int width, height;
    int scale;
    
    // Keyboard state
    std::array<bool, SDL_NUM_SCANCODES> keys_current;
    std::array<bool, SDL_NUM_SCANCODES> keys_previous;
    
    // Async file dialog
    std::future<std::string> file_dialog_future;
    std::atomic<bool> file_dialog_running{false};
    
    // Static function for thread
    static std::string RunZenityDialog();
    
    // SameBoy default DMG palette (from display.c line 9)
    // Original: {{{0x08, 0x18, 0x10}, {0x39, 0x61, 0x39}, {0x84, 0xA5, 0x63}, {0xC6, 0xDE, 0x8C}}}
    // Converted from BGR to ARGB8888
    static constexpr uint32_t PALETTE[4] = {
        0xFFD2E6A6,  // Lightest (SameBoy index 4, but we use as white)
        0xFF8CAD63,  // Light
        0xFF396139,  // Dark  
        0xFF101808   // Darkest (black)
    };
    
    // Pixel buffer for texture update
    std::array<uint32_t, 160 * 144> pixels;
    
    bool quit_requested;
    
    // Audio
    SDL_AudioDeviceID audio_device = 0;
    AudioBuffer* audio_buffer = nullptr;
    static void AudioCallback(void* userdata, uint8_t* stream, int len);
};


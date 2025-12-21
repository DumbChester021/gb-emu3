#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include <array>
#include <future>
#include <atomic>

/**
 * Frontend Window - SDL2 Window and Rendering
 * 
 * Handles:
 * - Window creation and management
 * - Framebuffer display (160x144 scaled)
 * - File dialog for ROM loading
 * - ROM info display
 */
class Window {
public:
    Window();
    ~Window();
    
    // Initialize SDL2 and create window
    bool Init(const std::string& title, int scale = 4);
    
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
    
    // DMG color palette (classic green)
    static constexpr uint32_t PALETTE[4] = {
        0xFF9BBC0F,  // Lightest (white)
        0xFF8BAC0F,  // Light
        0xFF306230,  // Dark  
        0xFF0F380F   // Darkest (black)
    };
    
    // Pixel buffer for texture update
    std::array<uint32_t, 160 * 144> pixels;
    
    bool quit_requested;
};


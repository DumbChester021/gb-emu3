#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include <array>
#include <future>
#include <atomic>
#include <deque>
#include <vector>

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
 * - QOL: Volume, mute, FPS, screenshots, notifications
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
    
    // === QOL Features ===
    
    // Volume (0.0 - 1.0)
    void SetVolume(float vol) { volume = std::max(0.0f, std::min(1.0f, vol)); }
    float GetVolume() const { return volume; }
    void AdjustVolume(float delta);
    
    // Mute
    void SetMuted(bool m) { muted = m; }
    bool IsMuted() const { return muted; }
    void ToggleMute() { muted = !muted; }
    
    // FPS display
    void SetShowFPS(bool show) { show_fps = show; }
    bool GetShowFPS() const { return show_fps; }
    void ToggleFPS() { show_fps = !show_fps; }
    
    // Screenshot
    void SaveScreenshot();
    
    // Notifications (auto-dismiss after ~2 seconds)
    void ShowNotification(const std::string& text);
    
    // Save/restore window state
    void SaveWindowState();
    void RestoreWindowState();
    
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
    static constexpr uint32_t PALETTE[4] = {
        0xFFD2E6A6,  // Lightest
        0xFF8CAD63,  // Light
        0xFF396139,  // Dark  
        0xFF101808   // Darkest
    };
    
    // Pixel buffer for texture update
    std::array<uint32_t, 160 * 144> pixels;
    std::vector<uint32_t> last_framebuffer;  // For clean screenshots
    
    bool quit_requested;
    
    // Audio
    SDL_AudioDeviceID audio_device = 0;
    AudioBuffer* audio_buffer = nullptr;
    static void AudioCallback(void* userdata, uint8_t* stream, int len);
    
    // === QOL State ===
    float volume = 1.0f;
    bool muted = false;
    bool show_fps = false;
    int fps_counter = 0;
    int fps_display = 0;
    uint32_t fps_last_time = 0;
    
    // Notification queue
    struct Notification {
        std::string text;
        int frames_remaining;
    };
    std::deque<Notification> notifications;
    
    // Bitmap font rendering
    static const uint8_t FONT[38][8];
    void DrawChar(int x, int y, char c, uint32_t color);
    void DrawString(int x, int y, const std::string& str, uint32_t color);
};

#include "Window.hpp"
#include "Config.hpp"
#include "../apu/AudioBuffer.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>
#include <chrono>

#ifdef __linux__
#include <cstdlib>
#include <cstdio>
#endif

constexpr uint32_t Window::PALETTE[4];

// Bitmap font (0-9, A-Z, :, %)
const uint8_t Window::FONT[38][8] = {
    {0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00}, // 0
    {0x10, 0x30, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00}, // 1
    {0x3C, 0x42, 0x02, 0x0C, 0x30, 0x40, 0x7E, 0x00}, // 2
    {0x3C, 0x42, 0x02, 0x1C, 0x02, 0x42, 0x3C, 0x00}, // 3
    {0x0C, 0x14, 0x24, 0x44, 0x7E, 0x04, 0x04, 0x00}, // 4
    {0x7E, 0x40, 0x7C, 0x02, 0x02, 0x42, 0x3C, 0x00}, // 5
    {0x3C, 0x40, 0x7C, 0x42, 0x42, 0x42, 0x3C, 0x00}, // 6
    {0x7E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00}, // 7
    {0x3C, 0x42, 0x42, 0x3C, 0x42, 0x42, 0x3C, 0x00}, // 8
    {0x3C, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x3C, 0x00}, // 9
    {0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00}, // A
    {0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00}, // B
    {0x3C, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3C, 0x00}, // C
    {0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x00}, // D
    {0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00}, // E
    {0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00}, // F
    {0x3C, 0x42, 0x40, 0x4E, 0x42, 0x42, 0x3C, 0x00}, // G
    {0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00}, // H
    {0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00}, // I
    {0x0E, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00}, // J
    {0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00}, // K
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00}, // L
    {0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x42, 0x00}, // M
    {0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00}, // N
    {0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00}, // O
    {0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00}, // P
    {0x3C, 0x42, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00}, // Q
    {0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00}, // R
    {0x3C, 0x40, 0x40, 0x3C, 0x02, 0x02, 0x3C, 0x00}, // S
    {0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00}, // T
    {0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00}, // U
    {0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00}, // V
    {0x42, 0x42, 0x42, 0x42, 0x5A, 0x66, 0x42, 0x00}, // W
    {0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x42, 0x00}, // X
    {0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x10, 0x00}, // Y
    {0x7E, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7E, 0x00}, // Z
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00}, // :
    {0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00}  // %
};

Window::Window()
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , width(160)
    , height(144)
    , scale(4)
    , quit_requested(false)
{
    keys_current.fill(false);
    keys_previous.fill(false);
    pixels.fill(PALETTE[0]);
    last_framebuffer.resize(160 * 144);
}

Window::~Window() {
    SaveWindowState();
    CloseAudio();
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

bool Window::Init(const std::string& title, int window_scale) {
    scale = window_scale;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Load saved position or use centered
    int x = Config::Instance().GetInt("WindowX", SDL_WINDOWPOS_CENTERED);
    int y = Config::Instance().GetInt("WindowY", SDL_WINDOWPOS_CENTERED);
    int w = Config::Instance().GetInt("WindowWidth", 160 * scale);
    int h = Config::Instance().GetInt("WindowHeight", 144 * scale);
    
    window = SDL_CreateWindow(
        title.c_str(), x, y, w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Restore maximized state
    if (Config::Instance().GetInt("Maximized", 0)) {
        SDL_MaximizeWindow(window);
    }
    
    renderer = SDL_CreateRenderer(window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    SDL_RenderSetLogicalSize(renderer, 160, 144);
    
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        160, 144
    );
    
    if (!texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Load saved settings
    volume = Config::Instance().GetFloat("Volume", 1.0f);
    muted = Config::Instance().GetInt("Muted", 0) != 0;
    show_fps = Config::Instance().GetInt("ShowFPS", 0) != 0;
    
    return true;
}

void Window::SaveWindowState() {
    if (!window) return;
    
    Uint32 flags = SDL_GetWindowFlags(window);
    bool maximized = flags & SDL_WINDOW_MAXIMIZED;
    
    Config::Instance().SetInt("Maximized", maximized ? 1 : 0);
    
    if (!maximized) {
        int x, y, w, h;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GetWindowSize(window, &w, &h);
        Config::Instance().SetInt("WindowX", x);
        Config::Instance().SetInt("WindowY", y);
        Config::Instance().SetInt("WindowWidth", w);
        Config::Instance().SetInt("WindowHeight", h);
    }
    
    Config::Instance().SetFloat("Volume", volume);
    Config::Instance().SetInt("Muted", muted ? 1 : 0);
    Config::Instance().SetInt("ShowFPS", show_fps ? 1 : 0);
    Config::Instance().Save();
}

void Window::RestoreWindowState() {
    // Called from Init, already handled there
}

bool Window::InitAudio(AudioBuffer* buffer) {
    if (!buffer) return false;
    
    audio_buffer = buffer;
    
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 48000;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = AudioCallback;
    want.userdata = this;  // Pass Window* for volume/mute
    
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device == 0) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    SDL_PauseAudioDevice(audio_device, 0);
    
    std::cout << "Audio initialized: " << have.freq << "Hz, " 
              << (int)have.channels << " channels, "
              << have.samples << " samples\n";
    
    return true;
}

void Window::CloseAudio() {
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    audio_buffer = nullptr;
}

void Window::AudioCallback(void* userdata, uint8_t* stream, int len) {
    Window* win = static_cast<Window*>(userdata);
    float* output = reinterpret_cast<float*>(stream);
    size_t sample_count = len / sizeof(float) / 2;
    
    if (win && win->audio_buffer) {
        // Check focus - mute if window not focused
        bool should_mute = win->muted;
        if (win->window) {
            Uint32 flags = SDL_GetWindowFlags(win->window);
            if (!(flags & SDL_WINDOW_INPUT_FOCUS)) {
                should_mute = true;  // Mute when not focused
            }
        }
        
        if (should_mute) {
            std::memset(stream, 0, len);
        } else {
            win->audio_buffer->Pop(output, sample_count);
            // Apply volume
            if (win->volume < 1.0f) {
                for (size_t i = 0; i < sample_count * 2; i++) {
                    output[i] *= win->volume;
                }
            }
        }
    } else {
        std::memset(stream, 0, len);
    }
}

void Window::RenderFrame(const uint8_t* framebuffer) {
    // Update FPS counter
    fps_counter++;
    uint32_t now = SDL_GetTicks();
    if (now - fps_last_time >= 1000) {
        fps_display = fps_counter;
        fps_counter = 0;
        fps_last_time = now;
    }
    
    // Convert 2-bit indices to ARGB8888
    for (int i = 0; i < 160 * 144; i++) {
        pixels[i] = PALETTE[framebuffer[i] & 0x03];
    }
    
    // Store for screenshots (clean, no OSD)
    std::copy(pixels.begin(), pixels.end(), last_framebuffer.begin());
    
    SDL_UpdateTexture(texture, nullptr, pixels.data(), 160 * sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    
    // Draw OSD
    if (show_fps) {
        std::string fps_str = "FPS:" + std::to_string(fps_display);
        DrawString(2, 144 - 10, fps_str, 0xFFFFFF00);
    }
    
    // Draw notifications (stacked from top)
    int notify_y = 2;
    for (auto it = notifications.begin(); it != notifications.end(); ) {
        DrawString(2, notify_y, it->text, 0xFFFFFFFF);
        notify_y += 10;
        it->frames_remaining--;
        if (it->frames_remaining <= 0) {
            it = notifications.erase(it);
        } else {
            ++it;
        }
    }
    
    SDL_RenderPresent(renderer);
}

void Window::DisplayROMInfo(const std::string& info) {
    SDL_SetRenderDrawColor(renderer, 0x10, 0x18, 0x08, 0xFF);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    std::cout << "\n" << info << "\n";
}

bool Window::ProcessEvents() {
    keys_previous = keys_current;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                quit_requested = true;
                return false;
                
            case SDL_KEYDOWN:
                if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                    keys_current[event.key.keysym.scancode] = true;
                }
                
                // Handle QOL keys
                switch (event.key.keysym.sym) {
                    case SDLK_m: ToggleMute(); ShowNotification(muted ? "MUTED" : "UNMUTED"); break;
                    case SDLK_F3: ToggleFPS(); break;
                    case SDLK_F12: SaveScreenshot(); break;
                    case SDLK_EQUALS:
                    case SDLK_PLUS: AdjustVolume(0.1f); break;
                    case SDLK_MINUS: AdjustVolume(-0.1f); break;
                }
                break;
                
            case SDL_KEYUP:
                if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                    keys_current[event.key.keysym.scancode] = false;
                }
                break;
                
            case SDL_DROPFILE:
                SDL_free(event.drop.file);
                break;
        }
    }
    
    return true;
}

bool Window::IsKeyPressed(SDL_Scancode key) const {
    return keys_current[key];
}

bool Window::IsKeyJustPressed(SDL_Scancode key) const {
    return keys_current[key] && !keys_previous[key];
}

void Window::AdjustVolume(float delta) {
    volume = std::max(0.0f, std::min(1.0f, volume + delta));
    int percent = static_cast<int>(volume * 100);
    ShowNotification("VOL:" + std::to_string(percent) + "%");
}

void Window::SaveScreenshot() {
    if (last_framebuffer.empty()) return;
    
    std::filesystem::create_directories("screenshots");
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char filename[64];
    std::strftime(filename, sizeof(filename), "screenshots/%Y%m%d_%H%M%S.bmp", std::localtime(&time));
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 160, 144, 32, SDL_PIXELFORMAT_ARGB8888);
    std::memcpy(surface->pixels, last_framebuffer.data(), 160 * 144 * sizeof(uint32_t));
    SDL_SaveBMP(surface, filename);
    SDL_FreeSurface(surface);
    
    std::cout << "Screenshot saved: " << filename << "\n";
    ShowNotification("SCREENSHOT SAVED");
}

void Window::ShowNotification(const std::string& text) {
    notifications.push_back({text, 120});  // 2 seconds at 60fps
    while (notifications.size() > 5) {
        notifications.pop_front();
    }
}

void Window::DrawChar(int x, int y, char c, uint32_t color) {
    int index = -1;
    if (c >= '0' && c <= '9') index = c - '0';
    else if (c >= 'A' && c <= 'Z') index = 10 + (c - 'A');
    else if (c >= 'a' && c <= 'z') index = 10 + (c - 'a');
    else if (c == ':') index = 36;
    else if (c == '%') index = 37;
    if (index == -1) return;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Draw black outline
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for (int row = 0; row < 8; row++) {
        uint8_t line = FONT[index][row];
        for (int col = 0; col < 8; col++) {
            if (line & (1 << (7 - col))) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        SDL_Rect rect = {x + col + dx, y + row + dy, 1, 1};
                        SDL_RenderFillRect(renderer, &rect);
                    }
                }
            }
        }
    }
    
    // Draw text
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (int row = 0; row < 8; row++) {
        uint8_t line = FONT[index][row];
        for (int col = 0; col < 8; col++) {
            if (line & (1 << (7 - col))) {
                SDL_Rect rect = {x + col, y + row, 1, 1};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
}

void Window::DrawString(int x, int y, const std::string& str, uint32_t color) {
    int curX = x;
    for (char c : str) {
        DrawChar(curX, y, c, color);
        curX += 8;
    }
}

std::string Window::RunZenityDialog() {
#ifdef __linux__
    std::string lastDir = Config::Instance().Get("LastROMDir", ".");
    std::string cmd = "zenity --file-selection --filename=\"" + lastDir + "/\" --file-filter='Game Boy ROMs | *.gb *.gbc *.rom' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    
    char buffer[512];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        result = buffer;
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
    }
    pclose(pipe);
    
    // Save directory for next time
    if (!result.empty()) {
        std::filesystem::path p(result);
        Config::Instance().Set("LastROMDir", p.parent_path().string());
        Config::Instance().Save();
    }
    
    return result;
#else
    return "";
#endif
}

void Window::StartFileDialog() {
    if (file_dialog_running) return;
    file_dialog_running = true;
    file_dialog_future = std::async(std::launch::async, &Window::RunZenityDialog);
}

bool Window::IsFileDialogOpen() const {
    return file_dialog_running;
}

std::string Window::GetFileDialogResult() {
    if (!file_dialog_running) return "";
    if (file_dialog_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        file_dialog_running = false;
        return file_dialog_future.get();
    }
    return "";
}

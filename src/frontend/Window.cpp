#include "Window.hpp"
#include "../apu/AudioBuffer.hpp"
#include <iostream>
#include <cstring>

#ifdef __linux__
#include <cstdlib>
#include <cstdio>
#endif

constexpr uint32_t Window::PALETTE[4];

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
}

Window::~Window() {
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
    
    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED,
        160 * scale, 144 * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    renderer = SDL_CreateRenderer(window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Keep aspect ratio
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
    
    return true;
}

bool Window::InitAudio(AudioBuffer* buffer) {
    if (!buffer) return false;
    
    audio_buffer = buffer;
    
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 48000;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;  // ~21ms buffer at 48kHz
    want.callback = AudioCallback;
    want.userdata = audio_buffer;
    
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device == 0) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        return false;
    }
    
    // Start playback
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
    AudioBuffer* buffer = static_cast<AudioBuffer*>(userdata);
    float* output = reinterpret_cast<float*>(stream);
    size_t sample_count = len / sizeof(float) / 2;  // Stereo
    
    buffer->Pop(output, sample_count);
}

void Window::RenderFrame(const uint8_t* framebuffer) {
    // Convert 2-bit indices to ARGB8888
    for (int i = 0; i < 160 * 144; i++) {
        pixels[i] = PALETTE[framebuffer[i] & 0x03];
    }
    
    SDL_UpdateTexture(texture, nullptr, pixels.data(), 160 * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Window::DisplayROMInfo(const std::string& info) {
    // Clear to dark green background
    SDL_SetRenderDrawColor(renderer, 0x0F, 0x38, 0x0F, 0xFF);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    
    // Print info to console (SDL2 text rendering would require SDL_ttf)
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
                
                // Handle Escape to quit
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    quit_requested = true;
                    return false;
                }
                break;
                
            case SDL_KEYUP:
                if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                    keys_current[event.key.keysym.scancode] = false;
                }
                break;
                
            case SDL_DROPFILE:
                // Handle drag & drop of ROM files
                // Store for later retrieval
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

std::string Window::RunZenityDialog() {
#ifdef __linux__
    FILE* pipe = popen("zenity --file-selection --file-filter='Game Boy ROMs | *.gb *.gbc *.rom' 2>/dev/null", "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[512];
    std::string result;
    
    if (fgets(buffer, sizeof(buffer), pipe)) {
        result = buffer;
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
    }
    
    pclose(pipe);
    return result;
#else
    return "";
#endif
}

void Window::StartFileDialog() {
    if (file_dialog_running) return;  // Already running
    
    file_dialog_running = true;
    file_dialog_future = std::async(std::launch::async, &Window::RunZenityDialog);
}

bool Window::IsFileDialogOpen() const {
    return file_dialog_running;
}

std::string Window::GetFileDialogResult() {
    if (!file_dialog_running) return "";
    
    // Check if future is ready
    if (file_dialog_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        file_dialog_running = false;
        return file_dialog_future.get();
    }
    
    return "";  // Still running
}


#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <filesystem>

#include "Emulator.hpp"
#include "frontend/Window.hpp"
#include "cartridge/Cartridge.hpp"
#include "apu/AudioBuffer.hpp"

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " [options] [rom_file]\n"
              << "\nOptions:\n"
              << "  --boot-rom <file>   Load boot ROM\n"
              << "  --headless          Run without display (for testing)\n"
              << "  --cycles <n>        Run for N cycles then exit\n"
              << "  --dump-screen <f>   Dump screen to PGM file on exit\n"
              << "  --scale <n>         Window scale (1-8, default: 4)\n"
              << "  --help              Show this help\n"
              << "\nIf no ROM file is specified, a file dialog will open.\n";
}

struct Args {
    std::string rom_path;
    std::string boot_rom_path;
    std::string dump_screen_path;
    bool headless = false;
    uint64_t max_cycles = 0;
    int scale = 4;
};

bool ParseArgs(int argc, char* argv[], Args& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        } else if (arg == "--boot-rom" && i + 1 < argc) {
            args.boot_rom_path = argv[++i];
        } else if (arg == "--headless") {
            args.headless = true;
        } else if (arg == "--cycles" && i + 1 < argc) {
            args.max_cycles = std::stoull(argv[++i]);
        } else if (arg == "--dump-screen" && i + 1 < argc) {
            args.dump_screen_path = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            args.scale = std::stoi(argv[++i]);
            if (args.scale < 1) args.scale = 1;
            if (args.scale > 8) args.scale = 8;
        } else if (arg[0] != '-') {
            args.rom_path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

// Dump framebuffer to PGM file (grayscale)
void DumpScreen(const Emulator& emu, const std::string& path) {
    // Create directory if needed
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    
    const uint8_t* fb = emu.GetFramebuffer();
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to write screen dump: " << path << "\n";
        return;
    }
    // PGM header: P5 (binary grayscale), 160x144, max value 255
    file << "P5\n160 144\n255\n";
    // Convert 0-3 to 0-255 grayscale (inverted: 0=white, 3=black)
    for (int i = 0; i < 160 * 144; i++) {
        uint8_t color = fb[i] & 0x03;
        uint8_t gray = 255 - (color * 85);  // 0->255, 1->170, 2->85, 3->0
        file.put(static_cast<char>(gray));
    }
    std::cout << "Screen dumped to: " << path << "\n";
}

// Helper to extract test name from ROM path
std::string GetTestName(const std::string& rom_path) {
    std::filesystem::path p(rom_path);
    return p.stem().string();
}

int RunHeadless(Emulator& emu, uint64_t max_cycles, const std::string& rom_path, const std::string& dump_path = "") {
    std::string serial_output;
    uint64_t cycles = 0;
    uint64_t target = max_cycles > 0 ? max_cycles : 30000000;
    
    // Mooneye test result (set by callback)
    int mooneye_result = -1;  // -1 = not done, 0 = pass, 1 = fail
    
    // Set up Mooneye detection callback
    emu.SetMooneyeCallback([&mooneye_result](bool passed) {
        mooneye_result = passed ? 0 : 1;
    });
    
    auto start = std::chrono::high_resolution_clock::now();
    
    while (cycles < target && mooneye_result < 0) {
        cycles += emu.Step();
        
        // Blargg-style serial detection
        if (emu.IsSerialTransferComplete()) {
            char c = static_cast<char>(emu.GetSerialData());
            serial_output += c;
            emu.ClearSerialTransferComplete();
            std::cout << c << std::flush;
            
            if (serial_output.find("Passed") != std::string::npos ||
                serial_output.find("passed") != std::string::npos) {
                std::cout << "\n\n=== TEST PASSED (Blargg) ===\n";
                return 0;
            }
            if (serial_output.find("Failed") != std::string::npos ||
                serial_output.find("failed") != std::string::npos) {
                std::cout << "\n\n=== TEST FAILED (Blargg) ===\n";
                return 1;
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Mooneye result check
    if (mooneye_result == 0) {
        std::cout << "\n\n=== TEST PASSED (Mooneye) ===\n";
        if (!dump_path.empty()) DumpScreen(emu, dump_path);
        return 0;
    } else if (mooneye_result == 1) {
        std::cout << "\n\n=== TEST FAILED (Mooneye) ===\n";
        DumpScreen(emu, "test_dumps/" + GetTestName(rom_path) + ".pgm");  // Dump to test_dumps with test name
        if (!dump_path.empty()) DumpScreen(emu, dump_path);
        return 1;
    }
    
    std::cout << "\n\nExecuted " << cycles << " cycles in " << duration.count() << "ms\n";
    if (duration.count() > 0) {
        std::cout << "Speed: " << (cycles * 1000.0 / duration.count() / 4194304.0) << "x realtime\n";
    }
    std::cout << "\nCPU State: PC=$" << std::hex << emu.GetPC() 
              << " SP=$" << emu.GetSP()
              << " AF=$" << emu.GetAF() << std::dec << "\n";
    
    if (!serial_output.empty()) {
        std::cout << "\nSerial output: " << serial_output << "\n";
    } else {
        std::cout << "\nNo serial output received.\n";
    }
    return 0;
}

int RunGUI(Emulator& emu, Window& window, const std::string& rom_info, const std::string& save_path) {
    window.DisplayROMInfo(rom_info);
    
    // Initialize audio
    AudioBuffer audio_buffer;
    if (window.InitAudio(&audio_buffer)) {
        emu.ConnectAudioBuffer(&audio_buffer);
    }
    
    std::cout << "\n=== Starting Emulation ===\n";
    std::cout << "Controls: Arrows = D-Pad, Z = A, X = B, RShift = Select, Enter = Start\n";
    std::cout << "Press ESC to quit\n\n";
    
    // FPS tracking
    int frame_count = 0;
    int fps_frame_count = 0;
    auto fps_start = std::chrono::high_resolution_clock::now();
    
    // Frame timing for 59.7275 Hz (DMG refresh rate)
    // 70224 T-cycles per frame at 4.194304 MHz = 16.742706... ms per frame
    constexpr auto FRAME_DURATION = std::chrono::nanoseconds(16742706);
    auto frame_start = std::chrono::high_resolution_clock::now();
    
    while (window.ProcessEvents()) {
        emu.RunFrame();
        frame_count++;
        fps_frame_count++;
        
        // FPS tracking: print every second
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fps_start);
        if (elapsed.count() >= 1000) {
            double fps = fps_frame_count * 1000.0 / elapsed.count();
            std::cerr << "[FPS: " << std::fixed << std::setprecision(1) << fps << "] "
                      << "PC=$" << std::hex << emu.GetPC() << std::dec << std::endl;
            fps_frame_count = 0;
            fps_start = now;
        }
        
        window.RenderFrame(emu.GetFramebuffer());
        
        // Software frame limiter for 59.7275 Hz
        // VSYNC may not work on all systems, so we add this fallback
        auto frame_end = std::chrono::high_resolution_clock::now();
        auto frame_elapsed = frame_end - frame_start;
        if (frame_elapsed < FRAME_DURATION) {
            std::this_thread::sleep_for(FRAME_DURATION - frame_elapsed);
        }
        frame_start = std::chrono::high_resolution_clock::now();
        
        // Serial output to console
        while (emu.IsSerialTransferComplete()) {
            char c = static_cast<char>(emu.GetSerialData());
            std::cout << c << std::flush;
            emu.ClearSerialTransferComplete();
        }
        
        // Handle input
        emu.SetButton(4, window.IsKeyPressed(SDL_SCANCODE_RIGHT));
        emu.SetButton(5, window.IsKeyPressed(SDL_SCANCODE_LEFT));
        emu.SetButton(6, window.IsKeyPressed(SDL_SCANCODE_UP));
        emu.SetButton(7, window.IsKeyPressed(SDL_SCANCODE_DOWN));
        emu.SetButton(0, window.IsKeyPressed(SDL_SCANCODE_Z));
        emu.SetButton(1, window.IsKeyPressed(SDL_SCANCODE_X));
        emu.SetButton(2, window.IsKeyPressed(SDL_SCANCODE_RSHIFT));
        emu.SetButton(3, window.IsKeyPressed(SDL_SCANCODE_RETURN));
    }
    
    // Save battery-backed RAM on exit
    if (emu.HasBattery() && !save_path.empty()) {
        if (emu.SaveRAM(save_path)) {
            std::cout << "Saved to: " << save_path << "\n";
        } else {
            std::cerr << "Failed to save: " << save_path << "\n";
        }
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << R"(
   ╔═══════════════════════════════════════╗
   ║         GB-EMU3 - DMG Emulator        ║
   ║     Hardware Accurate • T-Cycles      ║
   ╚═══════════════════════════════════════╝
)" << "\n";
    
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        return 1;
    }
    
    Window window;
    if (!args.headless) {
        if (!window.Init("GB-EMU3", args.scale)) {
            std::cerr << "Failed to initialize window\n";
            return 1;
        }
    }
    
    if (args.rom_path.empty() && !args.headless) {
        std::cout << "No ROM specified, opening file dialog...\n";
        
        // Start async file dialog
        window.StartFileDialog();
        
        // Keep processing SDL events while dialog is open
        while (window.IsFileDialogOpen()) {
            if (!window.ProcessEvents()) {
                return 0;  // Window closed
            }
            SDL_Delay(16);  // ~60fps polling
            
            // Check for result
            std::string result = window.GetFileDialogResult();
            if (!result.empty()) {
                args.rom_path = result;
                break;
            }
        }
        
        // Get final result if not already set
        if (args.rom_path.empty()) {
            args.rom_path = window.GetFileDialogResult();
        }
        
        if (args.rom_path.empty()) {
            std::cerr << "No ROM selected\n";
            return 1;
        }
    } else if (args.rom_path.empty()) {
        std::cerr << "Error: No ROM file specified\n";
        PrintUsage(argv[0]);
        return 1;
    }
    
    Cartridge cart;
    if (!cart.LoadROM(args.rom_path)) {
        std::cerr << "Failed to load ROM: " << args.rom_path << "\n";
        return 1;
    }
    
    std::string rom_info = cart.GetDetailedInfo();
    
    Emulator emu;
    
    if (!args.boot_rom_path.empty()) {
        if (!emu.LoadBootROM(args.boot_rom_path)) {
            std::cerr << "Failed to load boot ROM: " << args.boot_rom_path << "\n";
            return 1;
        }
        std::cout << "Boot ROM loaded: " << args.boot_rom_path << "\n";
    }
    
    if (!emu.LoadROM(args.rom_path)) {
        std::cerr << "Failed to load ROM into emulator\n";
        return 1;
    }
    
    // Calculate save path (replace .gb/.gbc with .sav)
    std::string save_path = args.rom_path;
    size_t dot_pos = save_path.rfind('.');
    if (dot_pos != std::string::npos) {
        save_path = save_path.substr(0, dot_pos) + ".sav";
    } else {
        save_path += ".sav";
    }
    
    // Load existing save if battery-backed
    if (emu.HasBattery()) {
        if (emu.LoadSave(save_path)) {
            std::cout << "Loaded save: " << save_path << "\n";
        }
    }
    
    emu.Reset();
    
    if (args.headless) {
        return RunHeadless(emu, args.max_cycles, args.rom_path, args.dump_screen_path);
    } else {
        return RunGUI(emu, window, rom_info, save_path);
    }
}

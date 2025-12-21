#include "Cartridge.hpp"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Nintendo logo for validation (first 24 bytes shown)
static constexpr uint8_t NINTENDO_LOGO[] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

// Cartridge type names for display
static const char* GetCartridgeTypeName(uint8_t type) {
    switch (type) {
        case 0x00: return "ROM ONLY";
        case 0x01: return "MBC1";
        case 0x02: return "MBC1+RAM";
        case 0x03: return "MBC1+RAM+BATTERY";
        case 0x05: return "MBC2";
        case 0x06: return "MBC2+BATTERY";
        case 0x08: return "ROM+RAM";
        case 0x09: return "ROM+RAM+BATTERY";
        case 0x0B: return "MMM01";
        case 0x0C: return "MMM01+RAM";
        case 0x0D: return "MMM01+RAM+BATTERY";
        case 0x0F: return "MBC3+TIMER+BATTERY";
        case 0x10: return "MBC3+TIMER+RAM+BATTERY";
        case 0x11: return "MBC3";
        case 0x12: return "MBC3+RAM";
        case 0x13: return "MBC3+RAM+BATTERY";
        case 0x19: return "MBC5";
        case 0x1A: return "MBC5+RAM";
        case 0x1B: return "MBC5+RAM+BATTERY";
        case 0x1C: return "MBC5+RUMBLE";
        case 0x1D: return "MBC5+RUMBLE+RAM";
        case 0x1E: return "MBC5+RUMBLE+RAM+BATTERY";
        case 0x20: return "MBC6";
        case 0x22: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
        case 0xFC: return "POCKET CAMERA";
        case 0xFD: return "BANDAI TAMA5";
        case 0xFE: return "HuC3";
        case 0xFF: return "HuC1+RAM+BATTERY";
        default: return "UNKNOWN";
    }
}

// ROM size lookup
static size_t GetROMSize(uint8_t code) {
    if (code <= 0x08) {
        return 32768 << code;  // 32KB << code
    }
    // Special cases
    switch (code) {
        case 0x52: return 72 * 16384;   // 1.1MB
        case 0x53: return 80 * 16384;   // 1.2MB
        case 0x54: return 96 * 16384;   // 1.5MB
        default: return 32768;
    }
}

// ROM bank count
static uint16_t GetROMBankCount(uint8_t code) {
    return static_cast<uint16_t>(GetROMSize(code) / 16384);
}

// RAM size lookup
static size_t GetRAMSizeFromCode(uint8_t code) {
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 2048;         // 2KB (unofficial)
        case 0x02: return 8192;         // 8KB
        case 0x03: return 32768;        // 32KB (4 banks)
        case 0x04: return 131072;       // 128KB (16 banks)
        case 0x05: return 65536;        // 64KB (8 banks)
        default: return 0;
    }
}

// Destination code
static const char* GetDestination(uint8_t code) {
    return code == 0x00 ? "Japan" : "Overseas";
}

// Licensee lookup
static const char* GetLicensee(uint8_t old_code, const char* new_code) {
    if (old_code == 0x33) {
        // Use new licensee code
        if (new_code[0] == '0' && new_code[1] == '1') return "Nintendo";
        if (new_code[0] == '0' && new_code[1] == '8') return "Capcom";
        if (new_code[0] == '1' && new_code[1] == '3') return "EA";
        if (new_code[0] == '1' && new_code[1] == '8') return "Hudson Soft";
        if (new_code[0] == '1' && new_code[1] == '9') return "B-AI";
        if (new_code[0] == '2' && new_code[1] == '0') return "KSS";
        if (new_code[0] == '2' && new_code[1] == '2') return "POW";
        if (new_code[0] == '2' && new_code[1] == '4') return "PCM Complete";
        if (new_code[0] == '2' && new_code[1] == '5') return "San-X";
        if (new_code[0] == '2' && new_code[1] == '8') return "Kemco Japan";
        if (new_code[0] == '2' && new_code[1] == '9') return "SETA";
        if (new_code[0] == '3' && new_code[1] == '0') return "Viacom";
        if (new_code[0] == '3' && new_code[1] == '1') return "Nintendo";
        if (new_code[0] == '3' && new_code[1] == '2') return "Bandai";
        if (new_code[0] == '3' && new_code[1] == '3') return "Ocean/Acclaim";
        if (new_code[0] == '3' && new_code[1] == '4') return "Konami";
        if (new_code[0] == '3' && new_code[1] == '5') return "Hector";
        if (new_code[0] == '4' && new_code[1] == '1') return "Ubisoft";
        if (new_code[0] == '4' && new_code[1] == '2') return "Atlus";
        if (new_code[0] == '4' && new_code[1] == '4') return "Malibu";
        if (new_code[0] == '4' && new_code[1] == '6') return "Angel";
        if (new_code[0] == '4' && new_code[1] == '7') return "Bullet-Proof";
        if (new_code[0] == '4' && new_code[1] == '9') return "Irem";
        if (new_code[0] == '5' && new_code[1] == '0') return "Absolute";
        if (new_code[0] == '5' && new_code[1] == '1') return "Acclaim";
        if (new_code[0] == '5' && new_code[1] == '2') return "Activision";
        if (new_code[0] == '5' && new_code[1] == '3') return "American Sammy";
        if (new_code[0] == '5' && new_code[1] == '4') return "Konami";
        if (new_code[0] == '5' && new_code[1] == '5') return "Hi Tech Entertainment";
        if (new_code[0] == '5' && new_code[1] == '6') return "LJN";
        if (new_code[0] == '5' && new_code[1] == '7') return "Matchbox";
        if (new_code[0] == '5' && new_code[1] == '8') return "Mattel";
        if (new_code[0] == '5' && new_code[1] == '9') return "Milton Bradley";
        if (new_code[0] == '6' && new_code[1] == '0') return "Titus";
        if (new_code[0] == '6' && new_code[1] == '1') return "Virgin";
        if (new_code[0] == '6' && new_code[1] == '4') return "LucasArts";
        if (new_code[0] == '6' && new_code[1] == '7') return "Ocean";
        if (new_code[0] == '6' && new_code[1] == '9') return "EA";
        if (new_code[0] == '7' && new_code[1] == '0') return "Infogrames";
        if (new_code[0] == '7' && new_code[1] == '1') return "Interplay";
        if (new_code[0] == '7' && new_code[1] == '2') return "Broderbund";
        if (new_code[0] == '7' && new_code[1] == '3') return "Sculptured";
        if (new_code[0] == '7' && new_code[1] == '5') return "SCI";
        if (new_code[0] == '7' && new_code[1] == '8') return "THQ";
        if (new_code[0] == '7' && new_code[1] == '9') return "Accolade";
        if (new_code[0] == '8' && new_code[1] == '0') return "Misawa";
        if (new_code[0] == '8' && new_code[1] == '3') return "LOZC";
        if (new_code[0] == '8' && new_code[1] == '6') return "Tokuma Shoten";
        if (new_code[0] == '8' && new_code[1] == '7') return "Tsukuda Ori";
        if (new_code[0] == '9' && new_code[1] == '1') return "Chunsoft";
        if (new_code[0] == '9' && new_code[1] == '2') return "Video System";
        if (new_code[0] == '9' && new_code[1] == '3') return "Ocean/Acclaim";
        if (new_code[0] == '9' && new_code[1] == '5') return "Varie";
        if (new_code[0] == '9' && new_code[1] == '6') return "Yonezawa/S'Pal";
        if (new_code[0] == '9' && new_code[1] == '7') return "Kaneko";
        if (new_code[0] == '9' && new_code[1] == '9') return "Pack-In-Video";
        if (new_code[0] == 'A' && new_code[1] == '4') return "Konami";
        return "Unknown";
    }
    
    // Old licensee code
    switch (old_code) {
        case 0x00: return "None";
        case 0x01: return "Nintendo";
        case 0x08: return "Capcom";
        case 0x09: return "Hot-B";
        case 0x0A: return "Jaleco";
        case 0x0B: return "Coconuts";
        case 0x0C: return "Elite Systems";
        case 0x13: return "EA";
        case 0x18: return "Hudson Soft";
        case 0x19: return "ITC Entertainment";
        case 0x1A: return "Yanoman";
        case 0x1D: return "Clary";
        case 0x1F: return "Virgin";
        case 0x24: return "PCM Complete";
        case 0x25: return "San-X";
        case 0x28: return "Kotobuki Systems";
        case 0x29: return "SETA";
        case 0x30: return "Infogrames";
        case 0x31: return "Nintendo";
        case 0x32: return "Bandai";
        case 0x34: return "Konami";
        case 0x35: return "Hector";
        case 0x38: return "Capcom";
        case 0x39: return "Banpresto";
        case 0x3C: return "Entertainment i";
        case 0x3E: return "Gremlin";
        case 0x41: return "Ubisoft";
        case 0x42: return "Atlus";
        case 0x44: return "Malibu";
        case 0x46: return "Angel";
        case 0x47: return "Spectrum Holoby";
        case 0x49: return "Irem";
        case 0x4A: return "Virgin";
        case 0x4D: return "Malibu";
        case 0x4F: return "U.S. Gold";
        case 0x50: return "Absolute";
        case 0x51: return "Acclaim";
        case 0x52: return "Activision";
        case 0x53: return "American Sammy";
        case 0x54: return "GameTek";
        case 0x55: return "Park Place";
        case 0x56: return "LJN";
        case 0x57: return "Matchbox";
        case 0x59: return "Milton Bradley";
        case 0x5A: return "Mindscape";
        case 0x5B: return "Romstar";
        case 0x5C: return "Naxat Soft";
        case 0x5D: return "Tradewest";
        case 0x60: return "Titus";
        case 0x61: return "Virgin";
        case 0x67: return "Ocean";
        case 0x69: return "EA";
        case 0x6E: return "Elite Systems";
        case 0x6F: return "Electro Brain";
        case 0x70: return "Infogrames";
        case 0x71: return "Interplay";
        case 0x72: return "Broderbund";
        case 0x73: return "Sculptered Soft";
        case 0x75: return "The Sales Curve";
        case 0x78: return "THQ";
        case 0x79: return "Accolade";
        case 0x7A: return "Triffix Entertainment";
        case 0x7C: return "Microprose";
        case 0x7F: return "Kemco";
        case 0x80: return "Misawa Entertainment";
        case 0x83: return "LOZC";
        case 0x86: return "Tokuma Shoten";
        case 0x8B: return "Bullet-Proof Software";
        case 0x8C: return "Vic Tokai";
        case 0x8E: return "Ape";
        case 0x8F: return "I'Max";
        case 0x91: return "Chunsoft";
        case 0x92: return "Video System";
        case 0x93: return "Tsuburava";
        case 0x95: return "Varie";
        case 0x96: return "Yonezawa/S'Pal";
        case 0x97: return "Kaneko";
        case 0x99: return "Arc";
        case 0x9A: return "Nihon Bussan";
        case 0x9B: return "Tecmo";
        case 0x9C: return "Imagineer";
        case 0x9D: return "Banpresto";
        case 0x9F: return "Nova";
        case 0xA1: return "Hori Electric";
        case 0xA2: return "Bandai";
        case 0xA4: return "Konami";
        case 0xA6: return "Kawada";
        case 0xA7: return "Takara";
        case 0xA9: return "Technos Japan";
        case 0xAA: return "Broderbund";
        case 0xAC: return "Toei Animation";
        case 0xAD: return "Toho";
        case 0xAF: return "Namco";
        case 0xB0: return "Acclaim";
        case 0xB1: return "ASCII/Nexoft";
        case 0xB2: return "Bandai";
        case 0xB4: return "Enix";
        case 0xB6: return "HAL";
        case 0xB7: return "SNK";
        case 0xB9: return "Pony Canyon";
        case 0xBA: return "Culture Brain";
        case 0xBB: return "Sunsoft";
        case 0xBD: return "Sony Imagesoft";
        case 0xBF: return "Sammy";
        case 0xC0: return "Taito";
        case 0xC2: return "Kemco";
        case 0xC3: return "Squaresoft";
        case 0xC4: return "Tokuma Shoten";
        case 0xC5: return "Data East";
        case 0xC6: return "Tonkin House";
        case 0xC8: return "Koei";
        case 0xC9: return "UFL";
        case 0xCA: return "Ultra";
        case 0xCB: return "Vap";
        case 0xCC: return "Use";
        case 0xCD: return "Meldac";
        case 0xCE: return "Pony Canyon";
        case 0xCF: return "Angel";
        case 0xD0: return "Taito";
        case 0xD1: return "Sofel";
        case 0xD2: return "Quest";
        case 0xD3: return "Sigma Enterprises";
        case 0xD4: return "Ask Kodansha";
        case 0xD6: return "Naxat Soft";
        case 0xD7: return "Copya Systems";
        case 0xD9: return "Banpresto";
        case 0xDA: return "Tomy";
        case 0xDB: return "LJN";
        case 0xDD: return "NCS";
        case 0xDE: return "Human";
        case 0xDF: return "Altron";
        case 0xE0: return "Jaleco";
        case 0xE1: return "Towachiki";
        case 0xE2: return "Uutaka";
        case 0xE3: return "Varie";
        case 0xE5: return "Epoch";
        case 0xE7: return "Athena";
        case 0xE8: return "Asmik";
        case 0xE9: return "Natsume";
        case 0xEA: return "King Records";
        case 0xEB: return "Atlus";
        case 0xEC: return "Epic/Sony Records";
        case 0xEE: return "IGS";
        case 0xF0: return "A Wave";
        case 0xF3: return "Extreme Entertainment";
        case 0xFF: return "LJN";
        default: return "Unknown";
    }
}

Cartridge::Cartridge()
    : ram_enabled(false)
    , mbc_type(0)
    , rom_bank(1)
    , ram_bank(0)
    , ram_bank_mode(false)
    , mbc1_multicart(false)
    , cartridge_type(0)
    , rom_size_code(0)
    , ram_size_code(0)
    , has_battery(false)
    , has_timer(false)
    , rom_loaded(false)
{
    rtc = {};
}

Cartridge::~Cartridge() = default;

bool Cartridge::LoadROM(const std::string& path) {
    // === Modern file loading: supports various methods ===
    
    // 1. Check if file exists
    if (!std::filesystem::exists(path)) {
        std::cerr << "Error: ROM file not found: " << path << "\n";
        return false;
    }
    
    // 2. Check file extension
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext != ".gb" && ext != ".gbc" && ext != ".rom" && ext != ".bin") {
        std::cerr << "Warning: Unusual file extension: " << ext << "\n";
    }
    
    // 3. Get file size
    auto file_size = std::filesystem::file_size(path);
    
    // Validate minimum size (at least header must be present)
    if (file_size < 0x150) {
        std::cerr << "Error: ROM too small (< 336 bytes)\n";
        return false;
    }
    
    // Validate maximum size (8MB for MBC5)
    if (file_size > 8 * 1024 * 1024) {
        std::cerr << "Error: ROM too large (> 8MB)\n";
        return false;
    }
    
    // 4. Open and read file
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Failed to open ROM file\n";
        return false;
    }
    
    // 5. Read entire file into ROM vector
    rom.resize(file_size);
    file.read(reinterpret_cast<char*>(rom.data()), file_size);
    
    if (!file) {
        std::cerr << "Error: Failed to read ROM data\n";
        rom.clear();
        return false;
    }
    
    file.close();
    
    // 6. Parse header
    ParseHeader();
    
    // 7. Validate ROM size matches header
    size_t expected_size = GetROMSize(rom_size_code);
    if (rom.size() < expected_size) {
        std::cerr << "Warning: ROM smaller than header indicates ("
                  << rom.size() << " < " << expected_size << ")\n";
        // Pad with 0xFF
        rom.resize(expected_size, 0xFF);
    } else if (rom.size() > expected_size) {
        std::cerr << "Warning: ROM larger than header indicates ("
                  << rom.size() << " > " << expected_size << ")\n";
    }
    
    // 8. Set MBC type FIRST (needed for correct RAM size detection)
    switch (cartridge_type) {
        case 0x00: mbc_type = 0; break;  // ROM ONLY
        case 0x01: case 0x02: case 0x03: mbc_type = 1; break;  // MBC1
        case 0x05: case 0x06: mbc_type = 2; break;  // MBC2
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: mbc_type = 3; break;  // MBC3
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: mbc_type = 5; break;  // MBC5
        default: mbc_type = 0; break;  // Treat unknown as ROM ONLY
    }
    
    // 9. Initialize RAM (now mbc_type is set, so MBC2 512-byte RAM works correctly)
    size_t ram_size = GetRAMSize();
    if (ram_size > 0) {
        ram.resize(ram_size, 0x00);
    }
    
    // 10. Initialize bank registers
    rom_bank = 1;
    ram_bank = 0;
    ram_enabled = false;
    ram_bank_mode = false;
    mbc1_multicart = false;
    
    // 11. Detect MBC1M (multicart) by checking for Nintendo logo at bank $10
    // MBC1M carts are 1MB MBC1 carts with alternate wiring where BANK2 is connected
    // to bits 4-5 instead of 5-6. They can be identified by having a valid Nintendo
    // logo at offset $40104 (bank $10's header area).
    // See: https://gbdev.io/pandocs/MBC1.html#mbc1m-1-mib-multi-game-compilation-carts
    if (mbc_type == 1 && rom.size() >= 0x41000) {  // At least 1MB+ ROM
        // Check for Nintendo logo at bank $10 (offset 0x40000 + 0x104 = 0x40104)
        const uint32_t logo_offset = 0x40104;
        if (rom.size() > logo_offset + sizeof(NINTENDO_LOGO)) {
            if (memcmp(&rom[logo_offset], NINTENDO_LOGO, sizeof(NINTENDO_LOGO)) == 0) {
                mbc1_multicart = true;
            }
        }
    }
    
    rom_loaded = true;
    return true;
}

void Cartridge::ParseHeader() {
    // Title: $0134-$0143 (16 bytes, may include manufacturer code in CGB)
    title.clear();
    for (int i = 0x134; i <= 0x143 && rom[i] != 0; ++i) {
        char c = static_cast<char>(rom[i]);
        if (c >= 32 && c < 127) {
            title += c;
        }
    }
    
    // CGB flag at $0143
    // 0x80 = CGB compatible, 0xC0 = CGB only
    bool is_cgb = (rom[0x143] == 0x80 || rom[0x143] == 0xC0);
    if (is_cgb && title.length() > 11) {
        title = title.substr(0, 11);  // Title is shorter in CGB ROMs
    }
    
    // Cartridge type: $0147
    cartridge_type = rom[0x147];
    
    // Check for battery
    has_battery = (cartridge_type == 0x03 || cartridge_type == 0x06 ||
                   cartridge_type == 0x09 || cartridge_type == 0x0D ||
                   cartridge_type == 0x0F || cartridge_type == 0x10 ||
                   cartridge_type == 0x13 || cartridge_type == 0x1B ||
                   cartridge_type == 0x1E || cartridge_type == 0xFF);
    
    // Check for timer (RTC)
    has_timer = (cartridge_type == 0x0F || cartridge_type == 0x10);
    
    // ROM size: $0148
    rom_size_code = rom[0x148];
    
    // RAM size: $0149
    ram_size_code = rom[0x149];
}

size_t Cartridge::GetRAMSize() const {
    // MBC2 has 512×4 bits of RAM regardless of header
    if (mbc_type == 2) {
        return 512;  // 512 bytes (4-bit values)
    }
    return GetRAMSizeFromCode(ram_size_code);
}

// === Cartridge Header Info for Display ===

std::string Cartridge::GetDetailedInfo() const {
    if (!rom_loaded) {
        return "No ROM loaded";
    }
    
    std::stringstream ss;
    
    ss << "╔══════════════════════════════════════════════════════════╗\n";
    ss << "║                    CARTRIDGE INFO                        ║\n";
    ss << "╠══════════════════════════════════════════════════════════╣\n";
    
    // Title
    ss << "║ Title:         " << std::left << std::setw(42) << title << "║\n";
    
    // Cartridge type
    ss << "║ Type:          " << std::setw(42) << GetCartridgeTypeName(cartridge_type) << "║\n";
    
    // MBC
    std::string mbc_str = "MBC" + std::to_string(mbc_type);
    if (mbc_type == 0) mbc_str = "No MBC";
    ss << "║ MBC:           " << std::setw(42) << mbc_str << "║\n";
    
    // ROM size
    size_t rom_size = GetROMSize(rom_size_code);
    uint16_t banks = GetROMBankCount(rom_size_code);
    std::stringstream rom_ss;
    if (rom_size >= 1024 * 1024) {
        rom_ss << (rom_size / 1024 / 1024) << " MB (" << banks << " banks)";
    } else {
        rom_ss << (rom_size / 1024) << " KB (" << banks << " banks)";
    }
    ss << "║ ROM Size:      " << std::setw(42) << rom_ss.str() << "║\n";
    
    // RAM size
    size_t ram_size = GetRAMSize();
    std::stringstream ram_ss;
    if (ram_size == 0) {
        ram_ss << "None";
    } else if (ram_size >= 1024) {
        ram_ss << (ram_size / 1024) << " KB";
        if (has_battery) ram_ss << " (Battery)";
    } else {
        ram_ss << ram_size << " bytes";
        if (has_battery) ram_ss << " (Battery)";
    }
    ss << "║ RAM Size:      " << std::setw(42) << ram_ss.str() << "║\n";
    
    // Features
    std::string features;
    if (has_battery) features += "Battery ";
    if (has_timer) features += "RTC ";
    if (features.empty()) features = "None";
    ss << "║ Features:      " << std::setw(42) << features << "║\n";
    
    // CGB flag
    std::string cgb_str;
    uint8_t cgb_flag = rom[0x143];
    if (cgb_flag == 0x80) cgb_str = "CGB Enhanced";
    else if (cgb_flag == 0xC0) cgb_str = "CGB Only";
    else cgb_str = "DMG Only";
    ss << "║ Platform:      " << std::setw(42) << cgb_str << "║\n";
    
    // SGB flag
    std::string sgb_str = (rom[0x146] == 0x03) ? "Yes" : "No";
    ss << "║ SGB Support:   " << std::setw(42) << sgb_str << "║\n";
    
    // Destination
    ss << "║ Destination:   " << std::setw(42) << GetDestination(rom[0x14A]) << "║\n";
    
    // Licensee
    char new_licensee[3] = { static_cast<char>(rom[0x144]), static_cast<char>(rom[0x145]), 0 };
    ss << "║ Publisher:     " << std::setw(42) << GetLicensee(rom[0x14B], new_licensee) << "║\n";
    
    // Version
    ss << "║ Version:       " << std::setw(42) << ("1." + std::to_string(rom[0x14C])) << "║\n";
    
    // Checksums
    std::stringstream chk_ss;
    
    // Header checksum
    uint8_t header_checksum = 0;
    for (uint16_t addr = 0x134; addr <= 0x14C; addr++) {
        header_checksum = header_checksum - rom[addr] - 1;
    }
    bool header_valid = (header_checksum == rom[0x14D]);
    chk_ss << "Header: " << (header_valid ? "VALID" : "INVALID") << " (0x" 
           << std::hex << std::uppercase << (int)rom[0x14D] << ")";
    ss << "║ Checksum:      " << std::setw(42) << chk_ss.str() << "║\n";
    
    // Nintendo logo check  
    bool logo_valid = (memcmp(&rom[0x104], NINTENDO_LOGO, sizeof(NINTENDO_LOGO)) == 0);
    ss << "║ Nintendo Logo: " << std::setw(42) << (logo_valid ? "Valid" : "Invalid/Modified") << "║\n";
    
    ss << "╚══════════════════════════════════════════════════════════╝\n";
    
    return ss.str();
}

// === Memory Access ===

uint8_t Cartridge::Read(uint16_t addr) const {
    if (addr < 0x8000) {
        return ReadROM(addr);
    } else if (addr >= 0xA000 && addr < 0xC000) {
        return ReadRAM(addr);
    }
    return 0xFF;
}

void Cartridge::Write(uint16_t addr, uint8_t value) {
    if (addr < 0x8000) {
        WriteROM(addr, value);
    } else if (addr >= 0xA000 && addr < 0xC000) {
        WriteRAM(addr, value);
    }
}

uint8_t Cartridge::ReadROM(uint16_t addr) const {
    uint32_t offset = GetROMOffset(addr);
    if (offset < rom.size()) {
        return rom[offset];
    }
    return 0xFF;
}

void Cartridge::WriteROM(uint16_t addr, uint8_t value) {
    // ROM writes are MBC register writes
    switch (mbc_type) {
        case 0:  // No MBC - writes ignored
            break;
            
        case 1:  // MBC1
            // See: https://gbdev.io/pandocs/MBC1.html
            if (addr < 0x2000) {
                // $0000-$1FFF: RAM Enable
                // Any value with $A in lower 4 bits enables RAM
                ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                // $2000-$3FFF: ROM Bank Number (BANK1 register)
                // 5-bit register, but 00→01 translation is handled during address calculation
                // We store the raw 5-bit value here
                rom_bank = value & 0x1F;
            } else if (addr < 0x6000) {
                // $4000-$5FFF: RAM Bank Number / Upper ROM Bank bits (BANK2 register)
                // This 2-bit register is ALWAYS written to - mode only affects how it's USED
                // during address translation, not how it's stored
                ram_bank = value & 0x03;
            } else {
                // $6000-$7FFF: Banking Mode Select
                // 0 = Simple mode (default): 0000-3FFF locked to bank 0, A000-BFFF locked to RAM bank 0
                // 1 = Advanced mode: 0000-3FFF and A000-BFFF can be switched via BANK2
                ram_bank_mode = (value & 0x01) != 0;
            }
            break;
            
        case 2:  // MBC2
            if (addr < 0x4000) {
                if (addr & 0x0100) {
                    // ROM Bank (bit 8 of address set)
                    rom_bank = value & 0x0F;
                    if (rom_bank == 0) rom_bank = 1;
                } else {
                    // RAM Enable (bit 8 of address clear)
                    ram_enabled = ((value & 0x0F) == 0x0A);
                }
            }
            break;
            
        case 3:  // MBC3
            if (addr < 0x2000) {
                // RAM/RTC Enable
                ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                // ROM Bank
                rom_bank = value & 0x7F;
                if (rom_bank == 0) rom_bank = 1;
            } else if (addr < 0x6000) {
                // RAM Bank / RTC Register Select
                ram_bank = value;
            } else {
                // Latch Clock Data
                if (rtc.latch_register == 0 && value == 1) {
                    rtc.latched = !rtc.latched;
                }
                rtc.latch_register = value;
            }
            break;
            
        case 5:  // MBC5
            if (addr < 0x2000) {
                // RAM Enable
                ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x3000) {
                // ROM Bank Low 8 bits
                rom_bank = (rom_bank & 0x100) | value;
            } else if (addr < 0x4000) {
                // ROM Bank High bit
                rom_bank = (rom_bank & 0xFF) | ((value & 0x01) << 8);
            } else if (addr < 0x6000) {
                // RAM Bank
                ram_bank = value & 0x0F;
            }
            break;
    }
}

uint8_t Cartridge::ReadRAM(uint16_t addr) const {
    if (!ram_enabled || ram.empty()) {
        return 0xFF;
    }
    
    // MBC3 RTC registers
    if (mbc_type == 3 && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        switch (ram_bank) {
            case 0x08: return rtc.seconds;
            case 0x09: return rtc.minutes;
            case 0x0A: return rtc.hours;
            case 0x0B: return rtc.days_low;
            case 0x0C: return rtc.days_high;
        }
        return 0xFF;
    }
    
    uint32_t offset = GetRAMOffset(addr);
    if (offset < ram.size()) {
        if (mbc_type == 2) {
            // MBC2: 4-bit RAM
            return ram[offset] | 0xF0;
        }
        return ram[offset];
    }
    return 0xFF;
}

void Cartridge::WriteRAM(uint16_t addr, uint8_t value) {
    if (!ram_enabled || ram.empty()) {
        return;
    }
    
    // MBC3 RTC registers
    if (mbc_type == 3 && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        switch (ram_bank) {
            case 0x08: rtc.seconds = value; break;
            case 0x09: rtc.minutes = value; break;
            case 0x0A: rtc.hours = value; break;
            case 0x0B: rtc.days_low = value; break;
            case 0x0C: rtc.days_high = value; break;
        }
        return;
    }
    
    uint32_t offset = GetRAMOffset(addr);
    if (offset < ram.size()) {
        if (mbc_type == 2) {
            // MBC2: 4-bit RAM
            ram[offset] = value & 0x0F;
        } else {
            ram[offset] = value;
        }
    }
}

uint32_t Cartridge::GetROMOffset(uint16_t addr) const {
    // Calculate the bank mask for hardware-accurate AND gate emulation
    // Real MBC hardware uses AND gates to mask bank numbers, not modulo
    // For power-of-2 bank counts, mask = num_banks - 1
    uint16_t num_banks = GetROMBankCount(rom_size_code);
    uint16_t bank_mask = num_banks - 1;  // Hardware AND gate mask
    
    if (addr < 0x4000) {
        // $0000-$3FFF: ROM Bank X0
        if (mbc_type == 1 && ram_bank_mode) {
            // Mode 1: BANK2 applies to this region too
            uint32_t bank;
            if (mbc1_multicart) {
                // MBC1M: BANK2 applies to bits 4-5
                bank = (ram_bank << 4);
            } else {
                // Normal MBC1: BANK2 applies to bits 5-6
                bank = (ram_bank << 5);
            }
            // Hardware AND gate masking
            bank = bank & bank_mask;
            return (bank * 0x4000) + addr;
        }
        // Mode 0: Always bank 0
        return addr;
    } else {
        // $4000-$7FFF: Switchable ROM bank
        uint32_t bank = rom_bank;  // 5-bit BANK1 register
        
        if (mbc_type == 1) {
            // MBC1 specific handling
            if (mbc1_multicart) {
                // MBC1M (multicart) mode:
                // - Only lower 4 bits of BANK1 used for banking
                // - But full 5 bits still used for 00→01 translation
                // - BANK2 applies to bits 4-5 (not 5-6)
                if (bank == 0) {
                    bank = 1;
                }
                // Use only lower 4 bits for actual banking
                uint32_t bank_low = bank & 0x0F;
                // Apply BANK2 to bits 4-5
                bank = bank_low | (ram_bank << 4);
            } else {
                // Normal MBC1:
                // Combine BANK1 (5-bit) with BANK2 (2-bit) for upper bits
                // The 00→01 translation only looks at BANK1 (the 5-bit register)
                if (bank == 0) {
                    bank = 1;
                }
                // Always apply BANK2 as upper bits (bits 5-6) for $4000-$7FFF
                bank = bank | (ram_bank << 5);
            }
            // Hardware AND gate masking
            bank = bank & bank_mask;
        } else if (mbc_type == 5) {
            // MBC5: 9-bit bank number, bank 0 IS accessible
            // Hardware AND gate masking
            bank = bank & bank_mask;
        } else if (mbc_type == 3) {
            // MBC3: 7-bit bank number, 00→01 translation
            if (bank == 0) bank = 1;
            // Hardware AND gate masking
            bank = bank & bank_mask;
        } else if (mbc_type == 2) {
            // MBC2: 4-bit bank number, 00→01 translation
            if (bank == 0) bank = 1;
            // Hardware AND gate masking
            bank = bank & bank_mask;
        } else if (num_banks > 1) {
            // No MBC but multi-bank: mask to size
            bank = bank & bank_mask;
        }
        
        return (bank * 0x4000) + (addr - 0x4000);
    }
}

uint32_t Cartridge::GetRAMOffset(uint16_t addr) const {
    if (mbc_type == 2) {
        // MBC2: 512 bytes, mirrored via hardware AND gate
        return (addr - 0xA000) & 0x1FF;
    }
    
    // Calculate RAM bank mask for hardware-accurate AND gate emulation
    size_t ram_size = ram.size();
    uint32_t num_ram_banks = (ram_size > 0) ? ((ram_size + 0x1FFF) / 0x2000) : 0;
    uint32_t ram_bank_mask = (num_ram_banks > 0) ? (num_ram_banks - 1) : 0;
    
    // Apply RAM bank using hardware AND gate masking
    uint32_t bank = 0;
    if (mbc_type == 1 && ram_bank_mode && num_ram_banks > 1) {
        // MBC1 mode 1: BANK2 applies to RAM
        // Hardware AND gate masking
        bank = ram_bank & ram_bank_mask;
    } else if ((mbc_type == 3 || mbc_type == 5) && num_ram_banks > 1) {
        // Hardware AND gate masking
        bank = ram_bank & ram_bank_mask;
    }
    
    return (bank * 0x2000) + (addr - 0xA000);
}

// === Save/Load ===

bool Cartridge::LoadSave(const std::string& path) {
    if (!has_battery || ram.empty()) {
        return false;
    }
    
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(ram.data()), ram.size());
    return file.good();
}

bool Cartridge::SaveRAM(const std::string& path) const {
    if (!has_battery || ram.empty()) {
        return false;
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(ram.data()), ram.size());
    return file.good();
}

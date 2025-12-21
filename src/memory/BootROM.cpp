#include "BootROM.hpp"
#include <fstream>

BootROM::BootROM()
    : enabled(false)
    , loaded(false)
{
    rom.fill(0);
}

bool BootROM::Load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(rom.data()), 256);
    
    if (file.gcount() != 256) {
        return false;
    }
    
    loaded = true;
    enabled = true;
    return true;
}

uint8_t BootROM::Read(uint16_t addr) const {
    if (addr < 256) {
        return rom[addr];
    }
    return 0xFF;
}

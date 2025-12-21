#include "PPU.hpp"

PPU::PPU() {
    Reset();
}

void PPU::Reset() {
    mode = OAM_SCAN;
    dot_counter = 0;
    ly = 0;
    window_line = 0;
    window_active = false;
    window_triggered = false;
    
    // Registers
    lcdc = 0x91;
    stat = 0;
    scy = 0;
    scx = 0;
    lyc = 0;
    bgp = 0xFC;
    obp0 = 0xFF;
    obp1 = 0xFF;
    wy = 0;
    wx = 0;
    
    // Memory
    vram.fill(0);
    oam.fill(0);
    framebuffer.fill(0);
    
    // Interrupts
    vblank_irq = false;
    stat_irq = false;
    frame_complete = false;
    stat_line = false;
    
    // FIFOs
    ClearFIFOs();
    
    // Fetcher
    fetcher_step = FetcherStep::GET_TILE;
    fetcher_dots = 0;
    fetcher_x = 0;
    fetcher_tile_no = 0;
    fetcher_tile_low = 0;
    fetcher_tile_high = 0;
    fetcher_window = false;
    
    // Pixel output
    lcd_x = 0;
    discard_pixels = 0;
    
    // Sprites
    sprite_count = 0;
    sprite_index = 0;
    fetching_sprite = false;
}

void PPU::Step(uint8_t cycles) {
    if (!IsLCDEnabled()) {
        return;
    }
    
    for (int i = 0; i < cycles; i++) {
        // Per Pan Docs: process at current dot, THEN increment
        switch (mode) {
            case OAM_SCAN:
                StepOAMScan();
                break;
            case PIXEL_TRANSFER:
                StepPixelTransfer();
                break;
            case HBLANK:
                StepHBlank();
                break;
            case VBLANK:
                StepVBlank();
                break;
        }
        
        dot_counter++;
    }
}

// === Mode 2: OAM Scan (80 dots: 0-79) ===
// Per Pan Docs: scans OAM for sprites on current line
void PPU::StepOAMScan() {
    // Check 1 OAM entry every 2 dots
    if ((dot_counter & 1) == 0 && dot_counter < 80) {
        uint8_t entry = dot_counter / 2;
        if (entry < 40 && sprite_count < 10) {
            uint8_t y = oam[entry * 4];
            uint8_t x = oam[entry * 4 + 1];
            uint8_t height = IsTallSprites() ? 16 : 8;
            
            // Sprite Y is screen Y + 16
            if (ly + 16 >= y && ly + 16 < y + height) {
                scanline_sprites[sprite_count++] = {
                    y, x,
                    oam[entry * 4 + 2],
                    oam[entry * 4 + 3],
                    entry
                };
            }
        }
    }
    
    // Transition at dot 80
    if (dot_counter == 79) {
        mode = PIXEL_TRANSFER;
        // Note: mode bits are stored in 'mode' variable, not in 'stat'
        
        InitFetcher();
        CheckStatInterrupt();
    }
}

// === Mode 3: Pixel Transfer ===
// Per Pan Docs: Fetcher runs at 2 dots per step, FIFO pops at 1 pixel per dot
void PPU::StepPixelTransfer() {
    // Check for sprite at current X (before popping)
    if (!fetching_sprite && IsSpritesEnabled()) {
        for (int i = 0; i < sprite_count; i++) {
            if (scanline_sprites[i].x != 0 && 
                lcd_x + 8 >= scanline_sprites[i].x && 
                lcd_x + 8 < scanline_sprites[i].x + 8) {
                // Start sprite fetch
                sprite_index = i;
                fetching_sprite = true;
                fetcher_dots = 0;
                break;
            }
        }
    }
    
    // Sprite fetch pauses BG fetcher
    if (fetching_sprite) {
        fetcher_dots++;
        if (fetcher_dots >= 6) {  // 6 dots for sprite fetch per Pan Docs
            FetchSprite();
            scanline_sprites[sprite_index].x = 0;  // Mark processed
            fetching_sprite = false;
        }
    } else {
        // Run BG fetcher
        AdvanceFetcher();
    }
    
    // Pop pixel from FIFO if we have enough (need 8+ for mixing)
    if (!fetching_sprite && bg_fifo_size >= 8) {
        // Discard SCX % 8 pixels at start of line
        if (discard_pixels > 0) {
            PopBGPixel();
            if (sprite_fifo_size > 0) PopSpritePixel();
            discard_pixels--;
        } else {
            RenderPixel();
            lcd_x++;
            
            if (lcd_x >= 160) {
                mode = HBLANK;
                // Note: mode bits are stored in 'mode' variable, not in 'stat'
                CheckStatInterrupt();
            }
        }
    }
}

// === Mode 0: HBlank ===
void PPU::StepHBlank() {
    if (dot_counter == 455) {
        if (window_active) window_line++;
        
        ly++;
        stat = (stat & ~0x04) | ((ly == lyc) ? 0x04 : 0);
        
        if (ly == 144) {
            mode = VBLANK;
            // Note: mode bits stored in 'mode' variable, not in 'stat'
            vblank_irq = true;
            frame_complete = true;
        } else {
            mode = OAM_SCAN;
            // Note: mode bits stored in 'mode' variable, not in 'stat'
            sprite_count = 0;
            window_active = false;
        }
        
        dot_counter = (uint16_t)-1;
        CheckStatInterrupt();
    }
}

// === Mode 1: VBlank ===
void PPU::StepVBlank() {
    if (dot_counter == 455) {
        ly++;
        stat = (stat & ~0x04) | ((ly == lyc) ? 0x04 : 0);
        
        if (ly > 153) {
            ly = 0;
            mode = OAM_SCAN;
            // Note: mode bits stored in 'mode' variable, not in 'stat'
            window_line = 0;
            window_triggered = false;
            sprite_count = 0;
            window_active = false;
        }
        
        dot_counter = (uint16_t)-1;
        CheckStatInterrupt();
    }
}

// === Fetcher ===
void PPU::InitFetcher() {
    ClearFIFOs();
    fetcher_step = FetcherStep::GET_TILE;
    fetcher_dots = 0;
    fetcher_x = 0;
    fetcher_window = false;
    lcd_x = 0;
    discard_pixels = scx & 7;
    sprite_index = 0;
    fetching_sprite = false;
}

// Per Pan Docs: Each step takes 2 dots, except PUSH which attempts every dot
void PPU::AdvanceFetcher() {
    fetcher_dots++;
    
    switch (fetcher_step) {
        case FetcherStep::GET_TILE:
            if (fetcher_dots >= 2) {
                fetcher_tile_no = FetchTileNumber();
                fetcher_step = FetcherStep::GET_TILE_DATA_LOW;
                fetcher_dots = 0;
            }
            break;
            
        case FetcherStep::GET_TILE_DATA_LOW:
            if (fetcher_dots >= 2) {
                fetcher_tile_low = FetchTileDataLow();
                fetcher_step = FetcherStep::GET_TILE_DATA_HIGH;
                fetcher_dots = 0;
            }
            break;
            
        case FetcherStep::GET_TILE_DATA_HIGH:
            if (fetcher_dots >= 2) {
                fetcher_tile_high = FetchTileDataHigh();
                fetcher_step = FetcherStep::SLEEP;
                fetcher_dots = 0;
            }
            break;
            
        case FetcherStep::SLEEP:
            if (fetcher_dots >= 2) {
                fetcher_step = FetcherStep::PUSH;
                fetcher_dots = 0;
            }
            break;
            
        case FetcherStep::PUSH:
            // Push when FIFO has room for 8 more pixels (per Pan Docs)
            if (bg_fifo_size <= 8) {
                PushRowToFIFO();
                fetcher_x++;
                fetcher_step = FetcherStep::GET_TILE;
                fetcher_dots = 0;
            }
            // Else stall (attempted every dot)
            break;
    }
}

uint8_t PPU::FetchTileNumber() {
    uint16_t map = fetcher_window ? GetWindowTileMapBase() : GetBGTileMapBase();
    uint8_t tx = fetcher_window ? fetcher_x : ((scx / 8 + fetcher_x) & 0x1F);
    uint8_t ty = fetcher_window ? (window_line / 8) : (((ly + scy) & 0xFF) / 8);
    return vram[(map - 0x8000) + ty * 32 + tx];
}

uint8_t PPU::FetchTileDataLow() {
    uint8_t line = fetcher_window ? (window_line & 7) : ((ly + scy) & 7);
    uint16_t addr = UsesSignedTileIndex() 
        ? (0x9000 + static_cast<int8_t>(fetcher_tile_no) * 16)
        : (0x8000 + fetcher_tile_no * 16);
    return vram[(addr - 0x8000) + line * 2];
}

uint8_t PPU::FetchTileDataHigh() {
    uint8_t line = fetcher_window ? (window_line & 7) : ((ly + scy) & 7);
    uint16_t addr = UsesSignedTileIndex()
        ? (0x9000 + static_cast<int8_t>(fetcher_tile_no) * 16)
        : (0x8000 + fetcher_tile_no * 16);
    return vram[(addr - 0x8000) + line * 2 + 1];
}

void PPU::PushRowToFIFO() {
    for (int bit = 7; bit >= 0; bit--) {
        uint8_t color = ((fetcher_tile_high >> bit) & 1) << 1 | ((fetcher_tile_low >> bit) & 1);
        PushBGPixel(color);
    }
}

void PPU::FetchSprite() {
    SpriteEntry& spr = scanline_sprites[sprite_index];
    uint8_t height = IsTallSprites() ? 16 : 8;
    uint8_t line = (ly + 16) - spr.y;
    
    if (spr.flags & 0x40) line = (height - 1) - line;  // Y-flip
    
    uint8_t tile = spr.tile;
    if (IsTallSprites()) {
        tile &= 0xFE;
        if (line >= 8) { tile |= 1; line -= 8; }
    }
    
    uint8_t lo = vram[tile * 16 + line * 2];
    uint8_t hi = vram[tile * 16 + line * 2 + 1];
    
    // Mix into sprite FIFO
    for (int bit = 7; bit >= 0; bit--) {
        int actual_bit = (spr.flags & 0x20) ? (7 - bit) : bit;  // X-flip
        uint8_t color = ((hi >> actual_bit) & 1) << 1 | ((lo >> actual_bit) & 1);
        
        if (color != 0) {
            uint8_t pos = 7 - bit;
            if (pos < sprite_fifo_size) {
                // Merge - keep existing if non-transparent (X priority)
                if (sprite_fifo[(sprite_fifo_head + pos) & 0xF].color == 0) {
                    sprite_fifo[(sprite_fifo_head + pos) & 0xF] = {
                        color,
                        (uint8_t)((spr.flags >> 4) & 1),
                        (uint8_t)((spr.flags >> 7) & 1),
                        spr.oam_index
                    };
                }
            } else {
                PushSpritePixel(color, (spr.flags >> 4) & 1, (spr.flags >> 7) & 1, spr.oam_index);
            }
        }
    }
}

// === FIFO Operations ===
void PPU::ClearFIFOs() {
    bg_fifo_head = bg_fifo_size = 0;
    sprite_fifo_head = sprite_fifo_size = 0;
}

void PPU::PushBGPixel(uint8_t color) {
    if (bg_fifo_size < 16) {
        bg_fifo[(bg_fifo_head + bg_fifo_size) & 0xF] = {color, 0, 0, 0};
        bg_fifo_size++;
    }
}

PPU::FIFOPixel PPU::PopBGPixel() {
    FIFOPixel p = bg_fifo[bg_fifo_head];
    bg_fifo_head = (bg_fifo_head + 1) & 0xF;
    bg_fifo_size--;
    return p;
}

void PPU::PushSpritePixel(uint8_t color, uint8_t pal, bool bgpri, uint8_t idx) {
    if (sprite_fifo_size < 16) {
        sprite_fifo[(sprite_fifo_head + sprite_fifo_size) & 0xF] = {color, pal, bgpri ? (uint8_t)1 : (uint8_t)0, idx};
        sprite_fifo_size++;
    }
}

PPU::FIFOPixel PPU::PopSpritePixel() {
    FIFOPixel p = sprite_fifo[sprite_fifo_head];
    sprite_fifo_head = (sprite_fifo_head + 1) & 0xF;
    sprite_fifo_size--;
    return p;
}

void PPU::RenderPixel() {
    FIFOPixel bg = PopBGPixel();
    FIFOPixel obj = {0, 0, 0, 0};
    if (sprite_fifo_size > 0) obj = PopSpritePixel();
    
    // Window trigger check
    if (!fetcher_window && IsWindowEnabled() && ly >= wy && lcd_x + 7 >= wx) {
        fetcher_window = true;
        window_active = true;
        ClearFIFOs();
        fetcher_step = FetcherStep::GET_TILE;
        fetcher_dots = 0;
        fetcher_x = 0;
        return;
    }
    
    uint8_t color;
    if (obj.color != 0 && IsSpritesEnabled() && (!obj.bg_priority || bg.color == 0)) {
        color = ((obj.palette ? obp1 : obp0) >> (obj.color * 2)) & 3;
    } else {
        color = IsBGEnabled() ? ((bgp >> (bg.color * 2)) & 3) : 0;
    }
    
    if (lcd_x < 160 && ly < 144) {
        framebuffer[ly * 160 + lcd_x] = color;
    }
}

// === STAT Interrupt ===
void PPU::CheckStatInterrupt() {
    // Per SameBoy GB_STAT_update line 525:
    // if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) return;
    // When LCD is off, the comparison clock is frozen
    if (!IsLCDEnabled()) return;
    
    // Per SameBoy GB_STAT_update lines 532-542:
    // Update LY=LYC bit in STAT register
    bool lyc_match = (ly == lyc);
    if (lyc_match) {
        stat |= 0x04;   // Set LY=LYC bit
    } else {
        stat &= ~0x04;  // Clear LY=LYC bit
    }
    
    // Per SameBoy GB_STAT_update lines 545-555:
    // Mode-based interrupts only for modes 0, 1, 2 (not 3)
    bool line = ((stat & 0x40) && lyc_match) ||             // LYC=LY interrupt
                ((stat & 0x20) && mode == OAM_SCAN) ||      // Mode 2 interrupt  
                ((stat & 0x10) && mode == VBLANK) ||        // Mode 1 interrupt
                ((stat & 0x08) && mode == HBLANK);          // Mode 0 interrupt
    
    // Rising edge detection - only trigger on LOW→HIGH transition
    if (line && !stat_line) stat_irq = true;
    stat_line = line;
}

// === Register Access ===
uint8_t PPU::ReadRegister(uint16_t addr) const {
    switch (addr) {
        case 0xFF40: return lcdc;
        case 0xFF41:
            // Per SameBoy: STAT is stored with LY=LYC bit, not computed dynamically
            // This allows the bit to be "frozen" when LCD is off
            return stat | mode | 0x80;
        case 0xFF42: return scy;
        case 0xFF43: return scx;
        case 0xFF44: return ly;
        case 0xFF45: return lyc;
        case 0xFF47: return bgp;
        case 0xFF48: return obp0;
        case 0xFF49: return obp1;
        case 0xFF4A: return wy;
        case 0xFF4B: return wx;
        default: return 0xFF;
    }
}

void PPU::WriteRegister(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0xFF40: {
            bool was_enabled = IsLCDEnabled();
            if (!(value & 0x80) && was_enabled) {
                // LCD turning OFF per SameBoy GB_lcd_off:
                // - LY = 0, mode = 0 (HBlank)
                // - LY=LYC bit (bit 2 of stat) is preserved
                // - Mode bits stored separately in 'mode' variable
                ly = 0;
                dot_counter = 0;
                mode = HBLANK;  // Per SameBoy line 575: STAT mode = 0
                // Note: Don't touch stat here - preserve bit 2 (LY=LYC)
            }
            lcdc = value;
            if ((value & 0x80) && !was_enabled) {
                // LCD turning ON - per SameBoy, the comparison clock starts
                // and LY=LYC comparison is immediately updated
                CheckStatInterrupt();
            }
            break;
        }
        case 0xFF41:
            // Per SameBoy: preserve bit 2 (LY=LYC) and set bits 3-6 from value
            // Bits 0-1 (mode) are stored separately in 'mode' variable
            stat = (stat & 0x04) | (value & 0x78);
            CheckStatInterrupt();  // Per SameBoy: re-evaluate IRQ line after enable changes
            break;
        case 0xFF42: scy = value; break;
        case 0xFF43: scx = value; break;
        case 0xFF45:
            lyc = value;
            CheckStatInterrupt();  // Per SameBoy: re-evaluate IRQ line after LYC change
            break;
        case 0xFF47: bgp = value; break;
        case 0xFF48: obp0 = value; break;
        case 0xFF49: obp1 = value; break;
        case 0xFF4A: wy = value; break;
        case 0xFF4B: wx = value; break;
    }
}

// === VRAM/OAM Access ===
uint8_t PPU::ReadVRAM(uint16_t addr) const {
    // Per Pan Docs: VRAM accessible except during Mode 3, or when LCD is OFF
    if (!IsLCDEnabled() || mode != PIXEL_TRANSFER) {
        return vram[addr - 0x8000];
    }
    return 0xFF;
}

void PPU::WriteVRAM(uint16_t addr, uint8_t value) {
    // Per Pan Docs: VRAM accessible except during Mode 3, or when LCD is OFF
    if (!IsLCDEnabled() || mode != PIXEL_TRANSFER) {
        vram[addr - 0x8000] = value;
    }
}

uint8_t PPU::ReadOAM(uint16_t addr) const {
    // Per Pan Docs: OAM accessible during HBlank, VBlank, or when LCD is OFF
    if (!IsLCDEnabled() || mode == HBLANK || mode == VBLANK) {
        return oam[addr - 0xFE00];
    }
    return 0xFF;
}

void PPU::WriteOAM(uint16_t addr, uint8_t value) {
    // Per Pan Docs: OAM accessible during HBlank, VBlank, or when LCD is OFF
    if (!IsLCDEnabled() || mode == HBLANK || mode == VBLANK) {
        oam[addr - 0xFE00] = value;
    }
}

void PPU::DMAWriteOAM(uint8_t index, uint8_t value) {
    if (index < 160) oam[index] = value;
}

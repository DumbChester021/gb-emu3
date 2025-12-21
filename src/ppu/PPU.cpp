#include "PPU.hpp"

PPU::PPU() {
    Reset();
}

void PPU::Reset(bool bootRomEnabled) {
    mode = bootRomEnabled ? HBLANK : OAM_SCAN;  // LCD off starts in mode 0
    dot_counter = 0;
    ly = 0;
    window_line = -1;
    window_active = false;
    window_triggered = false;
    
    // Registers - per SameBoy: boot ROM starts with LCD off (LCDC=0)
    // Without boot ROM, start with post-boot state (LCDC=$91)
    lcdc = bootRomEnabled ? 0x00 : 0x91;
    stat = 0;
    scy = 0;
    scx = 0;
    lyc = 0;
    bgp = bootRomEnabled ? 0x00 : 0xFC;
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
    mode_for_interrupt = 2;  // Start in Mode 2
    
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
    // Per SameBoy display.c lines 1778-1792:
    // Mode 2 interrupt fires 1 T-cycle BEFORE STAT mode bits change
    // At dot 0: mode_for_interrupt=2, but STAT mode bits still 0
    // At dot 1: STAT mode bits become 2
    if (dot_counter == 0 && ly != 0) {
        // Fire Mode 2 interrupt while STAT mode bits still show 0 (HBLANK)
        // The 'mode' variable already is OAM_SCAN internally, but we'll handle
        // the STAT read to return 0 at dot 0 via mode_for_interrupt
        mode_for_interrupt = 2;
        CheckStatInterrupt();
        
        // Per SameBoy display.c line 517-519:
        // Set wy_triggered when LY == WY (this persists for rest of frame)
        if (IsWindowEnabled() && ly == wy) {
            window_triggered = true;
        }
    }
    
    // Check 1 OAM entry every 2 dots
    if ((dot_counter & 1) == 0 && dot_counter < 80) {
        uint8_t entry = dot_counter / 2;
        if (entry < 40 && sprite_count < 10) {
            uint8_t y = oam[entry * 4];
            uint8_t x = oam[entry * 4 + 1];
            uint8_t height = IsTallSprites() ? 16 : 8;
            
            // Sprite Y is screen Y + 16
            if (ly + 16 >= y && ly + 16 < y + height) {
                // Per SameBoy display.c lines 637-649:
                // Insert-sort by X coordinate. Higher X comes first, lower X comes last.
                // This ensures lower X sprites are processed later and win when overlapping.
                // For same X, later OAM entries come after earlier ones (OAM order preserved).
                uint8_t j = 0;
                for (; j < sprite_count; j++) {
                    // Find insertion point: stop when we find a sprite with X <= this sprite's X
                    // This places higher X sprites before lower X sprites
                    if (scanline_sprites[j].x <= x) break;
                }
                // Shift sprites to make room for insertion
                for (uint8_t k = sprite_count; k > j; k--) {
                    scanline_sprites[k] = scanline_sprites[k - 1];
                }
                // Insert the new sprite
                scanline_sprites[j] = {
                    y, x,
                    oam[entry * 4 + 2],
                    oam[entry * 4 + 3],
                    entry
                };
                sprite_count++;
            }
        }
    }
    
    // Mode 3 transition timing per SameBoy analysis:
    // SameBoy has 4 cycles before OAM loop (lines 1770-1789: 2+1+1)
    // My dot 0 = SameBoy cycle 4, so: SameBoy cycle N = my dot (N-4)
    //
    // SameBoy:
    //   - STAT mode = 3 at cycle 84 = my dot 80
    //   - mode_3_start at cycle 89 = my dot 85 (after +5 cycles: 84+5=89)
    //   - Mode 3 rendering = 167 cycles
    //   - Mode 0 STAT at cycle 256 = my dot 252
    //   - Mode 0 interrupt at cycle 257 = my dot 253
    
    if (dot_counter == 79) {
        // STAT mode = 3 starts at SameBoy cycle 84 = my dot 80
        // But we check at dot 79 so the change takes effect AT dot 80
        mode_for_interrupt = 3;
        CheckStatInterrupt();
    }
    
    if (dot_counter == 84) {
        // mode_3_start: SameBoy cycle 89 = my dot 85
        // Check at dot 84 so PIXEL_TRANSFER starts AT dot 85
        mode = PIXEL_TRANSFER;
        InitFetcher();
    }
}

// === Mode 3: Pixel Transfer ===
// Per Pan Docs: Fetcher runs at 2 dots per step, FIFO pops at 1 pixel per dot
void PPU::StepPixelTransfer() {
    // Check for sprite at current X (before popping)
    // Per SameBoy: process sprites from end of array (lowest X) first
    // Array is sorted with highest X first, so iterate backwards to get lowest X
    if (!fetching_sprite && IsSpritesEnabled()) {
        for (int i = sprite_count - 1; i >= 0; i--) {
            if (scanline_sprites[i].x != 0 && 
                lcd_x + 8 >= scanline_sprites[i].x && 
                lcd_x + 8 < scanline_sprites[i].x + 8) {
                // Start sprite fetch for this sprite
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
    
    // Pop pixel from FIFO if it has data
    // Per Pan Docs: LCD_X only advances if background FIFO has data
    if (!fetching_sprite && bg_fifo_size > 0) {
        // Discard SCX % 8 pixels at start of line
        if (discard_pixels > 0) {
            PopBGPixel();
            if (sprite_fifo_size > 0) PopSpritePixel();
            discard_pixels--;
        } else {
            // Only increment lcd_x if a pixel was actually rendered
            // Window trigger returns false and doesn't render - we'll render at same lcd_x once window data is ready
            if (RenderPixel()) {
                lcd_x++;
                
                if (lcd_x >= 160) {
                    mode = HBLANK;
                    mode_for_interrupt = 0;
                    CheckStatInterrupt();
                }
            }
        }
    }
}

// === Mode 0: HBlank ===
void PPU::StepHBlank() {
    if (dot_counter == 455) {
        // Note: window_line is incremented when window TRIGGERS (in RenderPixel), not here
        
        ly++;
        stat = (stat & ~0x04) | ((ly == lyc) ? 0x04 : 0);
        
        if (ly == 144) {
            // Per SameBoy display.c lines 2160-2162, 2177-2178:
            // At VBlank entry, Mode 2 interrupt (bit 5) also fires if enabled
            // This is a DMG quirk where line 144 triggers the "OAM STAT interrupt"
            if ((stat & 0x20) && !stat_line) {
                stat_irq = true;
            }
            
            mode = VBLANK;
            mode_for_interrupt = 1;  // Per SameBoy: Mode 1 interrupt check
            vblank_irq = true;
            frame_complete = true;
            CheckStatInterrupt();  // Mode 1 interrupt at VBlank entry
        } else {
            // Per SameBoy: Mode 2 STAT interrupt fires at dot 0 with STAT mode bits still 0
            // Internal mode is OAM_SCAN so we run StepOAMScan, but STAT read will
            // return special value at dot 0 (handled in ReadRegister)
            mode = OAM_SCAN;
            mode_for_interrupt = -1;  // Wait for dot 0 to set mode_for_interrupt=2
            sprite_count = 0;
            window_active = false;
        }
        
        dot_counter = (uint16_t)-1;
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
            mode_for_interrupt = 2;  // Per SameBoy: Mode 2 interrupt check
            window_line = -1;  // Per SameBoy: starts at -1, incremented when window triggers
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
    
    // Per SameBoy display.c line 1851: Pre-fill FIFO with 8 "junk" pixels
    // These will be discarded, but allow rendering to start immediately
    // This enables pixel pop/render to run in parallel with the first tile fetch
    for (int i = 0; i < 8; i++) {
        bg_fifo[bg_fifo_size++] = {0, 0, false, 0};  // Junk pixel
    }
    
    fetcher_step = FetcherStep::GET_TILE;
    fetcher_dots = 0;
    fetcher_x = 0;
    fetcher_window = false;
    lcd_x = 0;
    // Discard the 8 junk pixels + SCX fine scroll
    discard_pixels = 8 + (scx & 7);
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
                // Per SameBoy: go directly to PUSH, no SLEEP step
                fetcher_step = FetcherStep::PUSH;
                fetcher_dots = 0;
            }
            break;
            
        // Note: Per SameBoy display.c lines 862-872, there's no SLEEP step between
        // GET_TILE_DATA_HIGH and PUSH. The fetcher is only 6 states (GET_TILE T1/T2,
        // GET_DATA_LOW T1/T2, GET_DATA_HIGH T1/T2) plus PUSH.
            
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
    
    // Per SameBoy display.c lines 127-130: ensure FIFO has 8 transparent slots
    while (sprite_fifo_size < 8) {
        PushSpritePixel(0, 0, 0, 0);  // Push transparent pixel
    }
    
    // Per SameBoy lines 132-145: overlay sprite pixels
    // flip_xor: when X-flip is set, XOR position with 7 to reverse order
    uint8_t flip_xor = (spr.flags & 0x20) ? 0 : 7;
    
    for (int i = 7; i >= 0; i--) {
        // Always read MSB (bit 7), shift after each read (like SameBoy)
        uint8_t color = ((hi >> 7) & 1) << 1 | ((lo >> 7) & 1);
        lo <<= 1;
        hi <<= 1;
        
        // Target position: XOR with flip_xor for X-flip
        uint8_t pos = i ^ flip_xor;
        FIFOPixel* target = &sprite_fifo[(sprite_fifo_head + pos) & 0xF];
        
        // Only overlay if pixel is non-transparent AND target is transparent
        // Per SameBoy line 137: for DMG, all sprites have priority=0, so only
        // transparent pixels can be overwritten.
        // This gives X-coordinate priority: sprites processed first (lower X) keep their pixels.
        if (color != 0 && target->color == 0) {
            target->color = color;
            target->palette = (spr.flags >> 4) & 1;
            target->bg_priority = (spr.flags >> 7) & 1;
            target->oam_index = spr.oam_index;
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

bool PPU::RenderPixel() {
    // Window trigger check - must be BEFORE FIFO pops
    // Per SameBoy display.c line 1897: window activates when WX == position_in_line + 7
    // Per SameBoy: requires window_triggered (wy_triggered) to be set
    // Per reference PPU line 1189: WX must be < 166 for window to trigger
    if (!fetcher_window && IsWindowEnabled() && window_triggered && 
        wx < 166 && lcd_x + 7 == wx) {
        // Per reference PPU line 1191: Increment window_line when window triggers
        window_line++;
        fetcher_window = true;
        window_active = true;
        // Per SameBoy line 1915: only clear BG FIFO, NOT sprite FIFO
        bg_fifo_head = bg_fifo_size = 0;
        fetcher_step = FetcherStep::GET_TILE;
        fetcher_dots = 0;
        fetcher_x = 0;
        return false;  // Don't increment lcd_x - no pixel rendered yet
    }
    
    FIFOPixel bg = PopBGPixel();
    FIFOPixel obj = {0, 0, 0, 0};
    if (sprite_fifo_size > 0) obj = PopSpritePixel();
    
    uint8_t color;
    if (obj.color != 0 && IsSpritesEnabled() && (!obj.bg_priority || bg.color == 0)) {
        color = ((obj.palette ? obp1 : obp0) >> (obj.color * 2)) & 3;
    } else {
        // Per SameBoy display.c line 1243:
        // When BG disabled on DMG, use color 0 from BGP palette, not raw 0
        color = IsBGEnabled() ? ((bgp >> (bg.color * 2)) & 3) : (bgp & 3);
    }
    
    if (lcd_x < 160 && ly < 144) {
        framebuffer[ly * 160 + lcd_x] = color;
    }
    return true;  // Pixel rendered
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
    // Use mode_for_interrupt (not mode) for interrupt evaluation
    // -1 or 3 = no mode-based interrupt
    bool line = ((stat & 0x40) && lyc_match);  // LYC=LY interrupt
    
    switch (mode_for_interrupt) {
        case 0: line |= (stat & 0x08) != 0; break;  // Mode 0 (HBlank)
        case 1: line |= (stat & 0x10) != 0; break;  // Mode 1 (VBlank)
        case 2: line |= (stat & 0x20) != 0; break;  // Mode 2 (OAM)
        // default: -1 or 3, no mode-based interrupt
    }
    
    // Rising edge detection - only trigger on LOW→HIGH transition
    if (line && !stat_line) {
        stat_irq = true;
    }
    stat_line = line;
}

// === Register Access ===
uint8_t PPU::ReadRegister(uint16_t addr) const {
    switch (addr) {
        case 0xFF40: return lcdc;
        case 0xFF41: {
            // Per SameBoy display.c:
            // - At dot 0 of line (non-zero), STAT mode bits still show 0 (HBLANK)
            // - STAT mode = 3 at dot 83 (based on pan docs: 80-dot OAM + 3-dot transition)
            // Note: mode_for_interrupt changes earlier for interrupt purposes
            uint8_t stat_mode = mode;
            
            if (mode == OAM_SCAN) {
                if (dot_counter == 0 && ly != 0) {
                    stat_mode = HBLANK;  // Return mode 0 at dot 0
                } else if (dot_counter >= 83) {
                    stat_mode = PIXEL_TRANSFER;  // STAT shows mode 3 at dot 83+
                }
            }
            return stat | stat_mode | 0x80;
        }
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

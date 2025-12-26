#include "PPU.hpp"
#include <cstdio>

PPU::PPU() {
    Reset();
}

void PPU::Reset(bool bootRomEnabled) {
    mode = bootRomEnabled ? HBLANK : OAM_SCAN;  // LCD off starts in mode 0
    mode_visible = mode;       // Visible mode starts in sync with internal
    next_mode_visible = mode;  // No pending update
    mode_visibility_delay = 0; // No pending update
    
    dot_counter = 0;
    ly = 0;
    ly_for_comparison = 0;  // Per SameBoy: comparison value starts at 0
    window_line = -1;
    window_active = false;
    window_triggered = false;
    lcd_just_enabled = false;
    first_line_after_lcd = false;
    ly_update_pending = false;
    ly_comparator_delay = 0;
    next_ly = 0;
    oam_read_blocked = false;
    oam_write_blocked = false;
    vram_read_blocked = false;
    vram_write_blocked = false;
    
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
    position_in_line = 0;
    
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
        // ========================================
        // PHASE 1: Commit Scheduled Visibility Changes
        // Visible state (LY, mode) updates FIRST
        // ========================================
        if (dot_counter == 0 && ly_update_pending) {
            ly = next_ly;  // Visible LY changes NOW
            ly_update_pending = false;
            // Note: ly_for_comparison and STAT bit 2 were already updated at line END
            // Phase 1 just commits the visible ly value
        }
        
        // Mode visibility update - uses countdown for delay
        if (mode_visibility_delay > 0) {
            mode_visibility_delay--;
            if (mode_visibility_delay == 0) {
                mode_visible = next_mode_visible;  // Visible mode changes NOW
            }
        }
        
        // ========================================
        // PHASE 2: Update Comparators & STAT Bits
        // Runs after countdown delay expires
        // ========================================
        if (ly_comparator_delay > 0) {
            ly_comparator_delay--;
            if (ly_comparator_delay == 0) {
                ly_for_comparison = ly;  // NOW update comparator input
                CheckStatInterrupt();  // STAT bit 2 and interrupts updated NOW
            }
        }
        
        // ========================================
        // PHASE 3: Mode Logic & State Advancement
        // ========================================
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
    if (dot_counter == 0) {
        // Per SameBoy display.c lines 508-521 (wy_check):
        // Set wy_triggered when LY == WY at the start of each line
        // This check happens for EVERY line including line 0
        if (IsWindowEnabled() && ly == wy) {
            window_triggered = true;
        }
        
        // Per SameBoy display.c line 1790: OAM blocked at Mode 2 entry
        // VRAM is blocked later at Mode 3 entry (line 1701), not here
        oam_read_blocked = true;
        oam_write_blocked = true;
        
        // Fire Mode 2 interrupt (only for lines != 0)
        // Line 0 has special handling in VBlank/HBlank transitions
        if (ly != 0) {
            mode_for_interrupt = 2;
            CheckStatInterrupt();
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
    
    // Per SameBoy display.c lines 1807-1818: VRAM blocked at OAM index 37 on DMG
    // The check happens AFTER GB_SLEEP(2), so timing is:
    //   4 cycles before loop + (38 entries * 2 cycles) = 80 cycles into line
    //   My dot 0 = SameBoy cycle 4, so SameBoy cycle 80 = my dot 76
    if (dot_counter == 76) {
        vram_read_blocked = true;
        vram_write_blocked = true;
    }
    
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
        // Immediate visibility update for OAM_SCAN→PIXEL_TRANSFER
        mode_visible = PIXEL_TRANSFER;
        mode_visibility_delay = 0;  // No delay needed
        // Per SameBoy: VRAM blocked when Mode 3 starts
        // (OAM was already blocked at Mode 2 entry)
        vram_read_blocked = true;
        vram_write_blocked = true;
        InitFetcher();
    }
}

// === Mode 3: Pixel Transfer ===
// Per Pan Docs: Fetcher runs at 2 dots per step, FIFO pops at 1 pixel per dot
void PPU::StepPixelTransfer() {
    // Per SameBoy display.c x_for_object_match:
    // Sprite matching uses position_in_line + 8
    // This allows matching sprites at X < 8 during the discard phase (when position_in_line is negative)
    uint8_t x_for_match = (position_in_line + 8) & 0xFF;
    // Clamp to 0 if we'd wrap around (position_in_line < -8)
    if (position_in_line < -8) x_for_match = 0;
    
    // Check for sprite at current position (before popping)
    // Per SameBoy: process sprites from end of array (lowest X) first
    // Array is sorted with highest X first, so iterate backwards to get lowest X
    if (!fetching_sprite && IsSpritesEnabled()) {
        for (int i = sprite_count - 1; i >= 0; i--) {
            // Match when sprite X equals our current position (position_in_line + 8)
            if (scanline_sprites[i].x != 0 && scanline_sprites[i].x == x_for_match) {
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
    
    // Per SameBoy display.c line 1897: Window trigger check uses position_in_line + 7
    // This must happen even during the discard phase (when position_in_line is negative)
    // WX=7 with SCX=0 should trigger at position_in_line=0 (first visible pixel)
    if (!fetching_sprite && bg_fifo_size > 0 && !fetcher_window && IsWindowEnabled() && window_triggered) {
        uint8_t wx_match = (position_in_line + 7) & 0xFF;
        if (wx < 166 && wx == wx_match) {
            // Window trigger!
            window_line++;
            fetcher_window = true;
            window_active = true;
            // Per SameBoy line 1915: only clear BG FIFO, NOT sprite FIFO
            bg_fifo_head = bg_fifo_size = 0;
            fetcher_step = FetcherStep::GET_TILE;
            fetcher_dots = 0;
            fetcher_x = 0;
            // Don't pop or render - window just triggered, fetcher needs to restart
            return;
        }
    }
    
    // Pop pixel from FIFO if it has data
    // Per Pan Docs: pixels pop at 1 per dot when FIFO has data
    if (!fetching_sprite && bg_fifo_size > 0) {
        if (position_in_line < 0) {
            // Discard phase: pop pixels but don't render to screen
            PopBGPixel();
            if (sprite_fifo_size > 0) PopSpritePixel();
            position_in_line++;
        } else {
            // Visible phase: render to screen
            // RenderPixel returns false if window just triggered (no pixel rendered)
            if (RenderPixel()) {
                lcd_x++;
                position_in_line++;
                
                if (lcd_x >= 160) {
                    // Debug: trace Mode 3 end
                    static int m3end_trace = 0;
                    if (ly == 0 && m3end_trace++ < 5) {
                        fprintf(stderr, "MODE3_END ly=%d dot=%d\n", ly, dot_counter);
                    }
                    mode = HBLANK;
                    // Immediate visibility update for PIXEL_TRANSFER→HBLANK
                    mode_visible = HBLANK;
                    mode_visibility_delay = 0;  // No delay needed
                    mode_for_interrupt = 0;
                    // Per SameBoy display.c line 2104: OAM/VRAM unblocked at HBlank entry
                    oam_read_blocked = false;
                    oam_write_blocked = false;
                    vram_read_blocked = false;
                    vram_write_blocked = false;
                    CheckStatInterrupt();
                }
            }
        }
    }
}

// === Mode 0: HBlank ===
void PPU::StepHBlank() {
    // Per SameBoy display.c: STAT mode=3 visible at cycle 78 (dot 77), but actual Mode 3
    // processing (mode_3_start) begins at cycle 83 (dot 82).
    //
    // CRITICAL: STAT mode bits must be LATCHED at dot 77, not recomputed on read!
    // The test expects different reads at 1 M-cycle apart to see different modes.
    // This only works if STAT bits are latched at the exact transition dot.
    
    if (lcd_just_enabled && ly == 0) {
        // STAT mode bits = 3 at dot 77 (latched, visible to CPU reads)
        // Per SameBoy lines 1692-1693, 1696-1697, 1700-1702: update STAT and block OAM/VRAM
        if (dot_counter == 77) {
            stat = (stat & ~0x03) | PIXEL_TRANSFER;  // LATCH mode bits in STAT
            mode_visible = PIXEL_TRANSFER;  // Also update mode_visible directly
            mode_for_interrupt = 3;
            // Per SameBoy line 1697, 1701: OAM and VRAM blocked when Mode 3 starts
            // For lcd_just_enabled, there's no Mode 2, so set it here
            oam_read_blocked = true;
            oam_write_blocked = true;
            vram_read_blocked = true;
            vram_write_blocked = true;
        }
        
        // Internal mode changes at dot 82 when rendering actually starts
        // Per SameBoy lines 1704-1714: mode_3_start after +2+3 more cycles
        if (dot_counter == 82) {
            mode = PIXEL_TRANSFER;
            // mode_visible already set at dot 77
            InitFetcher();
            lcd_just_enabled = false;
        }
    }
    
    // Determine line end position
    // Per SameBoy line 1690: "Mode 0 is shorter on the first line 0"
    // The +8 to cycles_for_line affects HBlank duration, not total line length
    // Test expects LY=0 at cycle 110 (dot ~440) and LY=1 at cycle 130 (dot ~520)
    // Line 0 ends between these cycles, around dot 451
    uint16_t line_end = first_line_after_lcd ? 451 : 455;
    
    if (dot_counter == line_end) {
        // Clear shortened line flag (only first line is affected)
        if (first_line_after_lcd) {
            first_line_after_lcd = false;
        }
        
        // Note: window_line is incremented when window TRIGGERS (in RenderPixel), not here
        
        // Schedule LY update for next line start
        next_ly = ly + 1;
        ly_update_pending = true;
        
        // KEY FIX: Update ly_for_comparison AT LINE END, not dot 0
        // This ensures CPU reads at cycle 111 (which happen BEFORE Phase 1)
        // see the correct ly_for_comparison value (-1 for lines != 0)
        // Per SameBoy line 1776: ly_for_comparison = current_line ? -1 : 0
        ly_for_comparison = (next_ly != 0) ? -1 : 0;
        // Clear STAT bit 2 directly (don't use CheckStatInterrupt to avoid IRQ side effects)
        // This is safe because -1 will never match any valid LYC value
        if (ly_for_comparison == -1) {
            stat &= ~0x04;  // Clear LYC coincidence bit
        }
        // Schedule Phase 2 to update ly_for_comparison to actual ly value after delay
        ly_comparator_delay = 4;  // 4 dots: matches mode visibility timing
        
        // KEY FIX: Set OAM blocked AT LINE END, not dot 0 (same reason as ly_for_comparison)
        // CPU reads at cycle 111 happen BEFORE PPU Step runs, so set it here
        // For VBlank transition (next_ly == 144), OAM stays accessible
        if (next_ly < 144) {
            oam_read_blocked = true;
            oam_write_blocked = true;
        }
        
        // Mode transitions still happen at line end
        if (next_ly == 144) {
            // Per SameBoy display.c lines 2160-2162, 2177-2178:
            // At VBlank entry, Mode 2 interrupt (bit 5) also fires if enabled
            // This is a DMG quirk where line 144 triggers the "OAM STAT interrupt"
            if ((stat & 0x20) && !stat_line) {
                stat_irq = true;
            }
            
            mode = VBLANK;
            // Immediate visibility update for HBlank→VBlank
            mode_visible = VBLANK;
            mode_visibility_delay = 0;
            mode_for_interrupt = 1;  // Per SameBoy: Mode 1 interrupt check
            vblank_irq = true;
            frame_complete = true;
            // OAM/VRAM accessible during VBlank
            oam_read_blocked = false;
            oam_write_blocked = false;
            vram_read_blocked = false;
            vram_write_blocked = false;
            CheckStatInterrupt();  // Mode 1 interrupt at VBlank entry
        } else {
            // Per SameBoy display.c lines 1782-1792:
            // At line transition, STAT mode bits are EXPLICITLY SET TO 0 first!
            // Timing: dots 0-3 show mode 0, dot 4+ shows mode 2
            // Test cycle 111 (dot 0): expects $80 (mode 0)
            // Test cycle 112 (dot 4): expects $82 (mode 2)
            mode = OAM_SCAN;
            // Set mode_visible to HBLANK NOW (matching SameBoy STAT &= ~3)
            // Schedule update to OAM_SCAN for 4 cycles later
            mode_visible = HBLANK;  // IMMEDIATE: visible mode stays HBLANK
            next_mode_visible = OAM_SCAN;
            mode_visibility_delay = 4;  // Will become OAM_SCAN at dot 4
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
        // Schedule LY update for next line start (don't update visible ly yet!)
        next_ly = ly + 1;
        if (next_ly > 153) next_ly = 0;  // Wrap at frame boundary
        ly_update_pending = true;
        
        // Update ly_for_comparison for INTERRUPT timing (fires at line boundary)
        // But STAT bit 2 will NOT change because CheckStatInterrupt uses effective_ly
        ly_for_comparison = ly + 1;
        if (ly_for_comparison > 153) ly_for_comparison = 0;
        CheckStatInterrupt();  // Evaluate interrupt with internal timing
        
        // Mode transitions
        if (next_ly == 0) {
            mode = OAM_SCAN;
            // Same 4-cycle delay as HBlank→OAM_SCAN (line 0 after VBlank)
            mode_visible = VBLANK;  // Stay at VBlank visibility briefly
            next_mode_visible = OAM_SCAN;
            mode_visibility_delay = 4;
            mode_for_interrupt = 2;  // Per SameBoy: Mode 2 interrupt check
            window_line = -1;  // Per SameBoy: starts at -1, incremented when window triggers
            window_triggered = false;
            sprite_count = 0;
            window_active = false;
        }
        
        dot_counter = (uint16_t)-1;
        CheckStatInterrupt();  // Re-evaluate after mode change
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
    // Per SameBoy: position_in_line starts negative
    // -8 for the junk pixels, minus SCX fine scroll
    position_in_line = -8 - (scx & 7);
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
    // Window trigger is now checked in StepPixelTransfer using position_in_line
    // This function just renders the pixel
    
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
// This function evaluates the STAT interrupt line based on current state.
// CRITICAL: STAT bit 2 uses ly_for_comparison, which LAGS behind visible ly!
void PPU::CheckStatInterrupt() {
    // Per SameBoy GB_STAT_update line 525:
    // if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) return;
    // When LCD is off, the comparison clock is frozen
    if (!IsLCDEnabled()) return;
    
    // PHASED MODEL: STAT bit 2 uses ly_for_comparison (lags behind visible ly)
    // At line boundary:
    //   - Phase 1: visible ly = 1, ly_for_comparison = -1 (no match)
    //   - Phase 2: ly_for_comparison = 1, STAT bit 2 updates
    // CPU reads between phases see: LY=1, but STAT shows no coincidence (old comparison)
    
    // Per SameBoy lines 532-542: when ly_for_comparison == -1, CLEAR the bit
    // This is different from normal case - -1 means "no comparison" which clears bit
    bool lyc_match = false;
    if (ly_for_comparison != -1) {
        lyc_match = (ly_for_comparison == lyc);
    }
    // Always update the bit (per SameBoy: -1 clears, match sets, no-match clears)
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
            // STAGED PPU: STAT mode bits come from mode_visible, not internal mode
            // mode_visible lags behind internal mode by 1 cycle (updated in Phase 1)
            // This allows STAT to show the old mode for 1 cycle after internal mode changes
            
            uint8_t stat_mode;
            
            if (lcd_just_enabled && ly == 0) {
                // LCD line 0 special case: use latched bits (written at LCD enable and dot 77)
                stat_mode = stat & 0x03;
            } else {
                // Normal operation: use mode_visible (staged PPU model)
                stat_mode = mode_visible;
                
                // Dot-level quirks still apply to the visible mode
                if (mode_visible == OAM_SCAN) {
                    if (dot_counter == 0 && ly != 0) {
                        stat_mode = HBLANK;  // Return mode 0 at dot 0
                    } else if (dot_counter >= 83) {
                        stat_mode = PIXEL_TRANSFER;  // STAT shows mode 3 at dot 83+
                    }
                }
            }
            
            // Return STAT with mode bits
            // Bit 2 (LYC coincidence) comes from cached stat register, updated by CheckStatInterrupt()
            // Bits 3-6: preserved from stat (interrupt enables)
            // Bit 7: always 1
            return (stat & 0xFC) | stat_mode | 0x80;
        }
        case 0xFF42: return scy;
        case 0xFF43: return scx;
        case 0xFF44:
            // HARDWARE QUIRK: Visible LY changes BEFORE STAT LYC bit updates!
            // At line boundary, LY returns next value, but STAT bit 2 still uses old LY.
            // Return next_ly if update is pending (line boundary crossed)
            return ly_update_pending ? next_ly : ly;
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
                ly = 0;
                dot_counter = 0;
                mode = HBLANK;  // Per SameBoy line 575: STAT mode = 0
                mode_visible = HBLANK;  // Visible mode also HBLANK
                mode_visibility_delay = 0;
                // OAM/VRAM accessible when LCD is off
                oam_read_blocked = false;
                oam_write_blocked = false;
                vram_read_blocked = false;
                vram_write_blocked = false;
            }
            lcdc = value;
            if ((value & 0x80) && !was_enabled) {
                // LCD turning ON - per SameBoy display.c lines 1664-1714:
                // Line 0 after LCD enable has special behavior:
                // - Starts in Mode 0 (not Mode 2)
                // - Skips OAM scan, goes directly to Mode 3
                // - Has 2-cycle timing offset
                // - Line is ~4 dots shorter (per SameBoy line 1690: "Mode 0 is shorter")
                lcd_just_enabled = true;
                first_line_after_lcd = true;  // Line 0 ends 4 dots early
                ly = 0;
                ly_for_comparison = 0;  // Comparison value also starts at 0
                dot_counter = 0;
                mode = HBLANK;  // Start in Mode 0
                mode_visible = HBLANK;  // Visible mode starts in Mode 0
                mode_visibility_delay = 0;
                stat = (stat & ~0x03) | HBLANK;  // LATCH mode bits to HBLANK
                mode_for_interrupt = -1;  // No interrupt initially
                // Per SameBoy display.c line 1674-1677: OAM/VRAM not blocked at LCD enable
                oam_read_blocked = false;
                oam_write_blocked = false;
                vram_read_blocked = false;
                vram_write_blocked = false;
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
            // Note: LYC coincidence flag is updated during PPU step, not on LYC write
            // See SameBoy's ly_for_comparison which handles timing edge cases
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
    // Per SameBoy: VRAM accessible when LCD is OFF or when not blocked
    // vram_read_blocked is set at specific T-cycle timings during Mode 3
    if (!IsLCDEnabled()) {
        return vram[addr - 0x8000];  // LCD off - always accessible
    }
    
    // Per SameBoy: check vram_read_blocked flag
    if (vram_read_blocked) {
        return 0xFF;
    }
    
    return vram[addr - 0x8000];
}

void PPU::WriteVRAM(uint16_t addr, uint8_t value) {
    // Per SameBoy: VRAM writable when LCD is OFF or when not blocked
    if (!IsLCDEnabled()) {
        vram[addr - 0x8000] = value;  // LCD off - always accessible
        return;
    }
    
    // Per SameBoy: check vram_write_blocked flag
    if (!vram_write_blocked) {
        vram[addr - 0x8000] = value;
    }
}

uint8_t PPU::ReadOAM(uint16_t addr) const {
    // Per SameBoy: OAM accessible when LCD is OFF or when not blocked
    // oam_read_blocked is set at specific T-cycle timings during Mode 2/3
    if (!IsLCDEnabled()) {
        return oam[addr - 0xFE00];  // LCD off - always accessible
    }
    
    // Per SameBoy memory.c line 560: check oam_read_blocked flag
    if (oam_read_blocked) {
        return 0xFF;
    }
    
    return oam[addr - 0xFE00];
}

void PPU::WriteOAM(uint16_t addr, uint8_t value) {
    // Per SameBoy: OAM writable when LCD is OFF or when not blocked
    // oam_write_blocked is set slightly before oam_read_blocked
    if (!IsLCDEnabled()) {
        oam[addr - 0xFE00] = value;  // LCD off - always accessible
        return;
    }
    
    // Per SameBoy: check oam_write_blocked flag
    if (!oam_write_blocked) {
        oam[addr - 0xFE00] = value;
    }
}

void PPU::DMAWriteOAM(uint8_t index, uint8_t value) {
    if (index < 160) oam[index] = value;
}

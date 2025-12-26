#pragma once

#include <cstdint>
#include <array>

/**
 * PPU - Picture Processing Unit (Hardware-Accurate Pixel FIFO)
 * 
 * Hardware Behavior:
 * - Operates on dot clock (same as T-cycle: 4.194304 MHz)
 * - Cycles through modes: OAM Scan -> Pixel Transfer -> HBlank -> VBlank
 * - Uses dual 16-pixel FIFOs for background and sprites
 * - 5-step fetcher: Get Tile -> Get Data Low -> Get Data High -> Sleep -> Push
 * 
 * Timing:
 * - Scanline: 456 dots total
 * - Mode 2 (OAM Scan): 80 dots
 * - Mode 3 (Pixel Transfer): 172-289 dots (variable)
 * - Mode 0 (HBlank): remainder to 456
 * - Mode 1 (VBlank): 10 scanlines (4560 dots)
 */
class PPU {
public:
    PPU();
    
    void Reset(bool bootRomEnabled = false);
    
    // Advance PPU by specified T-cycles (1 dot = 1 T-cycle)
    void Step(uint8_t cycles);
    
    // === Register Interface (memory-mapped I/O) ===
    uint8_t ReadRegister(uint16_t addr) const;
    void WriteRegister(uint16_t addr, uint8_t value);
    
    // === VRAM Interface ($8000-$9FFF) ===
    uint8_t ReadVRAM(uint16_t addr) const;
    void WriteVRAM(uint16_t addr, uint8_t value);
    
    // === OAM Interface ($FE00-$FE9F) ===
    uint8_t ReadOAM(uint16_t addr) const;
    void WriteOAM(uint16_t addr, uint8_t value);
    void DMAWriteOAM(uint8_t index, uint8_t value);
    
    // === Interrupt Signals ===
    bool IsVBlankInterruptRequested() const { return vblank_irq; }
    bool IsStatInterruptRequested() const { return stat_irq; }
    void ClearVBlankInterrupt() { vblank_irq = false; }
    void ClearStatInterrupt() { stat_irq = false; }
    
    // === Display Output ===
    const std::array<uint8_t, 160 * 144>& GetFramebuffer() const { return framebuffer; }
    bool IsFrameComplete() const { return frame_complete; }
    void ClearFrameComplete() { frame_complete = false; }
    
    // === State Query ===
    uint8_t GetMode() const { return mode; }
    uint8_t GetLY() const { return ly; }
    bool IsVRAMAccessible() const { return mode != PIXEL_TRANSFER; }
    bool IsOAMAccessible() const { return mode == HBLANK || mode == VBLANK; }
    
private:
    // === PPU Modes ===
    enum Mode : uint8_t {
        HBLANK = 0,
        VBLANK = 1,
        OAM_SCAN = 2,
        PIXEL_TRANSFER = 3
    };
    
    // === Fetcher States (4-step state machine per SameBoy) ===
    // Per SameBoy display.c lines 862-872: GET_TILE (2T), GET_DATA_LOW (2T),
    // GET_DATA_HIGH (2T), then PUSH immediately - no SLEEP step
    enum class FetcherStep : uint8_t {
        GET_TILE,           // Read tile number from tilemap (2 T-cycles)
        GET_TILE_DATA_LOW,  // Read low byte of tile data (2 T-cycles)
        GET_TILE_DATA_HIGH, // Read high byte of tile data (2 T-cycles)
        PUSH                // Push 8 pixels to FIFO (1 T-cycle when FIFO ready)
    };
    
    // === Pixel FIFO Entry ===
    struct FIFOPixel {
        uint8_t color;          // 0-3 (2-bit color value)
        uint8_t palette;        // DMG: 0-1 (OBP0/OBP1), CGB: 0-7
        uint8_t bg_priority;    // OBJ-to-BG priority (1=behind BG colors 1-3)
        uint8_t oam_index;      // For DMG X-priority: lower X or earlier OAM wins
    };
    
    // === Sprite Entry (OAM scan result) ===
    struct SpriteEntry {
        uint8_t y;
        uint8_t x;
        uint8_t tile;
        uint8_t flags;
        uint8_t oam_index;  // For CGB priority
    };
    
    // === Internal State ===
    Mode mode;                  // Internal mode (drives PPU logic)
    Mode mode_visible;          // Visible mode (STAT reads) - lags behind internal mode
    Mode next_mode_visible;     // Pending visible mode to apply after delay
    uint8_t mode_visibility_delay; // Countdown: 0=no update, 1=update next cycle, 2+=decrement
    
    uint16_t dot_counter;       // Dots within current scanline (0-455)
    uint8_t ly;                 // Current scanline (0-153)
    int16_t ly_for_comparison;  // Per SameBoy: value used for LYC comparison, may differ from ly
    int16_t window_line;        // Window internal line counter (starts at -1)
    bool window_active;         // Window triggered on current scanline
    bool window_triggered;      // Window was triggered this frame
    bool lcd_just_enabled;      // Per SameBoy: Line 0 after LCD enable has special timing
    bool first_line_after_lcd;  // Line 0 after LCD enable is shortened (4 dots less)
    bool ly_update_pending;     // Per SameBoy: visible LY updates at line START, not end
    uint8_t ly_comparator_delay; // Countdown: 0=no update, N=decrement, 0→update ly_for_comparison
    uint8_t next_ly;            // Pending LY value to apply at next line start
    
    // Per SameBoy: OAM/VRAM access blocking flags (independent of mode)
    // These are set at specific T-cycle timings during Mode 2/3 transitions
    bool oam_read_blocked;      // OAM reads return $FF when true (Mode 2/3)
    bool oam_write_blocked;     // OAM writes are ignored when true (Mode 2/3)
    bool vram_read_blocked;     // VRAM reads return $FF when true (Mode 3 only)
    bool vram_write_blocked;    // VRAM writes are ignored when true (Mode 3 only)
    
    // === Registers ===
    uint8_t lcdc;   // $FF40 - LCD Control
    uint8_t stat;   // $FF41 - LCD Status
    uint8_t scy;    // $FF42 - Scroll Y
    uint8_t scx;    // $FF43 - Scroll X
    uint8_t lyc;    // $FF45 - LY Compare
    uint8_t bgp;    // $FF47 - BG Palette
    uint8_t obp0;   // $FF48 - Object Palette 0
    uint8_t obp1;   // $FF49 - Object Palette 1
    uint8_t wy;     // $FF4A - Window Y
    uint8_t wx;     // $FF4B - Window X
    
    // === Memory ===
    std::array<uint8_t, 8192> vram;
    std::array<uint8_t, 160> oam;
    
    // === Framebuffer ===
    std::array<uint8_t, 160 * 144> framebuffer;
    
    // === Interrupt Flags ===
    bool vblank_irq;
    bool stat_irq;
    bool frame_complete;
    bool stat_line;          // Previous STAT interrupt line state
    int8_t mode_for_interrupt;  // Per SameBoy: separate from actual mode for STAT timing
                                // -1 = no mode-based interrupt, 0-2 = check this mode
    
    // === Pixel FIFO (16 entries each) ===
    std::array<FIFOPixel, 16> bg_fifo;
    std::array<FIFOPixel, 16> sprite_fifo;
    uint8_t bg_fifo_head;
    uint8_t bg_fifo_size;
    uint8_t sprite_fifo_head;
    uint8_t sprite_fifo_size;
    
    // === Fetcher State ===
    FetcherStep fetcher_step;
    uint8_t fetcher_dots;       // Dots spent in current step (0-1)
    uint8_t fetcher_x;          // Tile X being fetched
    uint8_t fetcher_tile_no;
    uint8_t fetcher_tile_low;
    uint8_t fetcher_tile_high;
    bool fetcher_window;        // Fetching window tiles
    
    // === Pixel Output State ===
    uint8_t lcd_x;              // Current LCD X (0-159)
    int16_t position_in_line;   // Per SameBoy: internal position, starts negative (-8 to -15)
                                // Used for sprite X matching at left edge
    
    // === OAM Scan Results ===
    std::array<SpriteEntry, 10> scanline_sprites;
    uint8_t sprite_count;
    uint8_t sprite_index;       // Current sprite being fetched
    bool fetching_sprite;
    
    // === Mode Stepping ===
    void StepOAMScan();
    void StepPixelTransfer();
    void StepHBlank();
    void StepVBlank();
    
    // === Fetcher Operations ===
    void InitFetcher();
    void AdvanceFetcher();
    uint8_t FetchTileNumber();
    uint8_t FetchTileDataLow();
    uint8_t FetchTileDataHigh();
    void PushRowToFIFO();
    
    // === FIFO Operations ===
    void ClearFIFOs();
    void PushBGPixel(uint8_t color);
    FIFOPixel PopBGPixel();
    void PushSpritePixel(uint8_t color, uint8_t palette, bool bg_priority, uint8_t oam_idx);
    FIFOPixel PopSpritePixel();
    bool RenderPixel();  // Returns true if pixel rendered, false if window triggered
    
    // === Sprite Operations ===
    void FetchSprite();
    
    // === Utility ===
    void CheckStatInterrupt();
    
    // === LCDC Bit Helpers ===
    bool IsLCDEnabled() const { return lcdc & 0x80; }
    bool IsWindowEnabled() const { return lcdc & 0x20; }
    bool IsSpritesEnabled() const { return lcdc & 0x02; }
    bool IsBGEnabled() const { return lcdc & 0x01; }
    bool IsTallSprites() const { return lcdc & 0x04; }
    uint16_t GetBGTileMapBase() const { return (lcdc & 0x08) ? 0x9C00 : 0x9800; }
    uint16_t GetWindowTileMapBase() const { return (lcdc & 0x40) ? 0x9C00 : 0x9800; }
    bool UsesSignedTileIndex() const { return !(lcdc & 0x10); }
};

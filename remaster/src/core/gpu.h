// GPU PS1: rasterizzatore software su VRAM 1024x512 (16 bit), alimentato via GP0/GP1.
// Obiettivo attuale: fedelta' 1:1 col PS1 (prima base di verifica). In futuro il
// renderer OpenGL ad alta risoluzione leggera' gli stessi comandi.
#pragma once
#include <cstdint>
#include <vector>

namespace cb {

struct Gpu {
    std::vector<uint16_t> vram = std::vector<uint16_t>(1024 * 512, 0);

    // stato disegno
    int tpx = 0, tpy = 0, semi_mode = 0, tex_depth = 0;   // texpage
    bool dither = false, draw_to_display = true;
    int win_mask_x = 0, win_mask_y = 0, win_off_x = 0, win_off_y = 0;
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = 1023, clip_y1 = 511;
    int off_x = 0, off_y = 0;
    bool set_mask = false, check_mask = false;

    // stato display (GP1)
    int disp_x = 0, disp_y = 0;
    int hres = 256, vres = 240;
    bool disp_enabled = false, rgb24 = false;
    uint32_t gpustat_extra = 0;

    // FIFO GP0
    std::vector<uint32_t> fifo;
    int need = 0;                 // parole attese per comando corrente (0 = nuovo comando)
    bool polyline = false;
    // trasferimenti VRAM
    bool cpu2vram = false; int cx = 0, cy = 0, cw = 0, ch = 0, cpos = 0;
    bool vram2cpu = false; int rx = 0, ry = 0, rw = 0, rh = 0, rpos = 0;

    uint64_t prim_count = 0;

    void gp0(uint32_t w);
    void gp1(uint32_t w);
    uint32_t gpustat() const;
    uint32_t gpuread();

    // Cammina una linked list (DMA2) in RAM e invia i pacchetti a GP0
    void dma_linked_list(uint8_t* ram, uint32_t ram_mask, uint32_t addr);
    void dma_block(uint8_t* ram, uint32_t ram_mask, uint32_t addr, uint32_t words);

private:
    void exec_command();
    void draw_poly(bool quad, bool gouraud, bool textured, bool semi, bool raw);
    void draw_rect(int sizeMode, bool textured, bool semi, bool raw);
    void draw_line(bool gouraud, bool semi);
    void fill_rect();
    void copy_rect();
    struct V { int x, y; int r, g, b; int u, v; };
    void raster_tri(const V& a, const V& b, const V& c, bool gouraud, bool textured, bool semi, bool raw, uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi);
    uint16_t fetch_tex(int u, int v, uint16_t clut, int px, int py, int depth);
    void put_pixel(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi);
};

} // namespace cb


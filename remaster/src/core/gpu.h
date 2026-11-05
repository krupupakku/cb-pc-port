#pragma once
#include <cstdint>
#include <vector>

namespace cb {

// Emulatore GPU PlayStation (GP0 / GP1 + VRAM 1024x512 a 16-bit)
// Include backend Remaster+ per rendering interno ad alta risoluzione (1x, 2x, 3x, 4x Ultra HD).
struct Gpu {
    std::vector<uint16_t> vram = std::vector<uint16_t>(1024 * 512, 0); // 1024 x 512 x 16-bit (1 MB VRAM PS1)

    // Registri GP1 (visualizzazione)
    int disp_x = 0, disp_y = 0;
    int hres = 320, vres = 240;
    bool disp_enabled = false;
    bool rgb24 = false;

    // Registri GP0 (stato rendering)
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = 1023, clip_y1 = 511;
    int off_x = 0, off_y = 0;
    int tpx = 0, tpy = 0, semi_mode = 0, tex_depth = 0;   // texpage
    bool dither = false, draw_to_display = false, set_mask = false, check_mask = false;

    // Statistiche
    uint64_t prim_count = 0;

    // Supporto Remaster+ ad alta risoluzione interna (1x, 2x, 3x, 4x)
    int scale = 1;
    bool bilinear_tex = false; // Filtro bilineare su texture dei poligoni 3D (F8)
    std::vector<uint32_t> hires_vram;

    void set_scale(int s);
    void flush_hires();
    void render_hires_band(int thread_id, int by0, int by1);

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
    void put_pixel_hires(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi);

    struct HCmd {
        bool gouraud = false, textured = false, semi = false, raw = false;
        uint16_t clut = 0; int ctpx = 0, ctpy = 0, cdepth = 0, csemi = 0;
        int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
        V v[3];
        int ymin = 0, ymax = 0;
    };
    std::vector<HCmd> hq;
    void queue_hires_tri(const V& a, const V& b, const V& d, bool gouraud, bool textured, bool semi, bool raw,
                         uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi);
    void hires_tri(const HCmd& cmd, int by0, int by1);

    std::vector<uint32_t> fifo;
    size_t need = 0;
    bool polyline = false;

    bool cpu2vram = false;
    int cx = 0, cy = 0, cw = 0, ch = 0, cpos = 0;

    bool vram2cpu = false;
    int rx = 0, ry = 0, rw = 0, rh = 0, rpos = 0;
};

// Callback opzionale per la sostituzione dinamica dei glifi dei pulsanti con icone HD
using ButtonGlyphSampler = bool (*)(int btnIndex, float nu, float nv, uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA);
void SetButtonGlyphSampler(ButtonGlyphSampler sampler);

} // namespace cb

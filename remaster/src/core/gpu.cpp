#include "core/gpu.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cb {

static inline int sx11(uint32_t v) { return ((int)(v << 21)) >> 21; }
static inline uint32_t rd32(uint8_t* ram, uint32_t mask, uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & mask & ~3u), 4); return v; }

void Gpu::set_scale(int s) {
    int target = (s >= 2 && s <= 4) ? s : 1;
    if (target == scale && (target == 1 || !hires_vram.empty())) return;
    scale = target;
    if (scale > 1) {
        hires_vram.assign((size_t)(1024 * scale) * (512 * scale), 0xFF000000u);
        int maxW = 1024 * scale;
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 1024; x++) {
                uint16_t pix = vram[y * 1024 + x];
                uint32_t r = ((pix & 31) << 3) | ((pix & 31) >> 2);
                uint32_t g = (((pix >> 5) & 31) << 3) | (((pix >> 5) >> 2) & 7);
                uint32_t b = (((pix >> 10) & 31) << 3) | (((pix >> 10) >> 2) & 7);
                uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        hires_vram[(y * scale + dy) * maxW + (x * scale + dx)] = rgba;
                    }
                }
            }
        }
    } else {
        hires_vram.clear();
    }
}

uint32_t Gpu::gpustat() const {
    static uint32_t tog = 0; tog ^= 0x80000000u;
    uint32_t s = (uint32_t)(tpx & 15) | ((tpy & 1) << 4) | ((semi_mode & 3) << 5) | ((tex_depth & 3) << 7);
    s |= (dither ? 1u << 9 : 0) | (draw_to_display ? 1u << 10 : 0) | (set_mask ? 1u << 11 : 0) | (check_mask ? 1u << 12 : 0);
    s |= 1u << 13;
    int hr = hres == 256 ? 0 : hres == 320 ? 1 : hres == 512 ? 2 : hres == 640 ? 3 : 1;
    s |= (uint32_t)hr << 17;
    if (vres == 480) s |= 1u << 19;
    if (rgb24) s |= 1u << 21;
    if (!disp_enabled) s |= 1u << 23;
    s |= 0x1C000000u;
    return s | tog;
}

uint32_t Gpu::gpuread() {
    if (vram2cpu) {
        uint32_t out = 0;
        for (int i = 0; i < 2; i++) {
            int px = (rx + rpos % rw) & 1023, py = (ry + rpos / rw) & 511;
            out |= (uint32_t)vram[py * 1024 + px] << (16 * i);
            rpos++;
        }
        if (rpos >= rw * rh) vram2cpu = false;
        return out;
    }
    return 0;
}

void Gpu::gp1(uint32_t w) {
    uint32_t cmd = w >> 24;
    switch (cmd) {
    case 0x00: fifo.clear(); need = 0; cpu2vram = vram2cpu = false; disp_enabled = false;
               tpx = tpy = semi_mode = tex_depth = 0; clip_x0 = clip_y0 = 0; clip_x1 = 1023; clip_y1 = 511; off_x = off_y = 0; break;
    case 0x01: fifo.clear(); need = 0; break;
    case 0x03: disp_enabled = !(w & 1); break;
    case 0x05: disp_x = w & 0x3FF; disp_y = (w >> 10) & 0x1FF; break;
    case 0x08: {
        int h = w & 3; hres = h == 0 ? 256 : h == 1 ? 320 : h == 2 ? 512 : 640;
        if (w & 0x40) hres = 368;
        vres = (w & 4) ? 480 : 240; rgb24 = (w >> 4) & 1; break; }
    default: break;
    }
}

void Gpu::gp0(uint32_t w) {
    if (cpu2vram) {
        for (int i = 0; i < 2 && cpos < cw * ch; i++, cpos++) {
            int px = (cx + cpos % cw) & 1023, py = (cy + cpos / cw) & 511;
            uint16_t pix = (uint16_t)(w >> (16 * i));
            vram[py * 1024 + px] = pix;
            if (scale > 1 && !hires_vram.empty()) {
                uint32_t r = ((pix & 31) << 3) | ((pix & 31) >> 2);
                uint32_t g = (((pix >> 5) & 31) << 3) | (((pix >> 5) >> 2) & 7);
                uint32_t b = (((pix >> 10) & 31) << 3) | (((pix >> 10) >> 2) & 7);
                uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;
                int maxW = 1024 * scale;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        hires_vram[(py * scale + dy) * maxW + (px * scale + dx)] = rgba;
                    }
                }
            }
        }
        if (cpos >= cw * ch) cpu2vram = false;
        return;
    }
    if (need == 0) {
        fifo.clear(); fifo.push_back(w);
        uint32_t cmd = w >> 24; polyline = false;
        if (cmd >= 0x20 && cmd < 0x40) {
            bool quad = cmd & 8, tex = cmd & 4, gour = cmd & 0x10; int n = quad ? 4 : 3;
            need = 1; for (int i = 0; i < n; i++) { need += 1 + (tex ? 1 : 0) + ((gour && i > 0) ? 1 : 0); }
        } else if (cmd >= 0x40 && cmd < 0x60) {
            bool gour = cmd & 0x10; polyline = cmd & 8; need = gour ? 4 : 3;
        } else if (cmd >= 0x60 && cmd < 0x80) {
            need = 2 + ((cmd & 4) ? 1 : 0) + (((cmd >> 3) & 3) == 0 ? 1 : 0);
        } else if (cmd == 0x02) need = 3;
        else if (cmd == 0x80) need = 4;
        else if (cmd == 0xA0 || cmd == 0xC0) need = 3;
        else need = 1;
        if (need == 1) { exec_command(); need = 0; fifo.clear(); }
        return;
    }
    fifo.push_back(w);
    if (fifo.size() >= need) {
        exec_command();
        if (!polyline) { need = 0; fifo.clear(); }
    }
}

void Gpu::exec_command() {
    uint32_t cmd = fifo[0] >> 24;
    prim_count++;
    switch (cmd) {
    case 0x00: break;
    case 0x01: fifo.clear(); need = 0; break;
    case 0x02: fill_rect(); break;
    case 0x80: copy_rect(); break;
    case 0xA0: {
        cx = fifo[1] & 0x3FF; cy = (fifo[1] >> 16) & 0x1FF;
        cw = fifo[2] & 0x3FF; ch = (fifo[2] >> 16) & 0x1FF;
        if (!cw) cw = 1024;
        if (!ch) ch = 512;
        cpos = 0; cpu2vram = true; break; }
    case 0xC0: {
        rx = fifo[1] & 0x3FF; ry = (fifo[1] >> 16) & 0x1FF;
        rw = fifo[2] & 0x3FF; rh = (fifo[2] >> 16) & 0x1FF;
        if (!rw) rw = 1024;
        if (!rh) rh = 512;
        rpos = 0; vram2cpu = true; break; }
    case 0xE1: { uint32_t w = fifo[0]; tpx = w & 15; tpy = (w >> 4) & 1; semi_mode = (w >> 5) & 3; tex_depth = (w >> 7) & 3;
                 dither = (w >> 9) & 1; draw_to_display = (w >> 10) & 1; break; }
    case 0xE2: break;
    case 0xE3: clip_x0 = fifo[0] & 0x3FF; clip_y0 = (fifo[0] >> 10) & 0x1FF; break;
    case 0xE4: clip_x1 = fifo[0] & 0x3FF; clip_y1 = (fifo[0] >> 10) & 0x3FF; break;
    case 0xE5: off_x = sx11(fifo[0] & 0x7FF); off_y = sx11((fifo[0] >> 11) & 0x7FF); break;
    case 0xE6: set_mask = fifo[0] & 1; check_mask = (fifo[0] >> 1) & 1; break;
    default:
        if (cmd >= 0x20 && cmd < 0x40) {
            bool quad = cmd & 8, tex = cmd & 4, semi = cmd & 2, raw = cmd & 1, gour = cmd & 0x10;
            draw_poly(quad, gour, tex, semi, raw);
        } else if (cmd >= 0x40 && cmd < 0x60) {
            bool gour = cmd & 0x10, semi = cmd & 2; draw_line(gour, semi);
        } else if (cmd >= 0x60 && cmd < 0x80) {
            bool tex = cmd & 4, semi = cmd & 2, raw = cmd & 1; int sm = (cmd >> 3) & 3;
            draw_rect(sm, tex, semi, raw);
        }
        break;
    }
}

void Gpu::fill_rect() {
    uint32_t c = fifo[0]; int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    uint16_t px = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    int x = fifo[1] & 0x3F0, y = (fifo[1] >> 16) & 0x1FF;
    int w = (((fifo[2] & 0x3FF) + 0xF) & ~0xF), h = (fifo[2] >> 16) & 0x1FF;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) vram[((y + j) & 511) * 1024 + ((x + i) & 1023)] = px;
    if (scale > 1 && !hires_vram.empty()) {
        uint32_t rgba = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        int maxW = 1024 * scale, maxH = 512 * scale;
        int hx = x * scale, hy = y * scale, hw = w * scale, hh = h * scale;
        for (int j = 0; j < hh; j++) {
            int py = (hy + j) % maxH;
            uint32_t* row = hires_vram.data() + (size_t)py * maxW;
            if (hx + hw <= maxW) {
                std::fill_n(row + hx, hw, rgba);
            } else {
                for (int i = 0; i < hw; i++) row[(hx + i) % maxW] = rgba;
            }
        }
    }
}

void Gpu::copy_rect() {
    int sx = fifo[1] & 0x3FF, sy = (fifo[1] >> 16) & 0x1FF, dx = fifo[2] & 0x3FF, dy = (fifo[2] >> 16) & 0x1FF;
    int w = fifo[3] & 0x3FF, h = (fifo[3] >> 16) & 0x1FF; if (!w) w = 1024; if (!h) h = 512;
    std::vector<uint16_t> tmp((size_t)w * h);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) tmp[j * w + i] = vram[((sy + j) & 511) * 1024 + ((sx + i) & 1023)];
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) vram[((dy + j) & 511) * 1024 + ((dx + i) & 1023)] = tmp[j * w + i];

    if (scale > 1 && !hires_vram.empty()) {
        int maxW = 1024 * scale, maxH = 512 * scale;
        int hsx = sx * scale, hsy = sy * scale, hdx = dx * scale, hdy = dy * scale;
        int hw = w * scale, hh = h * scale;
        std::vector<uint32_t> htmp((size_t)hw * hh);
        for (int j = 0; j < hh; j++) for (int i = 0; i < hw; i++)
            htmp[j * hw + i] = hires_vram[((hsy + j) % maxH) * maxW + ((hsx + i) % maxW)];
        for (int j = 0; j < hh; j++) for (int i = 0; i < hw; i++)
            hires_vram[((hdy + j) % maxH) * maxW + ((hdx + i) % maxW)] = htmp[j * hw + i];
    }
}

void Gpu::put_pixel(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi) {
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1 || x < 0 || x > 1023 || y < 0 || y > 511) return;
    uint16_t& dst = vram[y * 1024 + x];
    if (check_mask && (dst & 0x8000)) return;
    if (semi && texsemi) {
        int br = (dst & 31) << 3, bg = ((dst >> 5) & 31) << 3, bb = ((dst >> 10) & 31) << 3;
        switch (smode) {
        case 0: r = (br + r) >> 1; g = (bg + g) >> 1; b = (bb + b) >> 1; break;
        case 1: r = br + r; g = bg + g; b = bb + b; break;
        case 2: r = br - r; g = bg - g; b = bb - b; break;
        default: r = br + (r >> 2); g = bg + (g >> 2); b = bb + (b >> 2); break;
        }
        r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    }
    dst = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (set_mask ? 0x8000 : 0));
}

static inline uint32_t blend_px(uint32_t dst, int r, int g, int b, int smode) {
    int br = dst & 255, bg = (dst >> 8) & 255, bb = (dst >> 16) & 255;
    switch (smode) {
    case 0: r = (br + r) >> 1; g = (bg + g) >> 1; b = (bb + b) >> 1; break;
    case 1: r = br + r; g = bg + g; b = bb + b; break;
    case 2: r = br - r; g = bg - g; b = bb - b; break;
    default: r = br + (r >> 2); g = bg + (g >> 2); b = bb + (b >> 2); break;
    }
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

void Gpu::put_pixel_hires(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi) {
    int maxW = 1024 * scale, maxH = 512 * scale;
    if (x < clip_x0 * scale || x > clip_x1 * scale || y < clip_y0 * scale || y > clip_y1 * scale ||
        x < 0 || x >= maxW || y < 0 || y >= maxH) return;

    uint32_t& dst = hires_vram[(size_t)y * maxW + x];
    if (semi && texsemi) {
        dst = blend_px(dst, r, g, b, smode);
    } else {
        dst = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }
}

uint16_t Gpu::fetch_tex(int u, int v, uint16_t clut, int px, int py, int depth) {
    int bx = px * 64, by = py * 256;
    u &= 255; v &= 255;
    if (depth == 0) {
        uint16_t t = vram[(by + v) * 1024 + ((bx + (u >> 2)) & 1023)];
        int idx = (t >> ((u & 3) * 4)) & 15;
        return vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + idx) & 1023)];
    } else if (depth == 1) {
        uint16_t t = vram[(by + v) * 1024 + ((bx + (u >> 1)) & 1023)];
        int idx = (t >> ((u & 1) * 8)) & 255;
        return vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + idx) & 1023)];
    }
    return vram[(by + v) * 1024 + ((bx + u) & 1023)];
}

struct TexSampler {
    const uint16_t* vram;
    const uint16_t* page;
    const uint16_t* clut_ptr;
    int bx;
    int cbx;
    int depth;
    bool bilinear;

    TexSampler(const uint16_t* v, uint16_t clut, int px, int py, int d, bool bil)
        : vram(v),
          page(v + (size_t)(py * 256) * 1024),
          clut_ptr(v + (size_t)((clut >> 6) & 511) * 1024),
          bx(px * 64),
          cbx((clut & 63) * 16),
          depth(d),
          bilinear(bil) {}

    inline uint16_t texel(int u, int v) const {
        u &= 255; v &= 255;
        if (depth == 0) {
            uint16_t t = page[v * 1024 + ((bx + (u >> 2)) & 1023)];
            int idx = (t >> ((u & 3) * 4)) & 15;
            return clut_ptr[(cbx + idx) & 1023];
        } else if (depth == 1) {
            uint16_t t = page[v * 1024 + ((bx + (u >> 1)) & 1023)];
            int idx = (t >> ((u & 1) * 8)) & 255;
            return clut_ptr[(cbx + idx) & 1023];
        }
        return page[v * 1024 + ((bx + u) & 1023)];
    }
};

void Gpu::raster_tri_hires(const V& a0, const V& b0, const V& d0, bool gouraud, bool textured, bool semi, bool raw,
                           uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi) {
    const int s = scale;
    const int maxW = 1024 * s, maxH = 512 * s;
    V a = a0, b = b0, d = d0;
    a.x *= s; a.y *= s;
    b.x *= s; b.y *= s;
    d.x *= s; d.y *= s;

    long long area = (long long)(b.x - a.x) * (d.y - a.y) - (long long)(b.y - a.y) * (d.x - a.x);
    if (area == 0) return;
    if (area < 0) { std::swap(b, d); area = -area; }

    int minx = std::max({std::min({a.x, b.x, d.x}), clip_x0 * s, 0});
    int maxx = std::min({std::max({a.x, b.x, d.x}), (clip_x1 + 1) * s - 1, maxW - 1});
    int miny = std::max({std::min({a.y, b.y, d.y}), clip_y0 * s, 0});
    int maxy = std::min({std::max({a.y, b.y, d.y}), (clip_y1 + 1) * s - 1, maxH - 1});
    if (minx > maxx || miny > maxy) return;

    auto bias = [](const V& p, const V& q) {
        int dy = q.y - p.y, dx = q.x - p.x;
        return (dy > 0 || (dy == 0 && dx < 0)) ? 0 : -1;
    };
    const int b2b = bias(a, b), b0b = bias(b, d), b1b = bias(d, a);
    const long long dx2 = -(long long)(b.y - a.y), dx0 = -(long long)(d.y - b.y), dx1 = -(long long)(a.y - d.y);
    const float invArea = 1.0f / (float)area;
    const TexSampler ts(vram.data(), clut, ctpx, ctpy, cdepth, bilinear_tex);
    uint32_t* out = hires_vram.data();

    // Derivate spaziali costanti per tutta la superficie del triangolo (DDA)
    const float y_bd = (float)(b.y - d.y), y_da = (float)(d.y - a.y), y_ab = (float)(a.y - b.y);
    float du_dx = 0.0f, dv_dx = 0.0f;
    if (textured) {
        du_dx = ((float)a.u * y_bd + (float)b.u * y_da + (float)d.u * y_ab) * invArea;
        dv_dx = ((float)a.v * y_bd + (float)b.v * y_da + (float)d.v * y_ab) * invArea;
    }

    float dr_dx = 0.0f, dg_dx = 0.0f, db_dx = 0.0f;
    if (gouraud) {
        dr_dx = ((float)a.r * y_bd + (float)b.r * y_da + (float)d.r * y_ab) * invArea;
        dg_dx = ((float)a.g * y_bd + (float)b.g * y_da + (float)d.g * y_ab) * invArea;
        db_dx = ((float)a.b * y_bd + (float)b.b * y_da + (float)d.b * y_ab) * invArea;
    }

    for (int y = miny; y <= maxy; y++) {
        long long w2 = (long long)(b.x - a.x) * (y - a.y) - (long long)(b.y - a.y) * (minx - a.x);
        long long w0 = (long long)(d.x - b.x) * (y - b.y) - (long long)(d.y - b.y) * (minx - b.x);
        long long w1 = (long long)(a.x - d.x) * (y - d.y) - (long long)(a.y - d.y) * (minx - d.x);

        float fa = (float)w0 * invArea, fb = (float)w1 * invArea, fc = (float)w2 * invArea;
        bool entered = false;
        uint32_t* row = out + (size_t)y * maxW;

        if (!textured) {
            if (!gouraud) {
                uint32_t col = 0xFF000000u | ((uint32_t)a.b << 16) | ((uint32_t)a.g << 8) | (uint32_t)a.r;
                for (int x = minx; x <= maxx; x++, w2 += dx2, w0 += dx0, w1 += dx1) {
                    if (w2 + b2b < 0 || w0 + b0b < 0 || w1 + b1b < 0) { if (entered) break; continue; }
                    entered = true;
                    if (semi) row[x] = blend_px(row[x], a.r, a.g, a.b, csemi);
                    else row[x] = col;
                }
            } else {
                float cur_r = (float)a.r * fa + (float)b.r * fb + (float)d.r * fc;
                float cur_g = (float)a.g * fa + (float)b.g * fb + (float)d.g * fc;
                float cur_b = (float)a.b * fa + (float)b.b * fb + (float)d.b * fc;
                for (int x = minx; x <= maxx; x++, w2 += dx2, w0 += dx0, w1 += dx1,
                                                   cur_r += dr_dx, cur_g += dg_dx, cur_b += db_dx) {
                    if (w2 + b2b < 0 || w0 + b0b < 0 || w1 + b1b < 0) { if (entered) break; continue; }
                    entered = true;
                    int r = std::clamp((int)cur_r, 0, 255);
                    int g = std::clamp((int)cur_g, 0, 255);
                    int bl = std::clamp((int)cur_b, 0, 255);
                    if (semi) row[x] = blend_px(row[x], r, g, bl, csemi);
                    else row[x] = 0xFF000000u | ((uint32_t)bl << 16) | ((uint32_t)g << 8) | (uint32_t)r;
                }
            }
            continue;
        }

        // Triangolo texturato (DDA incremental stepping)
        float cur_u = (float)a.u * fa + (float)b.u * fb + (float)d.u * fc;
        float cur_v = (float)a.v * fa + (float)b.v * fb + (float)d.v * fc;
        float cur_r = 0.0f, cur_g = 0.0f, cur_b = 0.0f;
        if (gouraud) {
            cur_r = (float)a.r * fa + (float)b.r * fb + (float)d.r * fc;
            cur_g = (float)a.g * fa + (float)b.g * fb + (float)d.g * fc;
            cur_b = (float)a.b * fa + (float)b.b * fb + (float)d.b * fc;
        }

        if (ts.bilinear) {
            for (int x = minx; x <= maxx; x++, w2 += dx2, w0 += dx0, w1 += dx1,
                                               cur_u += du_dx, cur_v += dv_dx,
                                               cur_r += dr_dx, cur_g += dg_dx, cur_b += db_dx) {
                if (w2 + b2b < 0 || w0 + b0b < 0 || w1 + b1b < 0) { if (entered) break; continue; }
                entered = true;
                float uf = cur_u, vf = cur_v;
                int u0 = (int)uf; if (uf < 0.0f) u0 = (int)std::floor(uf);
                int v0 = (int)vf; if (vf < 0.0f) v0 = (int)std::floor(vf);
                uint16_t t = ts.texel(u0, v0);
                if (t == 0) continue;
                int fu = (int)((uf - (float)u0) * 256.0f), fv = (int)((vf - (float)v0) * 256.0f);
                uint16_t t10 = ts.texel(u0 + 1, v0), t01 = ts.texel(u0, v0 + 1), t11 = ts.texel(u0 + 1, v0 + 1);
                if (t10 == 0) t10 = t; if (t01 == 0) t01 = t; if (t11 == 0) t11 = t;
                int tr, tg, tb;
                if (t10 == t && t01 == t && t11 == t) {
                    tr = (t & 31) << 3; tg = ((t >> 5) & 31) << 3; tb = ((t >> 10) & 31) << 3;
                } else {
                    int w00 = (256 - fu) * (256 - fv), w10 = fu * (256 - fv), w01 = (256 - fu) * fv, w11 = fu * fv;
                    tr = (((t & 31) * w00 + (t10 & 31) * w10 + (t01 & 31) * w01 + (t11 & 31) * w11) >> 13);
                    tg = ((((t >> 5) & 31) * w00 + ((t10 >> 5) & 31) * w10 + ((t01 >> 5) & 31) * w01 + ((t11 >> 5) & 31) * w11) >> 13);
                    tb = ((((t >> 10) & 31) * w00 + ((t10 >> 10) & 31) * w10 + ((t01 >> 10) & 31) * w01 + ((t11 >> 10) & 31) * w11) >> 13);
                }
                int pr, pg, pbl;
                if (raw) {
                    pr = tr; pg = tg; pbl = tb;
                } else {
                    int mr = gouraud ? (int)cur_r : a.r;
                    int mg = gouraud ? (int)cur_g : a.g;
                    int mb = gouraud ? (int)cur_b : a.b;
                    pr = std::min(255, (tr * mr) >> 7);
                    pg = std::min(255, (tg * mg) >> 7);
                    pbl = std::min(255, (tb * mb) >> 7);
                }
                bool texsemi = (t & 0x8000) != 0;
                if (semi && texsemi) row[x] = blend_px(row[x], pr, pg, pbl, csemi);
                else row[x] = 0xFF000000u | ((uint32_t)pbl << 16) | ((uint32_t)pg << 8) | (uint32_t)pr;
            }
        } else {
            // Retro Point sampling veloce (default PS1 autentico)
            for (int x = minx; x <= maxx; x++, w2 += dx2, w0 += dx0, w1 += dx1,
                                               cur_u += du_dx, cur_v += dv_dx,
                                               cur_r += dr_dx, cur_g += dg_dx, cur_b += db_dx) {
                if (w2 + b2b < 0 || w0 + b0b < 0 || w1 + b1b < 0) { if (entered) break; continue; }
                entered = true;
                int u0 = (int)cur_u; if (cur_u < 0.0f) u0 = (int)std::floor(cur_u);
                int v0 = (int)cur_v; if (cur_v < 0.0f) v0 = (int)std::floor(cur_v);
                uint16_t t = ts.texel(u0, v0);
                if (t == 0) continue;
                int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
                int pr, pg, pbl;
                if (raw) {
                    pr = tr; pg = tg; pbl = tb;
                } else {
                    int mr = gouraud ? (int)cur_r : a.r;
                    int mg = gouraud ? (int)cur_g : a.g;
                    int mb = gouraud ? (int)cur_b : a.b;
                    pr = std::min(255, (tr * mr) >> 7);
                    pg = std::min(255, (tg * mg) >> 7);
                    pbl = std::min(255, (tb * mb) >> 7);
                }
                bool texsemi = (t & 0x8000) != 0;
                if (semi && texsemi) row[x] = blend_px(row[x], pr, pg, pbl, csemi);
                else row[x] = 0xFF000000u | ((uint32_t)pbl << 16) | ((uint32_t)pg << 8) | (uint32_t)pr;
            }
        }
    }
}

void Gpu::raster_tri(const V& a0, const V& b0, const V& c0, bool gouraud, bool textured, bool semi, bool raw,
                     uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi) {
    V a = a0, b = b0, c = c0;
    auto edge = [](const V& p, const V& q, int x, int y) { return (long long)(q.x - p.x) * (y - p.y) - (long long)(q.y - p.y) * (x - p.x); };
    long long area = edge(a, b, c.x, c.y);
    if (area == 0) return;
    if (area < 0) { std::swap(b, c); area = -area; }
    int minx = std::max({std::min({a.x, b.x, c.x}), clip_x0, 0}), maxx = std::min({std::max({a.x, b.x, c.x}), clip_x1, 1023});
    int miny = std::max({std::min({a.y, b.y, c.y}), clip_y0, 0}), maxy = std::min({std::max({a.y, b.y, c.y}), clip_y1, 511});
    if (std::max({a.x, b.x, c.x}) - std::min({a.x, b.x, c.x}) > 1023 || std::max({a.y, b.y, c.y}) - std::min({a.y, b.y, c.y}) > 511) return;
    auto bias = [](const V& p, const V& q) { int dy = q.y - p.y, dx = q.x - p.x; return (dy > 0 || (dy == 0 && dx < 0)) ? 0 : -1; };
    int b0b = bias(a, b), b1b = bias(b, c), b2b = bias(c, a);
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        long long w2 = edge(a, b, x, y), w0 = edge(b, c, x, y), w1 = edge(c, a, x, y);
        if (w2 + b0b < 0 || w0 + b1b < 0 || w1 + b2b < 0) continue;
        float fa = (float)w0 / area, fb = (float)w1 / area, fc = (float)w2 / area;
        int r, g, bl;
        if (gouraud) { r = (int)(a.r * fa + b.r * fb + c.r * fc); g = (int)(a.g * fa + b.g * fb + c.g * fc); bl = (int)(a.b * fa + b.b * fb + c.b * fc); }
        else { r = a.r; g = a.g; bl = a.b; }
        bool texsemi = true;
        if (textured) {
            int u = (int)(a.u * fa + b.u * fb + c.u * fc + 0.5f), v = (int)(a.v * fa + b.v * fb + c.v * fc + 0.5f);
            uint16_t t = fetch_tex(u, v, clut, ctpx, ctpy, cdepth);
            if (t == 0) continue;
            int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
            if (!raw) { tr = std::min(255, (tr * r) >> 7); tg = std::min(255, (tg * g) >> 7); tb = std::min(255, (tb * bl) >> 7); }
            r = tr; g = tg; bl = tb; texsemi = (t & 0x8000) != 0;
        }
        put_pixel(x, y, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255), semi, csemi, textured ? texsemi : true);
    }
}

void Gpu::draw_poly(bool quad, bool gouraud, bool textured, bool semi, bool raw) {
    int n = quad ? 4 : 3; V v[4]; int idx = 0; uint16_t clut = 0; int ctpx = tpx, ctpy = tpy, cdepth = tex_depth, csemi = semi_mode;
    for (int i = 0; i < n; i++) {
        uint32_t col = (i == 0 || !gouraud) ? fifo[0] : fifo[idx++];
        if (i == 0) idx = 1;
        uint32_t vw = fifo[idx++];
        v[i].r = col & 255; v[i].g = (col >> 8) & 255; v[i].b = (col >> 16) & 255;
        if (!gouraud) { uint32_t c0 = fifo[0]; v[i].r = c0 & 255; v[i].g = (c0 >> 8) & 255; v[i].b = (c0 >> 16) & 255; }
        v[i].x = sx11(vw & 0x7FF) + off_x; v[i].y = sx11((vw >> 16) & 0x7FF) + off_y;
        if (textured) {
            uint32_t uv = fifo[idx++]; v[i].u = uv & 255; v[i].v = (uv >> 8) & 255;
            if (i == 0) clut = (uint16_t)(uv >> 16);
            if (i == 1) { uint32_t tp = uv >> 16; ctpx = tp & 15; ctpy = (tp >> 4) & 1; csemi = (tp >> 5) & 3; cdepth = (tp >> 7) & 3;
                          tpx = ctpx; tpy = ctpy; semi_mode = csemi; tex_depth = cdepth; }
        }
    }
    raster_tri(v[0], v[1], v[2], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    if (quad) raster_tri(v[1], v[2], v[3], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    if (scale > 1 && !hires_vram.empty()) {
        raster_tri_hires(v[0], v[1], v[2], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
        if (quad) raster_tri_hires(v[1], v[2], v[3], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    }
}

void Gpu::draw_rect(int sizeMode, bool textured, bool semi, bool raw) {
    uint32_t col = fifo[0]; int r = col & 255, g = (col >> 8) & 255, b = (col >> 16) & 255;
    uint32_t vw = fifo[1]; int x = sx11(vw & 0x7FF) + off_x, y = sx11((vw >> 16) & 0x7FF) + off_y;
    int i = 2; int u0 = 0, v0 = 0; uint16_t clut = 0;
    if (textured) { uint32_t uv = fifo[i++]; u0 = uv & 255; v0 = (uv >> 8) & 255; clut = (uint16_t)(uv >> 16); }
    int w, h;
    if (sizeMode == 0) { uint32_t s = fifo[i++]; w = s & 0x3FF; h = (s >> 16) & 0x1FF; }
    else if (sizeMode == 1) w = h = 1; else if (sizeMode == 2) w = h = 8; else w = h = 16;
    for (int j = 0; j < h; j++) for (int k = 0; k < w; k++) {
        int pr = r, pg = g, pb = b; bool texsemi = true;
        if (textured) {
            uint16_t t = fetch_tex(u0 + k, v0 + j, clut, tpx, tpy, tex_depth);
            if (t == 0) continue;
            int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
            if (!raw) { tr = std::min(255, (tr * r) >> 7); tg = std::min(255, (tg * g) >> 7); tb = std::min(255, (tb * b) >> 7); }
            pr = tr; pg = tg; pb = tb; texsemi = (t & 0x8000) != 0;
        }
        put_pixel(x + k, y + j, pr, pg, pb, semi, semi_mode, texsemi);
        if (scale > 1 && !hires_vram.empty()) {
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    put_pixel_hires((x + k) * scale + dx, (y + j) * scale + dy, pr, pg, pb, semi, semi_mode, texsemi);
                }
            }
        }
    }
}

void Gpu::draw_line(bool gouraud, bool semi) {
    size_t i = 1; uint32_t c0 = fifo[0];
    auto vtx = [&](uint32_t w, int& x, int& y) { x = sx11(w & 0x7FF) + off_x; y = sx11((w >> 16) & 0x7FF) + off_y; };
    int x0, y0; vtx(fifo[i++], x0, y0); uint32_t col0 = c0;
    while (i < fifo.size()) {
        uint32_t col1 = col0; if (gouraud) col1 = fifo[i++];
        if (i >= fifo.size()) break;
        int x1, y1; vtx(fifo[i++], x1, y1);
        int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), steps = std::max(dx, dy);
        for (int s = 0; s <= steps; s++) {
            float t = steps ? (float)s / steps : 0;
            int x = x0 + (int)((x1 - x0) * t + (x1 >= x0 ? 0.5f : -0.5f)), y = y0 + (int)((y1 - y0) * t + (y1 >= y0 ? 0.5f : -0.5f));
            int r = (int)((col0 & 255) * (1 - t) + (col1 & 255) * t), g = (int)(((col0 >> 8) & 255) * (1 - t) + ((col1 >> 8) & 255) * t), b = (int)(((col0 >> 16) & 255) * (1 - t) + ((col1 >> 16) & 255) * t);
            put_pixel(x, y, r, g, b, semi, semi_mode, true);
            if (scale > 1 && !hires_vram.empty()) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        put_pixel_hires(x * scale + dx, y * scale + dy, r, g, b, semi, semi_mode, true);
                    }
                }
            }
        }
        x0 = x1; y0 = y1; col0 = col1;
    }
}

void Gpu::dma_linked_list(uint8_t* ram, uint32_t mask, uint32_t addr) {
    uint32_t node = addr & mask & ~3u; int guard = 0;
    while (guard++ < 200000) {
        uint32_t hdr = rd32(ram, mask, node);
        uint32_t cnt = hdr >> 24;
        for (uint32_t i = 0; i < cnt; i++) gp0(rd32(ram, mask, node + 4 + i * 4));
        uint32_t next = hdr & 0xFFFFFF;
        if (next == 0xFFFFFF) break;
        node = next & mask & ~3u;
    }
}

void Gpu::dma_block(uint8_t* ram, uint32_t mask, uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) gp0(rd32(ram, mask, addr + i * 4));
}

} // namespace cb

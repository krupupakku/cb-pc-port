// =============================================================================
// Crash Bash: Remaster+ — Dynamic On-Screen Button Prompts Overlay
// Implementazione OpenGL del rendering ad alta risoluzione dei glifi
// =============================================================================

#include "renderer/prompt_overlay.h"
#include "renderer/renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#include "stb_image.h"

namespace cb::ui {

static PromptDevice g_configDevice = PromptDevice::Auto;
static PromptDevice g_activeDevice = PromptDevice::Xbox;
static bool         g_showOverlay  = false; // Disattivato di default per non sporcare lo schermo
static GLuint       g_fontBase     = 0;
static HFONT        g_hFont        = nullptr;

struct CpuImage {
    int w = 0, h = 0;
    std::vector<uint32_t> pixels;
};

// Buffers CPU in memoria per il campionamento istantaneo in-game (0=Cross/A, 1=Square/X, 2=Triangle/Y, 3=Circle/B)
static CpuImage g_imgXbox[4] = {};
static CpuImage g_imgPs[4]   = {};
static CpuImage g_imgKb[4]   = {};

// Texture IDs per i prompt Xbox
static GLuint g_texXboxA      = 0;
static GLuint g_texXboxB      = 0;
static GLuint g_texXboxX      = 0;
static GLuint g_texXboxY      = 0;
static GLuint g_texXboxStickL = 0;
static GLuint g_texXboxDpad   = 0;

// Texture IDs per i prompt PlayStation
static GLuint g_texPsCross    = 0;
static GLuint g_texPsCircle   = 0;
static GLuint g_texPsSquare   = 0;
static GLuint g_texPsTriangle = 0;
static GLuint g_texPsStickL   = 0;
static GLuint g_texPsDpad     = 0;

// Texture IDs per i prompt Tastiera
static GLuint g_texKbSpace    = 0;
static GLuint g_texKbEnter    = 0;
static GLuint g_texKbEsc      = 0;
static GLuint g_texKbX        = 0;
static GLuint g_texKbC        = 0;
static GLuint g_texKbV        = 0;
static GLuint g_texKbW        = 0;
static GLuint g_texKbArrows   = 0;

static std::string FindAssetPath(const std::string& relPath) {
    const std::string prefixes[] = {
        "",
        "../",
        "../../",
        "remaster/",
        "../remaster/",
        "c:/Users/marco/Documents/Developer/cb/",
        "c:/Users/marco/Documents/Developer/cb/remaster/"
    };
    for (const auto& pre : prefixes) {
        std::string candidate = pre + relPath;
        FILE* f = std::fopen(candidate.c_str(), "rb");
        if (f) {
            std::fclose(f);
            return candidate;
        }
    }
    return relPath;
}

static GLuint LoadPngTexture(const std::string& relPath, CpuImage* cpuOut = nullptr) {
    std::string fullPath = FindAssetPath(relPath);
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::printf("[UI Prompts] Fallito caricamento texture: %s\n", fullPath.c_str());
        return 0;
    }

    if (cpuOut) {
        cpuOut->w = w;
        cpuOut->h = h;
        cpuOut->pixels.assign((const uint32_t*)data, (const uint32_t*)data + (size_t)w * h);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return tex;
}

static void InitFont(HDC hdc) {
    if (g_fontBase != 0) return;
    g_fontBase = glGenLists(96);
    g_hFont = CreateFontA(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
    if (g_hFont && hdc) {
        SelectObject(hdc, g_hFont);
        wglUseFontBitmapsA(hdc, 32, 96, g_fontBase);
    }
}

void Initialize() {
    std::printf("[UI Prompts] Inizializzazione texture prompt ad alta risoluzione...\n");

    // 1. Carica glifi Xbox (0: Cross -> A, 1: Square -> X, 2: Triangle -> Y, 3: Circle -> B)
    g_texXboxA      = LoadPngTexture("assets_hd/ui/xbox/xbox_button_color_a.png", &g_imgXbox[0]);
    g_texXboxB      = LoadPngTexture("assets_hd/ui/xbox/xbox_button_color_b.png", &g_imgXbox[3]);
    g_texXboxX      = LoadPngTexture("assets_hd/ui/xbox/xbox_button_color_x.png", &g_imgXbox[1]);
    g_texXboxY      = LoadPngTexture("assets_hd/ui/xbox/xbox_button_color_y.png", &g_imgXbox[2]);
    g_texXboxStickL = LoadPngTexture("assets_hd/ui/xbox/xbox_stick_l.png");
    g_texXboxDpad   = LoadPngTexture("assets_hd/ui/xbox/xbox_dpad.png");

    // 2. Carica glifi PlayStation (0: Cross, 1: Square, 2: Triangle, 3: Circle)
    g_texPsCross    = LoadPngTexture("assets_hd/ui/playstation/playstation_button_color_cross.png", &g_imgPs[0]);
    g_texPsCircle   = LoadPngTexture("assets_hd/ui/playstation/playstation_button_color_circle.png", &g_imgPs[3]);
    g_texPsSquare   = LoadPngTexture("assets_hd/ui/playstation/playstation_button_color_square.png", &g_imgPs[1]);
    g_texPsTriangle = LoadPngTexture("assets_hd/ui/playstation/playstation_button_color_triangle.png", &g_imgPs[2]);
    g_texPsStickL   = LoadPngTexture("assets_hd/ui/playstation/playstation_stick_l.png");
    g_texPsDpad     = LoadPngTexture("assets_hd/ui/playstation/playstation_dpad.png");

    // 3. Carica glifi Tastiera (0: Space, 1: X, 2: V, 3: C)
    g_texKbSpace    = LoadPngTexture("assets_hd/ui/keyboard/keyboard_space_icon.png", &g_imgKb[0]);
    g_texKbEnter    = LoadPngTexture("assets_hd/ui/keyboard/keyboard_enter.png");
    g_texKbEsc      = LoadPngTexture("assets_hd/ui/keyboard/keyboard_escape.png");
    g_texKbX        = LoadPngTexture("assets_hd/ui/keyboard/keyboard_x.png", &g_imgKb[1]);
    g_texKbC        = LoadPngTexture("assets_hd/ui/keyboard/keyboard_c.png", &g_imgKb[3]);
    g_texKbV        = LoadPngTexture("assets_hd/ui/keyboard/keyboard_v.png", &g_imgKb[2]);
    g_texKbW        = LoadPngTexture("assets_hd/ui/keyboard/keyboard_w.png");
    g_texKbArrows   = LoadPngTexture("assets_hd/ui/keyboard/keyboard_arrows_all.png");

    std::printf("[UI Prompts] Asset HD caricati con successo. Modalita corrente: %s\n", GetPromptDeviceName());
}

bool SampleButtonGlyph(int btnIndex, float nu, float nv, uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA) {
    if (btnIndex < 0 || btnIndex > 3) return false;
    if (nu < 0.0f || nu > 1.0f || nv < 0.0f || nv > 1.0f) return false;

    PromptDevice eff = GetEffectivePromptDevice();
    const CpuImage* img = nullptr;
    if (eff == PromptDevice::Xbox) {
        img = &g_imgXbox[btnIndex];
    } else if (eff == PromptDevice::Keyboard) {
        img = &g_imgKb[btnIndex];
    } else {
        img = &g_imgPs[btnIndex];
    }

    if (!img || img->pixels.empty() || img->w <= 0 || img->h <= 0) return false;

    int tx = std::clamp((int)(nu * (float)img->w), 0, img->w - 1);
    int ty = std::clamp((int)(nv * (float)img->h), 0, img->h - 1);
    uint32_t p = img->pixels[(size_t)ty * img->w + tx];

    outR = p & 0xFF;
    outG = (p >> 8) & 0xFF;
    outB = (p >> 16) & 0xFF;
    outA = (p >> 24) & 0xFF;
    return true;
}

void Shutdown() {
    auto FreeTex = [](GLuint& t) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    };
    FreeTex(g_texXboxA); FreeTex(g_texXboxB); FreeTex(g_texXboxX); FreeTex(g_texXboxY);
    FreeTex(g_texXboxStickL); FreeTex(g_texXboxDpad);

    FreeTex(g_texPsCross); FreeTex(g_texPsCircle); FreeTex(g_texPsSquare); FreeTex(g_texPsTriangle);
    FreeTex(g_texPsStickL); FreeTex(g_texPsDpad);

    FreeTex(g_texKbSpace); FreeTex(g_texKbEnter); FreeTex(g_texKbEsc);
    FreeTex(g_texKbX); FreeTex(g_texKbC); FreeTex(g_texKbV); FreeTex(g_texKbW);
    FreeTex(g_texKbArrows);

    if (g_fontBase) {
        glDeleteLists(g_fontBase, 96);
        g_fontBase = 0;
    }
    if (g_hFont) {
        DeleteObject(g_hFont);
        g_hFont = nullptr;
    }
}

void SetPromptDevice(PromptDevice dev) {
    g_configDevice = dev;
    if (dev != PromptDevice::Auto) {
        g_activeDevice = dev;
    }
}

PromptDevice GetConfiguredPromptDevice() {
    return g_configDevice;
}

PromptDevice GetEffectivePromptDevice() {
    if (g_configDevice == PromptDevice::Auto) {
        return g_activeDevice;
    }
    return g_configDevice;
}

void CyclePromptDevice() {
    StepPromptDevice(1);
}

void StepPromptDevice(int dir) {
    int idx = 0;
    if (g_configDevice == PromptDevice::Xbox)        idx = 1;
    else if (g_configDevice == PromptDevice::PlayStation) idx = 2;
    else if (g_configDevice == PromptDevice::Keyboard)    idx = 3;

    idx = (idx + dir + 4) % 4;
    if (idx == 0)      g_configDevice = PromptDevice::Auto;
    else if (idx == 1) g_configDevice = PromptDevice::Xbox;
    else if (idx == 2) g_configDevice = PromptDevice::PlayStation;
    else               g_configDevice = PromptDevice::Keyboard;

    if (g_configDevice != PromptDevice::Auto) {
        g_activeDevice = g_configDevice;
    }
    std::printf("[UI Prompts] Stile tasti a schermo: %s\n", GetPromptDeviceName());
    cb::renderer::SaveConfig();
}

const char* GetPromptDeviceName() {
    if (g_configDevice == PromptDevice::Auto) {
        if (g_activeDevice == PromptDevice::Xbox)        return "Auto (Xbox)";
        if (g_activeDevice == PromptDevice::PlayStation) return "Auto (PlayStation)";
        return "Auto (Tastiera)";
    }
    if (g_configDevice == PromptDevice::Xbox)        return "Xbox Series/One";
    if (g_configDevice == PromptDevice::PlayStation) return "PlayStation (DualShock)";
    return "Tastiera PC";
}

const char* GetPromptDeviceMenuLabel() {
    if (g_configDevice == PromptDevice::Auto)        return "< AUTO >";
    if (g_configDevice == PromptDevice::Xbox)        return "< XBOX >";
    if (g_configDevice == PromptDevice::PlayStation) return "< PS1 >";
    return "< KEYS >";
}

void TogglePromptOverlay() {
    g_showOverlay = !g_showOverlay;
    std::printf("[UI Prompts] Barra prompt HUD: %s\n", g_showOverlay ? "ATTIVA" : "DISATTIVA");
}

bool IsPromptOverlayVisible() {
    return g_showOverlay;
}

void NotifyInputActivity(bool fromGamepad, bool fromKeyboard) {
    if (g_configDevice != PromptDevice::Auto) return;
    if (fromGamepad) {
        g_activeDevice = PromptDevice::Xbox;
    } else if (fromKeyboard) {
        g_activeDevice = PromptDevice::Keyboard;
    }
}

static void DrawTexturedQuad(GLuint tex, float x, float y, float w, float h, float alpha = 1.0f) {
    if (!tex) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void DrawRoundedPill(float x, float y, float w, float h, float r, float g, float b, float a) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    // Bordo sottile di rilievo
    glColor4f(r + 0.2f, g + 0.2f, b + 0.25f, a * 0.8f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void DrawGlString(const char* str, float x, float y, float r, float g, float b, float a) {
    if (!g_fontBase || !str || !*str) return;
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    glListBase(g_fontBase - 32);
    glCallLists((GLsizei)std::strlen(str), GL_UNSIGNED_BYTE, str);
}

struct PromptItem {
    GLuint      iconTex;
    const char* label;
    float       iconW;
};

void Render(int windowWidth, int windowHeight, float uiScale) {
    if (!g_showOverlay || windowWidth <= 0 || windowHeight <= 0) return;
    (void)uiScale;

    HDC hdc = wglGetCurrentDC();
    InitFont(hdc);

    // Imposta coordinate ortografiche pixel-perfect (0,0 in alto a sinistra)
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)windowWidth, (double)windowHeight, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    PromptDevice eff = GetEffectivePromptDevice();

    // Prepariamo la lista dei tasti contestuali attuali di Crash Bash
    std::vector<PromptItem> items;
    if (eff == PromptDevice::Xbox) {
        items.push_back({ g_texXboxA,      "Salto / OK",       28.0f });
        items.push_back({ g_texXboxB,      "Giro / Esci",      28.0f });
        items.push_back({ g_texXboxX,      "Attacco / Spinta", 28.0f });
        items.push_back({ g_texXboxY,      "Controlli",        28.0f });
        items.push_back({ g_texXboxStickL, "Muovi",            28.0f });
    } else if (eff == PromptDevice::PlayStation) {
        items.push_back({ g_texPsCross,    "Salto / OK",       28.0f });
        items.push_back({ g_texPsCircle,   "Giro / Esci",      28.0f });
        items.push_back({ g_texPsSquare,   "Attacco / Spinta", 28.0f });
        items.push_back({ g_texPsTriangle, "Controlli",        28.0f });
        items.push_back({ g_texPsStickL,   "Muovi",            28.0f });
    } else {
        items.push_back({ g_texKbSpace,    "Salto (Spazio)",   42.0f });
        items.push_back({ g_texKbC,        "Giro (C)",         28.0f });
        items.push_back({ g_texKbX        ,"Attacco (X)",      28.0f });
        items.push_back({ g_texKbV,        "Controlli (V)",    28.0f });
        items.push_back({ g_texKbW,        "Muovi (WASD)",     28.0f });
    }

    // Calcolo dimensioni barra prompt
    const float padX = 14.0f;
    const float itemSpacing = 18.0f;
    const float barH = 38.0f;

    float totalW = padX * 2.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        float textW = (float)std::strlen(items[i].label) * 8.5f;
        totalW += items[i].iconW + 6.0f + textW;
        if (i + 1 < items.size()) totalW += itemSpacing;
    }

    float barX = ((float)windowWidth - totalW) * 0.5f;
    float barY = (float)windowHeight - barH - 12.0f;

    // Disegna sfondo frosted pill
    DrawRoundedPill(barX, barY, totalW, barH, 0.05f, 0.07f, 0.12f, 0.85f);

    // Disegna icone e testi
    float curX = barX + padX;
    float iconY = barY + (barH - 28.0f) * 0.5f;
    float textY = barY + barH * 0.5f + 5.0f;

    for (const auto& item : items) {
        DrawTexturedQuad(item.iconTex, curX, iconY, item.iconW, 28.0f);
        curX += item.iconW + 6.0f;

        DrawGlString(item.label, curX, textY, 0.95f, 0.95f, 0.98f, 1.0f);
        curX += (float)std::strlen(item.label) * 8.5f + itemSpacing;
    }

    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

} // namespace cb::ui

#else

namespace cb::ui {
void Initialize() {}
void Shutdown() {}
void SetPromptDevice(PromptDevice) {}
PromptDevice GetConfiguredPromptDevice() { return PromptDevice::Auto; }
PromptDevice GetEffectivePromptDevice() { return PromptDevice::Auto; }
void CyclePromptDevice() {}
void StepPromptDevice(int) {}
const char* GetPromptDeviceName() { return "None"; }
const char* GetPromptDeviceMenuLabel() { return "< AUTO >"; }
void TogglePromptOverlay() {}
bool IsPromptOverlayVisible() { return false; }
void NotifyInputActivity(bool, bool) {}
void Render(int, int, float) {}
}

#endif

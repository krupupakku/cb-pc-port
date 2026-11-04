#include "renderer/renderer.h"
#include "input/input_manager.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace cb::renderer {

static uint32_t g_internalWidth  = 3840;
static uint32_t g_internalHeight = 2160;
static int      g_winWidth       = 1280;
static int      g_winHeight      = 720;
static bool     g_widescreen     = true;
static bool     g_running        = false;

#ifdef _WIN32
static HWND   g_hwnd  = nullptr;
static HDC    g_hdc   = nullptr;
static HGLRC  g_hglrc = nullptr;
static GLuint g_logoTex = 0;
static int    g_logoW   = 0;
static int    g_logoH   = 0;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_SIZE:
            g_winWidth  = LOWORD(lParam);
            g_winHeight = HIWORD(lParam);
            if (g_hglrc && g_winWidth > 0 && g_winHeight > 0) {
                glViewport(0, 0, g_winWidth, g_winHeight);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static void LoadLogoTexture() {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "cb.png",
        "../cb.png",
        "../../cb.png"
    };

    std::string foundPath;
    for (const char* c : candidates) {
        if (fs::exists(c)) {
            foundPath = c;
            break;
        }
    }

    if (foundPath.empty()) {
        std::printf("[Renderer] File cb.png non trovato nei percorsi relativi.\n");
        return;
    }

    int channels = 0;
    unsigned char* data = stbi_load(foundPath.c_str(), &g_logoW, &g_logoH, &channels, 4);
    if (!data) {
        std::printf("[Renderer] Impossibile decodificare %s\n", foundPath.c_str());
        return;
    }

    glGenTextures(1, &g_logoTex);
    glBindTexture(GL_TEXTURE_2D, g_logoTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_logoW, g_logoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    std::printf("[Renderer] Caricato logo '%s' (%dx%d RGBA)\n", foundPath.c_str(), g_logoW, g_logoH);
}
#endif

bool Initialize(uint32_t internalWidth, uint32_t internalHeight, bool widescreen) {
    g_internalWidth  = internalWidth;
    g_internalHeight = internalHeight;
    g_widescreen     = widescreen;

#ifdef _WIN32
    HINSTANCE hInstance = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "CrashBashRemasterWndClass";
    RegisterClassA(&wc);

    RECT rect = { 0, 0, g_winWidth, g_winHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "Crash Bash: Remaster+ | 4K Pipeline | XInput / SteamInput (4P) [Premi ESC per uscire]",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwnd) {
        std::printf("[Renderer] ERRORE: Creazione finestra Win32 fallita.\n");
        return false;
    }

    g_hdc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(g_hdc, &pfd);
    SetPixelFormat(g_hdc, pf, &pfd);

    g_hglrc = wglCreateContext(g_hdc);
    wglMakeCurrent(g_hdc, g_hglrc);

    glViewport(0, 0, g_winWidth, g_winHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    LoadLogoTexture();
    g_running = true;
#endif

    std::printf("[Renderer] Finestra OpenGL attiva (Pipeline interna %ux%u, Aspect Ratio %s)\n",
        g_internalWidth, g_internalHeight, g_widescreen ? "16:9 Widescreen" : "4:3 Originale");
    return true;
}

void Shutdown() {
#ifdef _WIN32
    if (g_logoTex) {
        glDeleteTextures(1, &g_logoTex);
        g_logoTex = 0;
    }
    if (g_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_hglrc);
        g_hglrc = nullptr;
    }
    if (g_hwnd && g_hdc) {
        ReleaseDC(g_hwnd, g_hdc);
        g_hdc = nullptr;
    }
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
#endif
    std::printf("[Renderer] Chiusura backend grafico.\n");
}

bool ProcessWindowEvents() {
#ifdef _WIN32
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_running = false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
#endif
    return g_running;
}

static void DrawRect2D(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
#ifdef _WIN32
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
#endif
}

void BeginFrame(uint64_t frameCount) {
#ifdef _WIN32
    glClearColor(0.06f, 0.05f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Coordinate ortografiche 1280x720 per l'interfaccia
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1280.0, 720.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Sfondo gradiente stile Warp Room di Crash Bash
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor3f(0.05f, 0.08f, 0.22f); glVertex2f(0.0f,    0.0f);
    glColor3f(0.05f, 0.08f, 0.22f); glVertex2f(1280.0f, 0.0f);
    glColor3f(0.18f, 0.06f, 0.25f); glVertex2f(1280.0f, 720.0f);
    glColor3f(0.18f, 0.06f, 0.25f); glVertex2f(0.0f,    720.0f);
    glEnd();

    // Disegna il logo cb.png al centro in alto con leggera oscillazione fluida a 60Hz
    if (g_logoTex != 0 && g_logoW > 0 && g_logoH > 0) {
        float t = static_cast<float>(frameCount) * 0.04f;
        float bobY = std::sin(t) * 6.0f;
        float targetH = 290.0f;
        float aspect = static_cast<float>(g_logoW) / static_cast<float>(g_logoH);
        float targetW = targetH * aspect;
        float x = (1280.0f - targetW) * 0.5f;
        float y = 45.0f + bobY;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_logoTex);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,           y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + targetW, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + targetW, y + targetH);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,           y + targetH);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
#endif
}

void SubmitOrderingTable() {
#ifdef _WIN32
    // Visualizzatore live dei 4 slot Controller (Multitap PS1 -> XInput / SteamInput)
    const float cardW = 260.0f;
    const float cardH = 220.0f;
    const float gap   = 32.0f;
    const float totalW = 4.0f * cardW + 3.0f * gap;
    const float startX = (1280.0f - totalW) * 0.5f;
    const float startY = 430.0f;

    for (int p = 0; p < cb::input::MAX_PLAYERS; ++p) {
        const auto& ctrl = cb::input::GetController(p);
        bool connected = (ctrl.flags & cb::input::CB_CTRL_CONNECTED) != 0;

        float cx = startX + p * (cardW + gap);
        float cy = startY;

        // Card background
        if (connected) {
            DrawRect2D(cx - 3, cy - 3, cardW + 6, cardH + 6, 1.0f, 0.55f, 0.0f, 0.9f);
            DrawRect2D(cx, cy, cardW, cardH, 0.12f, 0.16f, 0.28f, 0.95f);
        } else {
            DrawRect2D(cx - 2, cy - 2, cardW + 4, cardH + 4, 0.3f, 0.3f, 0.4f, 0.5f);
            DrawRect2D(cx, cy, cardW, cardH, 0.08f, 0.08f, 0.14f, 0.85f);
        }

        // Barra superiore stato giocatore (Verde = Connesso, Grigio = Slot Libero/Bot)
        if (connected) {
            DrawRect2D(cx + 12, cy + 12, cardW - 24, 24, 0.15f, 0.85f, 0.35f, 1.0f);
        } else {
            DrawRect2D(cx + 12, cy + 12, cardW - 24, 24, 0.25f, 0.25f, 0.32f, 1.0f);
        }

        // Visualizzazione D-Pad / Stick Analogico (sinistra della card)
        float dpadX = cx + 65.0f;
        float dpadY = cy + 125.0f;
        DrawRect2D(dpadX - 36, dpadY - 36, 72, 72, 0.05f, 0.05f, 0.09f, 1.0f);

        auto btnColor = [&](uint32_t mask, float r, float g, float b) {
            bool on = (ctrl.buttonsHeld & mask) != 0;
            return on ? std::tuple<float,float,float>{r, g, b}
                      : std::tuple<float,float,float>{0.22f, 0.22f, 0.28f};
        };

        auto [uR, uG, uB] = btnColor(cb::input::CB_BTN_UP,    1.0f, 0.85f, 0.1f);
        auto [dR, dG, dB] = btnColor(cb::input::CB_BTN_DOWN,  1.0f, 0.85f, 0.1f);
        auto [lR, lG, lB] = btnColor(cb::input::CB_BTN_LEFT,  1.0f, 0.85f, 0.1f);
        auto [rR, rG, rB] = btnColor(cb::input::CB_BTN_RIGHT, 1.0f, 0.85f, 0.1f);

        DrawRect2D(dpadX - 10, dpadY - 32, 20, 20, uR, uG, uB);
        DrawRect2D(dpadX - 10, dpadY + 12, 20, 20, dR, dG, dB);
        DrawRect2D(dpadX - 32, dpadY - 10, 20, 20, lR, lG, lB);
        DrawRect2D(dpadX + 12, dpadY - 10, 20, 20, rR, rG, rB);

        // Visualizzazione Pulsanti Azione PS1/Xbox (destra della card: Triangolo/Y, Cerchio/B, Croce/A, Quadrato/X)
        float actX = cx + 190.0f;
        float actY = cy + 125.0f;

        auto [triR, triG, triB] = btnColor(cb::input::CB_BTN_TRIANGLE, 0.2f, 0.9f, 0.4f);
        auto [cirR, cirG, cirB] = btnColor(cb::input::CB_BTN_CIRCLE,   0.95f, 0.25f, 0.25f);
        auto [croR, croG, croB] = btnColor(cb::input::CB_BTN_CROSS,    0.25f, 0.55f, 1.0f);
        auto [squR, squG, squB] = btnColor(cb::input::CB_BTN_SQUARE,   0.95f, 0.4f, 0.85f);

        DrawRect2D(actX - 10, actY - 32, 20, 20, triR, triG, triB); // Triangolo / Y
        DrawRect2D(actX + 12, actY - 10, 20, 20, cirR, cirG, cirB); // Cerchio / B
        DrawRect2D(actX - 10, actY + 12, 20, 20, croR, croG, croB); // Croce / A
        DrawRect2D(actX - 32, actY - 10, 20, 20, squR, squG, squB); // Quadrato / X

        // Dorsali L1/L2 e R1/R2
        auto [l1R, l1G, l1B] = btnColor(cb::input::CB_BTN_L1 | cb::input::CB_BTN_L2, 1.0f, 0.6f, 0.1f);
        auto [r1R, r1G, r1B] = btnColor(cb::input::CB_BTN_R1 | cb::input::CB_BTN_R2, 1.0f, 0.6f, 0.1f);
        DrawRect2D(cx + 25,  cy + 55, 65, 14, l1R, l1G, l1B);
        DrawRect2D(cx + 170, cy + 55, 65, 14, r1R, r1G, r1B);
    }
#endif
}

void EndFrame() {
#ifdef _WIN32
    if (g_hdc) {
        SwapBuffers(g_hdc);
    }
#endif
}

} // namespace cb::renderer

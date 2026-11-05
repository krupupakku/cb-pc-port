#include "renderer/renderer.h"
#include "renderer/prompt_overlay.h"
#include "core/gpu.h"
#include "input/input_manager.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>

// Definizione estensioni OpenGL 2.0+ per Shaders GLSL moderni
typedef char GLchar;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void   (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint  (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void   (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void   (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);

static PFNGLCREATESHADERPROC       glCreateShader_ptr       = nullptr;
static PFNGLSHADERSOURCEPROC       glShaderSource_ptr       = nullptr;
static PFNGLCOMPILESHADERPROC      glCompileShader_ptr      = nullptr;
static PFNGLGETSHADERIVPROC        glGetShaderiv_ptr        = nullptr;
static PFNGLGETSHADERINFOLOGPROC   glGetShaderInfoLog_ptr   = nullptr;
static PFNGLCREATEPROGRAMPROC      glCreateProgram_ptr      = nullptr;
static PFNGLATTACHSHADERPROC       glAttachShader_ptr       = nullptr;
static PFNGLLINKPROGRAMPROC        glLinkProgram_ptr        = nullptr;
static PFNGLGETPROGRAMIVPROC       glGetProgramiv_ptr       = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC  glGetProgramInfoLog_ptr  = nullptr;
static PFNGLUSEPROGRAMPROC         glUseProgram_ptr         = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ptr = nullptr;
static PFNGLUNIFORM1IPROC          glUniform1i_ptr          = nullptr;
static PFNGLUNIFORM2FPROC          glUniform2f_ptr          = nullptr;
static PFNGLDELETESHADERPROC       glDeleteShader_ptr       = nullptr;
static PFNGLDELETEPROGRAMPROC      glDeleteProgram_ptr      = nullptr;

#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
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

enum AspectMode {
    ASPECT_4_3 = 0,
    ASPECT_16_9 = 1,
    ASPECT_21_9 = 2,
    ASPECT_FILL = 3,
    ASPECT_COUNT
};

enum VideoFilter {
    FILTER_SHARP_HD = 0,
    FILTER_CRT = 1,
    FILTER_SMOOTH = 2,
    FILTER_COUNT
};

static AspectMode  g_aspectMode   = ASPECT_4_3;
static VideoFilter g_filterMode   = FILTER_SHARP_HD;
static int         g_internalScale = 2; // Default 2x HD (1024x480), cicla con F7 (1x, 2x, 4x)
static bool        g_bilinearTex   = true; // Filtro bilineare su texture 3D
static bool        g_fullscreen   = false;
static RECT        g_prevWinRect  = {};
static DWORD       g_prevWinStyle = 0;

static bool   g_shadersSupported = false;
static GLuint g_progSharp        = 0;
static GLuint g_progCRT          = 0;
static GLuint g_progSmooth       = 0;

#ifdef _WIN32
static HWND   g_hwnd  = nullptr;
static HDC    g_hdc   = nullptr;
static HGLRC  g_hglrc = nullptr;
static GLuint g_logoTex = 0;
static int    g_logoW   = 0;
static int    g_logoH   = 0;

void CycleAspectRatio() {
    g_aspectMode = static_cast<AspectMode>((g_aspectMode + 1) % ASPECT_COUNT);
    std::printf("[Renderer] Aspect Ratio: %s (3D Widescreen Hack: %s)\n",
                GetAspectRatioName(), IsWidescreenHackEnabled() ? "ATTIVO" : "DISATTIVATO");
}

float GetCurrentAspectRatio() {
    switch (g_aspectMode) {
        case ASPECT_4_3:  return 4.0f / 3.0f;
        case ASPECT_16_9: return 16.0f / 9.0f;
        case ASPECT_21_9: return 21.0f / 9.0f;
        case ASPECT_FILL: return (float)g_winWidth / (float)(g_winHeight ? g_winHeight : 1);
        default: return 4.0f / 3.0f;
    }
}

const char* GetAspectRatioName() {
    switch (g_aspectMode) {
        case ASPECT_4_3:  return "4:3 (Originale)";
        case ASPECT_16_9: return "16:9 (Widescreen)";
        case ASPECT_21_9: return "21:9 (Ultrawide)";
        case ASPECT_FILL: return "Fullscreen Stretch";
        default: return "4:3";
    }
}

void SetAspectRatio(float aspect) {
    if (aspect >= 1.7f) g_aspectMode = ASPECT_16_9;
    else g_aspectMode = ASPECT_4_3;
}

void CycleInternalResolution() {
    if (g_internalScale == 1) g_internalScale = 2;
    else if (g_internalScale == 2) g_internalScale = 3;
    else if (g_internalScale == 3) g_internalScale = 4;
    else g_internalScale = 1;
    std::printf("[Renderer] Risoluzione Interna impostata a: %s\n", GetInternalResolutionName());
}

int GetInternalResolutionScale() {
    return g_internalScale;
}

const char* GetInternalResolutionName() {
    switch (g_internalScale) {
        case 1:  return "1x (Nativo PS1 240p)";
        case 2:  return "2x (HD 480p)";
        case 3:  return "3x (Super HD 720p)";
        case 4:  return "4x (Ultra HD 4K / 960p)";
        default: return "2x HD";
    }
}

bool IsWidescreenHackEnabled() {
    return g_aspectMode == ASPECT_16_9 || g_aspectMode == ASPECT_21_9;
}

void ToggleBilinearTextures() {
    g_bilinearTex = !g_bilinearTex;
    std::printf("[Renderer] Filtro Texture sui modelli 3D: %s\n",
                g_bilinearTex ? "Bilineare HD (Morbido)" : "Point Retro (Pixel PS1)");
}

bool IsBilinearTexturesEnabled() {
    return g_bilinearTex;
}

void CycleVideoFilter() {
    g_filterMode = static_cast<VideoFilter>((g_filterMode + 1) % FILTER_COUNT);
    std::printf("[Renderer] Filtro Video: %s\n", GetVideoFilterName());
}

const char* GetVideoFilterName() {
    switch (g_filterMode) {
        case FILTER_SHARP_HD: return "Sharp HD (4K)";
        case FILTER_CRT:      return "Retro CRT Scanlines";
        case FILTER_SMOOTH:   return "Smooth Bilinear";
        default: return "Sharp HD";
    }
}

void ToggleFullscreen() {
    if (!g_hwnd) return;
    g_fullscreen = !g_fullscreen;
    if (g_fullscreen) {
        g_prevWinStyle = static_cast<DWORD>(GetWindowLongA(g_hwnd, GWL_STYLE));
        GetWindowRect(g_hwnd, &g_prevWinRect);

        HMONITOR hmon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(hmon, &mi);

        SetWindowLongA(g_hwnd, GWL_STYLE, g_prevWinStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(g_hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, g_prevWinStyle);
        SetWindowPos(g_hwnd, nullptr,
                     g_prevWinRect.left, g_prevWinRect.top,
                     g_prevWinRect.right - g_prevWinRect.left,
                     g_prevWinRect.bottom - g_prevWinRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    std::printf("[Renderer] Schermo Intero: %s\n", g_fullscreen ? "ATTIVO" : "DISATTIVATO");
}

bool IsFullscreen() {
    return g_fullscreen;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_SYSKEYDOWN:
            if (wParam == VK_RETURN && (lParam & (1 << 29))) { // Alt + Enter
                ToggleFullscreen();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
                PostQuitMessage(0);
                return 0;
            }
            if (wParam == VK_F5) {
                CycleAspectRatio();
                return 0;
            }
            if (wParam == VK_F6) {
                CycleVideoFilter();
                return 0;
            }
            if (wParam == VK_F7) {
                CycleInternalResolution();
                return 0;
            }
            if (wParam == VK_F8) {
                ToggleBilinearTextures();
                return 0;
            }
            if (wParam == VK_F9) {
                cb::ui::CyclePromptDevice();
                return 0;
            }
            if (wParam == VK_F10) {
                cb::ui::TogglePromptOverlay();
                return 0;
            }
            if (wParam == VK_F11) {
                ToggleFullscreen();
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

static void LoadGlExtensions() {
    glCreateShader_ptr       = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glShaderSource_ptr       = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader_ptr      = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glGetShaderiv_ptr        = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog_ptr   = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram_ptr      = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader_ptr       = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram_ptr        = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv_ptr       = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog_ptr  = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
    glUseProgram_ptr         = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glGetUniformLocation_ptr = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glUniform1i_ptr          = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
    glUniform2f_ptr          = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
    glDeleteShader_ptr       = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glDeleteProgram_ptr      = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");

    g_shadersSupported = glCreateShader_ptr && glShaderSource_ptr && glCompileShader_ptr &&
                         glCreateProgram_ptr && glAttachShader_ptr && glLinkProgram_ptr &&
                         glUseProgram_ptr && glGetUniformLocation_ptr;
}

static GLuint CompileShader(GLenum type, const char* src) {
    if (!glCreateShader_ptr) return 0;
    GLuint s = glCreateShader_ptr(type);
    glShaderSource_ptr(s, 1, &src, nullptr);
    glCompileShader_ptr(s);
    GLint ok = 0;
    glGetShaderiv_ptr(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog_ptr(s, sizeof(log), nullptr, log);
        std::printf("[Renderer] Errore shader: %s\n", log);
        glDeleteShader_ptr(s);
        return 0;
    }
    return s;
}

static GLuint CreateProgram(const char* vsSrc, const char* fsSrc) {
    if (!glCreateProgram_ptr) return 0;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
        return 0;
    }
    GLuint p = glCreateProgram_ptr();
    glAttachShader_ptr(p, vs);
    glAttachShader_ptr(p, fs);
    glLinkProgram_ptr(p);
    GLint ok = 0;
    glGetProgramiv_ptr(p, GL_LINK_STATUS, &ok);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog_ptr(p, sizeof(log), nullptr, log);
        std::printf("[Renderer] Errore program: %s\n", log);
        glDeleteProgram_ptr(p);
        return 0;
    }
    return p;
}

static void InitShaders() {
    if (!g_shadersSupported) {
        std::printf("[Renderer] Driver non compatibile con estensioni GLSL shader: fallback standard.\n");
        return;
    }

    const char* vsCommon =
        "void main() {\n"
        "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "}\n";

    // 1. Sharp Bilinear Pixel-Perfect HD (4K / 1080p senza sfocatura slavata)
    const char* fsSharp =
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_texSize;\n"
        "uniform vec2 u_outSize;\n"
        "void main() {\n"
        "    vec2 coord = gl_TexCoord[0].xy;\n"
        "    vec2 pos = coord * vec2(1024.0, 512.0);\n"
        "    vec2 texel = pos - vec2(0.5);\n"
        "    vec2 f = fract(texel);\n"
        "    vec2 scale = u_outSize / u_texSize;\n"
        "    vec2 sharp_f = clamp(f * scale, 0.0, 0.5) + clamp((f - 1.0) * scale + 0.5, 0.0, 0.5);\n"
        "    vec2 uv = (floor(texel) + 0.5 + sharp_f) / vec2(1024.0, 512.0);\n"
        "    gl_FragColor = texture2D(u_tex, uv);\n"
        "}\n";

    // 2. Retro CRT Scanlines
    const char* fsCRT =
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_texSize;\n"
        "uniform vec2 u_outSize;\n"
        "void main() {\n"
        "    vec2 coord = gl_TexCoord[0].xy;\n"
        "    vec4 col = texture2D(u_tex, coord);\n"
        "    float scanline = sin(coord.y * 512.0 * 3.14159265);\n"
        "    float scan = 0.82 + 0.18 * scanline * scanline;\n"
        "    vec3 rgb = pow(col.rgb, vec3(0.95)) * scan * 1.05;\n"
        "    gl_FragColor = vec4(rgb, col.a);\n"
        "}\n";

    // 3. Smooth Bilinear HQ
    const char* fsSmooth =
        "uniform sampler2D u_tex;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_tex, gl_TexCoord[0].xy);\n"
        "}\n";

    g_progSharp  = CreateProgram(vsCommon, fsSharp);
    g_progCRT    = CreateProgram(vsCommon, fsCRT);
    g_progSmooth = CreateProgram(vsCommon, fsSmooth);

    if (g_progSharp && g_progCRT && g_progSmooth) {
        std::printf("[Renderer] Shaders GLSL attivati (Sharp HD 4K, CRT Scanlines, Smooth HQ).\n");
    }
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

    SetProcessDPIAware();   // coordinate reali in pixel (niente scaling sfocato / finestra "fuori schermo")

    // Dimensiona la finestra per stare sempre dentro l'area utile dello schermo (4:3 di default)
    RECT work{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left, workH = work.bottom - work.top;
    int clientH = (int)(workH * 0.80f); if (clientH > 1080) clientH = 1080;
    int clientW = clientH * 4 / 3;
    if (clientW > (int)(workW * 0.90f)) { clientW = (int)(workW * 0.90f); clientH = clientW * 3 / 4; }
    g_winWidth = clientW; g_winHeight = clientH;

    RECT rect = { 0, 0, g_winWidth, g_winHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int totW = rect.right - rect.left, totH = rect.bottom - rect.top;
    int posX = work.left + (workW - totW) / 2, posY = work.top + (workH - totH) / 2;

    g_hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "Crash Bash: Remaster+",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        posX, posY, totW, totH,
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

    LoadGlExtensions();
    InitShaders();
    LoadLogoTexture();
    cb::ui::Initialize();
    cb::SetButtonGlyphSampler(cb::ui::SampleButtonGlyph);
    g_running = true;
#endif

    std::printf("[Renderer] Finestra OpenGL attiva (Pipeline interna %ux%u, Aspect Ratio %s)\n",
        g_internalWidth, g_internalHeight, g_widescreen ? "16:9 Widescreen" : "4:3 Originale");
    std::printf("[Renderer] Tasti rapidi: F5 = Aspect Ratio | F6 = Filtri HD/CRT | F7 = Risoluzione 4K | F8 = Bilineare | F9 = Stile Tasti (Xbox/PS/KB) | F10 = Prompt HUD | F11 = Schermo Intero\n");
    return true;
}

void Shutdown() {
#ifdef _WIN32
    if (g_progSharp && glDeleteProgram_ptr) glDeleteProgram_ptr(g_progSharp);
    if (g_progCRT && glDeleteProgram_ptr) glDeleteProgram_ptr(g_progCRT);
    if (g_progSmooth && glDeleteProgram_ptr) glDeleteProgram_ptr(g_progSmooth);
    g_progSharp = g_progCRT = g_progSmooth = 0;

    if (g_logoTex) {
        glDeleteTextures(1, &g_logoTex);
        g_logoTex = 0;
    }
    cb::ui::Shutdown();
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

void SetWindowTitle(const char* title) {
#ifdef _WIN32
    if (g_hwnd) SetWindowTextA(g_hwnd, title);
#else
    (void)title;
#endif
}

void PresentPsxFrame(const uint16_t* vram, int x0, int y0, int w, int h, bool enabled, float aspect,
                     const uint32_t* hires_rgba, int hires_scale) {
#ifdef _WIN32
    static GLuint tex = 0;
    static int currentTexScale = 0;
    static std::vector<uint32_t> rgba(1024 * 512);

    int activeScale = (hires_rgba && hires_scale > 1) ? hires_scale : 1;
    if (!tex || currentTexScale != activeScale) {
        if (tex) glDeleteTextures(1, &tex);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024 * activeScale, 512 * activeScale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        currentTexScale = activeScale;
    }
    glViewport(0, 0, g_winWidth, g_winHeight);
    glDisable(GL_BLEND);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (enabled && w > 0 && h > 0) {
        glBindTexture(GL_TEXTURE_2D, tex);
        if (activeScale > 1 && hires_rgba) {
            // Upload diretto del buffer ad alta risoluzione (1080p / 4K)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 1024 * activeScale);
            int hx0 = x0 * activeScale, hy0 = y0 * activeScale;
            int hw = w * activeScale, hh = h * activeScale;
            const uint32_t* src = hires_rgba + (hy0 * 1024 * activeScale + hx0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, hw, hh, GL_RGBA, GL_UNSIGNED_BYTE, src);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        } else {
            // Modalità 1x nativa PS1 con espansione a 24-bit
            for (int j = 0; j < h; j++) {
                for (int i = 0; i < w; i++) {
                    uint16_t p = vram[((y0 + j) & 511) * 1024 + ((x0 + i) & 1023)];
                    uint32_t r = ((p & 31) << 3) | ((p & 31) >> 2);
                    uint32_t g = (((p >> 5) & 31) << 3) | (((p >> 5) >> 2) & 7);
                    uint32_t b = (((p >> 10) & 31) << 3) | (((p >> 10) >> 2) & 7);
                    rgba[j * 1024 + i] = 0xFF000000u | (b << 16) | (g << 8) | r;
                }
            }
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 1024);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }

        // Aspect ratio dinamico (F5) o forzato da riga comando
        float effectiveAspect = (aspect > 0.0f) ? aspect : GetCurrentAspectRatio();
        float winAspect = (float)g_winWidth / (float)(g_winHeight ? g_winHeight : 1);
        float sx = 1.0f, sy = 1.0f;
        if (winAspect > effectiveAspect) sx = effectiveAspect / winAspect; else sy = winAspect / effectiveAspect;

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
        glEnable(GL_TEXTURE_2D);

        int renderW = w * activeScale;
        int renderH = h * activeScale;
        int totalTexW = 1024 * activeScale;
        int totalTexH = 512 * activeScale;

        // Attivazione Shader GLSL moderno per scaling 4K e filtri video
        if (g_shadersSupported && glUseProgram_ptr) {
            GLuint prog = 0;
            if (g_filterMode == FILTER_SHARP_HD) prog = g_progSharp;
            else if (g_filterMode == FILTER_CRT) prog = g_progCRT;
            else if (g_filterMode == FILTER_SMOOTH) prog = g_progSmooth;

            if (prog) {
                glUseProgram_ptr(prog);
                GLint locTex = glGetUniformLocation_ptr(prog, "u_tex");
                GLint locTexSize = glGetUniformLocation_ptr(prog, "u_texSize");
                GLint locOutSize = glGetUniformLocation_ptr(prog, "u_outSize");
                if (locTex >= 0) glUniform1i_ptr(locTex, 0);
                if (locTexSize >= 0) glUniform2f_ptr(locTexSize, (float)renderW, (float)renderH);
                if (locOutSize >= 0) glUniform2f_ptr(locOutSize, (float)g_winWidth * sx, (float)g_winHeight * sy);
            }
        }

        glColor4f(1, 1, 1, 1);
        float u1 = (float)renderW / (float)totalTexW;
        float v1 = (float)renderH / (float)totalTexH;
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);   glVertex2f(-sx,  sy);
        glTexCoord2f(u1, 0);  glVertex2f( sx,  sy);
        glTexCoord2f(u1, v1); glVertex2f( sx, -sy);
        glTexCoord2f(0, v1);  glVertex2f(-sx, -sy);
        glEnd();

        if (g_shadersSupported && glUseProgram_ptr) glUseProgram_ptr(0);
        glDisable(GL_TEXTURE_2D);

        // Rendering dell'overlay HD dei prompt dinamici dei comandi (Xbox / PlayStation / Tastiera)
        cb::ui::Render(g_winWidth, g_winHeight, 1.0f);
    }
    if (g_hdc) SwapBuffers(g_hdc);
#else
    (void)vram; (void)x0; (void)y0; (void)w; (void)h; (void)enabled; (void)aspect;
    (void)hires_rgba; (void)hires_scale;
#endif
}

} // namespace cb::renderer

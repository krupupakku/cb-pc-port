#include "input/input_manager.h"
#include "renderer/prompt_overlay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

namespace cb::input {

static BashControllerState g_controllers[MAX_PLAYERS] = {};
static uint32_t            g_prevButtons[MAX_PLAYERS] = {};
static InputBackend        g_backend = InputBackend::XInputNative;

void Initialize() {
    std::memset(g_controllers, 0, sizeof(g_controllers));
    std::memset(g_prevButtons, 0, sizeof(g_prevButtons));

    // Se il gioco è avviato da Steam, le variabili d'ambiente SteamAppId / SteamGameId sono presenti
    if (std::getenv("SteamAppId") != nullptr || std::getenv("SteamGameId") != nullptr) {
        g_backend = InputBackend::SteamInput;
        std::printf("[Input] Rilevato avvio tramite Steam! Backend attivo: SteamInput API (+ fallback XInput)\n");
    } else {
        g_backend = InputBackend::XInputNative;
        std::printf("[Input] Avvio standalone rilevato. Backend attivo: XInput Nativo (4 giocatori / Multitap virtuale)\n");
    }

    PollControllers();
}

void Shutdown() {
#ifdef _WIN32
    for (int i = 0; i < MAX_PLAYERS; i++) {
        XINPUT_VIBRATION vib{};
        XInputSetState(static_cast<DWORD>(i), &vib);
    }
#endif
    std::printf("[Input] Sottosistema input terminato.\n");
}

void SetVibration(int playerIndex, uint8_t smallMotor, uint8_t bigMotor) {
#ifdef _WIN32
    if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return;
    static uint8_t lastSmall[MAX_PLAYERS] = {};
    static uint8_t lastBig[MAX_PLAYERS] = {};
    if (smallMotor == lastSmall[playerIndex] && bigMotor == lastBig[playerIndex]) return;
    lastSmall[playerIndex] = smallMotor;
    lastBig[playerIndex] = bigMotor;

    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed = static_cast<WORD>(bigMotor * 257);
    vib.wRightMotorSpeed = smallMotor ? (smallMotor > 1 ? static_cast<WORD>(smallMotor * 257) : 65535) : 0;
    XInputSetState(static_cast<DWORD>(playerIndex), &vib);
#endif
}

void PollControllers() {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        uint32_t held = CB_BTN_NONE;
        bool connected = false;
        int32_t angle = 0;
        bool analogActive = false;
        uint8_t stick_lx = 128, stick_ly = 128;
        uint8_t stick_rx = 128, stick_ry = 128;

#ifdef _WIN32
        XINPUT_STATE state{};
        if (XInputGetState(static_cast<DWORD>(i), &state) == ERROR_SUCCESS) {
            connected = true;
            const WORD w = state.Gamepad.wButtons;

            if (w & XINPUT_GAMEPAD_BACK)           held |= CB_BTN_SELECT;
            if (w & XINPUT_GAMEPAD_LEFT_THUMB)     held |= CB_BTN_L3;
            if (w & XINPUT_GAMEPAD_RIGHT_THUMB)    held |= CB_BTN_R3;
            if (w & XINPUT_GAMEPAD_START)          held |= CB_BTN_START;
            if (w & XINPUT_GAMEPAD_DPAD_UP)        held |= CB_BTN_UP;
            if (w & XINPUT_GAMEPAD_DPAD_RIGHT)     held |= CB_BTN_RIGHT;
            if (w & XINPUT_GAMEPAD_DPAD_DOWN)      held |= CB_BTN_DOWN;
            if (w & XINPUT_GAMEPAD_DPAD_LEFT)      held |= CB_BTN_LEFT;
            if (w & XINPUT_GAMEPAD_LEFT_SHOULDER)  held |= CB_BTN_L1;
            if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) held |= CB_BTN_R1;
            if (w & XINPUT_GAMEPAD_Y)              held |= CB_BTN_TRIANGLE;
            if (w & XINPUT_GAMEPAD_B)              held |= CB_BTN_CIRCLE;
            if (w & XINPUT_GAMEPAD_A)              held |= CB_BTN_CROSS;
            if (w & XINPUT_GAMEPAD_X)              held |= CB_BTN_SQUARE;

            if (state.Gamepad.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) held |= CB_BTN_L2;
            if (state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) held |= CB_BTN_R2;

            // Coordinate analogiche PS1 (0..255, 128 = centro)
            stick_lx = static_cast<uint8_t>(std::clamp((state.Gamepad.sThumbLX + 32768) >> 8, 0, 255));
            stick_ly = static_cast<uint8_t>(std::clamp((32767 - state.Gamepad.sThumbLY) >> 8, 0, 255));
            stick_rx = static_cast<uint8_t>(std::clamp((state.Gamepad.sThumbRX + 32768) >> 8, 0, 255));
            stick_ry = static_cast<uint8_t>(std::clamp((32767 - state.Gamepad.sThumbRY) >> 8, 0, 255));

            // Conversione stick sinistro nell'angolo nativo di Crash Bash (0..4095, 0x1000 = 360°)
            const short lx = state.Gamepad.sThumbLX;
            const short ly = state.Gamepad.sThumbLY;
            if (std::abs(static_cast<int>(lx)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
                std::abs(static_cast<int>(ly)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
                analogActive = true;
                double rad = std::atan2(static_cast<double>(lx), static_cast<double>(ly));
                int32_t bashAng = static_cast<int32_t>((rad / (2.0 * 3.14159265358979323846)) * 4096.0);
                angle = bashAng & 0xFFF;
                // Mappa anche su D-Pad per compatibilita' universale con tutti i minigiochi
                if (ly >  16000) held |= CB_BTN_UP;
                if (ly < -16000) held |= CB_BTN_DOWN;
                if (lx >  16000) held |= CB_BTN_RIGHT;
                if (lx < -16000) held |= CB_BTN_LEFT;
            }
            if (held != CB_BTN_NONE || analogActive) {
                cb::ui::NotifyInputActivity(true, false);
            }
        } else {
            SetVibration(i, 0, 0);
        }

        // Supporto Tastiera su Player 1 (combinabile o fallback)
        if (i == 0) {
            uint32_t kbHeld = CB_BTN_NONE;
            if (GetAsyncKeyState(VK_UP)    & 0x8000 || GetAsyncKeyState('W') & 0x8000) kbHeld |= CB_BTN_UP;
            if (GetAsyncKeyState(VK_DOWN)  & 0x8000 || GetAsyncKeyState('S') & 0x8000) kbHeld |= CB_BTN_DOWN;
            if (GetAsyncKeyState(VK_LEFT)  & 0x8000 || GetAsyncKeyState('A') & 0x8000) kbHeld |= CB_BTN_LEFT;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) kbHeld |= CB_BTN_RIGHT;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000 || GetAsyncKeyState('Z') & 0x8000) kbHeld |= CB_BTN_CROSS;
            if (GetAsyncKeyState('X')      & 0x8000) kbHeld |= CB_BTN_SQUARE;
            if (GetAsyncKeyState('C')      & 0x8000) kbHeld |= CB_BTN_CIRCLE;
            if (GetAsyncKeyState('V')      & 0x8000) kbHeld |= CB_BTN_TRIANGLE;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) kbHeld |= CB_BTN_R1;
            if (GetAsyncKeyState(VK_RETURN)& 0x8000) kbHeld |= CB_BTN_START;

            if (kbHeld != CB_BTN_NONE) {
                cb::ui::NotifyInputActivity(false, true);
                connected = true;
                held |= kbHeld;
            }
        }
#endif

        BashControllerState& ctrl = g_controllers[i];
        if (connected) {
            ctrl.flags = CB_CTRL_CONNECTED | CB_CTRL_HAS_ANALOG |
                         (analogActive ? CB_CTRL_ANALOG_ON : CB_CTRL_DIGITAL);
        } else {
            ctrl.flags = 0;
        }
        ctrl.targetAngle    = angle;
        ctrl.buttonsHeld    = held;
        ctrl.buttonsPressed = held & (~g_prevButtons[i]);
        ctrl.lx             = stick_lx;
        ctrl.ly             = stick_ly;
        ctrl.rx             = stick_rx;
        ctrl.ry             = stick_ry;
        g_prevButtons[i]    = held;
    }
}

const BashControllerState& GetController(int playerIndex) {
    return g_controllers[playerIndex & 3];
}

InputBackend GetActiveBackend() {
    return g_backend;
}

} // namespace cb::input

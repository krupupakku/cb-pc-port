#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Input Manager (XInput Nativo + SteamInput)
// Traduce lo stato di 4 gamepad moderni direttamente nella struct Controller
// originale di Crash Bash (include/controller.h, indirizzo PS1 0x80051338).
// =============================================================================

#include <cstdint>

namespace cb::input {

// Identico ai bitmask di include/controller.h (Eurocom)
enum BashButtons : uint32_t {
    CB_BTN_NONE      = 0x0000,
    CB_BTN_SELECT    = 0x0001,
    CB_BTN_L3        = 0x0002,
    CB_BTN_R3        = 0x0004,
    CB_BTN_START     = 0x0008,
    CB_BTN_UP        = 0x0010,
    CB_BTN_RIGHT     = 0x0020,
    CB_BTN_DOWN      = 0x0040,
    CB_BTN_LEFT      = 0x0080,
    CB_BTN_L2        = 0x0100,
    CB_BTN_R2        = 0x0200,
    CB_BTN_L1        = 0x0400,
    CB_BTN_R1        = 0x0800,
    CB_BTN_TRIANGLE  = 0x1000,
    CB_BTN_CIRCLE    = 0x2000,
    CB_BTN_CROSS     = 0x4000,
    CB_BTN_SQUARE    = 0x8000,
    CB_BTN_SHOULDERS = CB_BTN_L2 | CB_BTN_R2 | CB_BTN_L1 | CB_BTN_R1
};

enum BashControllerFlags : uint32_t {
    CB_CTRL_CONNECTED  = 0x1,
    CB_CTRL_ANALOG_ON  = 0x2,
    CB_CTRL_DIGITAL    = 0x4,
    CB_CTRL_HAS_ANALOG = 0x8
};

// Replica esatta di struct Controller in original/repos/crashbash/include/controller.h
struct BashControllerState {
    uint32_t flags;
    int32_t  field1_0x0[10];
    uint32_t isBotInput;
    uint32_t botInput;
    int32_t  field3_0x34;
    int32_t  targetAngle;    // Angolo 0..4095 (0x1000 = 360 gradi in math.h)
    int32_t  field2_0x3c[5];
    uint32_t buttonsHeld;
    uint32_t buttonsPressed; // Edge-triggered (premuti in questo frame)
};

static constexpr int MAX_PLAYERS = 4;

enum class InputBackend {
    XInputNative,
    SteamInput
};

void Initialize();
void Shutdown();

// Legge XInput / SteamInput per i 4 giocatori e popola l'array _controllers[4]
void PollControllers();

const BashControllerState& GetController(int playerIndex);
InputBackend GetActiveBackend();

} // namespace cb::input


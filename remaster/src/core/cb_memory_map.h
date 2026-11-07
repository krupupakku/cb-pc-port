#pragma once
#include <cstdint>

namespace psx::cb_mem {

// ============================================================================
// Crash Bash (SCUS-94570) Persistent RAM & Overlay Address Map
// See docs/MEMORY_MAP.md for full documentation.
// ============================================================================

// --- Binary & Overlay Base Addresses ---
constexpr uint32_t kScusFileToRamBias       = 0x8000F800u; // file_off 0x800 -> 0x80010000
constexpr uint32_t kScusTextStart           = 0x80010000u;
constexpr uint32_t kOverlay991Base          = 0x80078C90u; // 991.code.bin (code starts at +0x14 = 0x80078CA4)
constexpr uint32_t kDynamicOverlayBase      = 0x800B32B4u; // 722.type_0x69.bin, 487.bank.bin, 716..728.bank.bin
constexpr uint32_t kOverlay722TypeId        = 0x00000069u; // Main Menu overlay type ID at 0x800B32B4

// --- Core Engine Functions (SCUS_945.70 & 991.code.bin) ---
constexpr uint32_t kFnPlayMenuSfx           = 0x80022660u; // void PlayMenuSfx(int sfxId, ...)
constexpr uint32_t kFnDrawMenuText          = 0x800243A0u; // void DrawMenuText(int16_t x, int16_t y, const char* str, uint32_t flags, uint32_t colTop, uint32_t colBot)
constexpr uint32_t kFnDrawVolumeBar         = 0x800248A0u;
constexpr uint32_t kFnMenuHandleUpDownCross = 0x8007EFC0u; // int MenuHandleUpDownCross(int port, int maxIdx)
constexpr uint32_t kFnPauseOptionsInputTick = 0x8007FEBCu; // Pause OPTIONS input handler
constexpr uint32_t kFnPauseOptionsRootInput = 0x800802D8u; // Pause OPTIONS root page input (SOUND/CONTROLLER/EXIT)
constexpr uint32_t kFnProcessAndDrawMenuPage= 0x800809A0u; // void ProcessAndDrawMenuPage(MenuPageDesc* page, int16_t baseY)
constexpr uint32_t kFnPauseMenuRenderTick   = 0x80080FBCu; // Pause menu render dispatcher
constexpr uint32_t kFnPauseOptionsRenderSel = 0x80081280u; // Pause OPTIONS sub-page render selector
constexpr uint32_t kFnMainMenuSetWidgetPage = 0x80095BECu; // void MainMenuSetWidgetPage(int slot, void* widgetList)

// --- Main Menu Functions (722.type_0x69.bin @ 0x800B32B4) ---
constexpr uint32_t kFnMainMenuOptionsInput  = 0x800B4910u; // Main Menu OPTIONS input handler (f_off 0x165C)
constexpr uint32_t kFnMainMenuOptionsUpdate = 0x800B4F1Cu; // Main Menu OPTIONS update handler (f_off 0x1C68)
constexpr uint32_t kFnMainMenuOptionsInit   = 0x800B532Cu; // Main Menu OPTIONS init handler (f_off 0x2078)

// --- Global State & Input Addresses ---
constexpr uint32_t kAddrPadSlots            = 0x80051338u; // 4 * 0x58 bytes; +0x48 = buttons tapped
constexpr uint32_t kAddrP1ButtonsTapped     = 0x80051380u; // P1 newly pressed buttons bitmask
constexpr uint32_t kAddrMusicVolumePct      = 0x8005A604u; // 0..100
constexpr uint32_t kAddrSfxVolumePct        = 0x8005A608u; // 0..100
constexpr uint32_t kAddrAudioMonoFlag       = 0x8005A60Cu; // 0 = Stereo, 1 = Mono
constexpr uint32_t kAddrGameState           = 0x8005A614u;
constexpr uint32_t kAddrPauseMenuBitmask    = 0x8005A624u; // +0x10 from 0x8005A614 (0x0100 = OPTIONS)
constexpr uint32_t kAddrLanguageIndex       = 0x8005A640u; // +0x2C from 0x8005A614
constexpr uint32_t kAddrOverlay722Ptr       = 0x8005DDD0u;
constexpr uint32_t kAddrFontGlyphTable      = 0x80063EB8u; // 6 bytes per glyph

// --- Pause Menu State & Descriptors ---
constexpr uint32_t kAddrGlobalStringTable   = 0x80059668u; // char* table[]
constexpr uint32_t kAddrLangStringTablePtrs = 0x80059748u; // char** langTables[5]
constexpr uint32_t kAddrPauseOptionsDesc    = 0x80059C9Cu; // MenuPageDesc for Pause OPTIONS (SOUND, CONTROLLER, EXIT)
constexpr uint32_t kAddrPauseSoundDesc      = 0x80059D2Cu; // MenuPageDesc for Pause SOUND
constexpr uint32_t kAddrPauseControllerDesc = 0x80059E4Cu; // MenuPageDesc for Pause CONTROLLER
constexpr uint32_t kAddrPauseOptionsSubPage = 0x8009AEE8u; // 0 = Root, 1 = SOUND, 2 = CONTROLLER, 3 = EXTRA
constexpr uint32_t kAddrPauseMenuCursorIdx  = 0x8009AEECu; // 0..N
constexpr uint32_t kAddrPauseMenuPlayerPort = 0x8009AEF0u; // 0..3
constexpr uint32_t kAddrMenuBoxOriginY      = 0x8009BD18u; // int16_t (default 0x0070 = 112)

// --- Main Menu OPTIONS State & Descriptors (722.type_0x69.bin) ---
constexpr uint32_t kAddrMainMenuOptionsDesc = 0x800B8F20u; // Widget list for Main Menu OPTIONS (f_off 0x5C6C)
constexpr uint32_t kAddrMainMenuSoundDesc   = 0x800B901Cu; // Widget list for Main Menu SOUND (f_off 0x5D68)
constexpr uint32_t kAddrMainMenuCtrlDesc    = 0x800B91CCu; // Widget list for Main Menu CONTROLLER (f_off 0x5F18)
constexpr uint32_t kAddrMainMenuOptCursor   = 0x800B9508u; // Selected item in Main Menu OPTIONS (0..N)
constexpr uint32_t kAddrMainMenuOptSubPage  = 0x800B950Cu; // 0 = Root, 1 = SOUND, 2 = CONTROLLER, 3 = EXTRA

// --- Pad Button Bitmasks (in kAddrP1ButtonsTapped / PadSlot+0x48) ---
constexpr uint16_t kPadUp       = 0x0010u;
constexpr uint16_t kPadRight    = 0x0020u;
constexpr uint16_t kPadDown     = 0x0040u;
constexpr uint16_t kPadLeft     = 0x0080u;
constexpr uint16_t kPadTriangle = 0x1000u; // Back / Cancel
constexpr uint16_t kPadCircle   = 0x2000u;
constexpr uint16_t kPadCross    = 0x4000u; // Accept / Select
constexpr uint16_t kPadSquare   = 0x8000u; // Change

// --- Native Menu Colors (BGR24) ---
constexpr uint32_t kColorNormalTop   = 0x0000D0FFu; // Orange-yellow top gradient
constexpr uint32_t kColorNormalBot   = 0x000000FFu; // Red bottom gradient
constexpr uint32_t kColorSelectedTop = 0x00FFFFFFu; // White-yellow highlight top
constexpr uint32_t kColorSelectedBot = 0x0000FFFFu; // Yellow highlight bottom

} // namespace psx::cb_mem

namespace cb::mem {

constexpr uint32_t OVERLAY_BASE            = psx::cb_mem::kDynamicOverlayBase;
constexpr uint32_t FN_PLAY_UI_SFX          = psx::cb_mem::kFnPlayMenuSfx;

constexpr uint32_t PAD_STRUCT_BASE         = psx::cb_mem::kAddrPadSlots;
constexpr uint32_t PAD_STRUCT_STRIDE       = 0x58u;
constexpr uint32_t PAD_TAPPED_OFFSET       = 0x48u;
constexpr uint32_t PAD_P1_TAPPED           = psx::cb_mem::kAddrP1ButtonsTapped;

constexpr uint32_t PAD_BTN_UP              = psx::cb_mem::kPadUp;
constexpr uint32_t PAD_BTN_RIGHT           = psx::cb_mem::kPadRight;
constexpr uint32_t PAD_BTN_DOWN            = psx::cb_mem::kPadDown;
constexpr uint32_t PAD_BTN_LEFT            = psx::cb_mem::kPadLeft;
constexpr uint32_t PAD_BTN_TRIANGLE        = psx::cb_mem::kPadTriangle;
constexpr uint32_t PAD_BTN_CIRCLE          = psx::cb_mem::kPadCircle;
constexpr uint32_t PAD_BTN_CROSS           = psx::cb_mem::kPadCross;
constexpr uint32_t PAD_BTN_SQUARE          = psx::cb_mem::kPadSquare;

constexpr uint32_t PAUSE_STR_TABLE_EN      = psx::cb_mem::kAddrGlobalStringTable;
constexpr uint32_t PAUSE_STR_TABLE_PTRS    = psx::cb_mem::kAddrLangStringTablePtrs;
constexpr uint32_t PAUSE_OPT_SUBPAGE       = psx::cb_mem::kAddrPauseOptionsSubPage;
constexpr uint32_t PAUSE_OPT_CURSOR        = psx::cb_mem::kAddrPauseMenuCursorIdx;
constexpr uint32_t PAUSE_PLAYER_PORT       = psx::cb_mem::kAddrPauseMenuPlayerPort;
constexpr uint32_t PAUSE_OPT_BOX_Y         = psx::cb_mem::kAddrMenuBoxOriginY;

constexpr uint32_t MM_OPT_MAIN_WIDGETS     = psx::cb_mem::kAddrMainMenuOptionsDesc;
constexpr uint32_t MAIN_MENU_OPT_CURSOR    = psx::cb_mem::kAddrMainMenuOptCursor;
constexpr uint32_t MAIN_MENU_OPT_SUBPAGE   = psx::cb_mem::kAddrMainMenuOptSubPage;

// --- Remaster+ Custom RAM Region (0x8000C000 - 0x8000CFFF) ---
constexpr uint32_t REMASTER_STR_TABLE_BASE   = 0x8000C000u;
constexpr uint32_t REMASTER_STR_STATIC_BASE  = 0x8000C200u;
constexpr uint32_t REMASTER_STR_DYN_BASE     = 0x8000C380u;
constexpr uint32_t REMASTER_PAUSE_OPT_DESC   = 0x8000C500u;
constexpr uint32_t REMASTER_PAUSE_EXTRA_DESC = 0x8000C600u;
constexpr uint32_t REMASTER_MM_OPT_DESC      = 0x8000C800u;
constexpr uint32_t REMASTER_MM_EXTRA_DESC    = 0x8000CA00u;

} // namespace cb::mem

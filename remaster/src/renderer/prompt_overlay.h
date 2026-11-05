#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Dynamic On-Screen Button Prompts Overlay
// Rendering HD dei glifi del controller (Xbox / PlayStation / Tastiera)
// =============================================================================

#include <cstdint>

namespace cb::ui {

enum class PromptDevice {
    Auto,         // Rilevamento automatico in tempo reale (in base all'ultimo input)
    Xbox,         // Prompts stile Xbox Series / One (A, B, X, Y, LB, RB, LT, RT)
    PlayStation,  // Prompts stile DualShock / DualSense (Croce, Cerchio, Quadrato, Triangolo)
    Keyboard      // Prompts stile Tastiera PC (WASD, Spazio, Invio, Esc, Z, X, C, V)
};

void Initialize();
void Shutdown();

// Seleziona il tipo di prompt manuale o automatico
void SetPromptDevice(PromptDevice dev);
PromptDevice GetConfiguredPromptDevice();
PromptDevice GetEffectivePromptDevice();
void CyclePromptDevice();
const char* GetPromptDeviceName();

// Mostra / nasconde la barra dei prompt a schermo (Hotkey F9)
void TogglePromptOverlay();
bool IsPromptOverlayVisible();

// Notifica un evento di input da controller o tastiera per il rilevamento automatico
void NotifyInputActivity(bool fromGamepad, bool fromKeyboard);

// Campiona il glifo HD sostituito per il pulsante richiesto nel gioco originale:
// btnIndex: 0 = Cross (Xbox A, KB Space), 1 = Square (Xbox X, KB X), 2 = Triangle (Xbox Y, KB V), 3 = Circle (Xbox B, KB C)
// nu, nv: coordinate normalizzate 0.0f .. 1.0f all'interno dell'area del pulsante
// Restituisce true se il pixel deve essere sostituito con il glifo del dispositivo attivo
bool SampleButtonGlyph(int btnIndex, float nu, float nv, uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA);

// Disegna l'overlay dei prompt sopra il frame di gioco (se attivo con F10)
void Render(int windowWidth, int windowHeight, float uiScale);

} // namespace cb::ui


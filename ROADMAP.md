# Roadmap di Sviluppo — Crash Bash: Remaster+

Questa roadmap definisce il percorso tecnico step-by-step per portare *Crash Bash* da binario PS1 (MIPS R3000A + SDK Sony Psy-Q) a gioco nativo PC con funzionalità moderne.

---

## Fase 0: Ambiente di Lavoro, Studio e Pipeline Asset
> **Obiettivo:** Avere un ambiente riproducibile tra i membri del team, estrarre tutti i dati dal disco originale e comprendere l'architettura degli eseguibili Eurocom.

- [x] Definizione struttura directory (`original/` vs `remaster/`) e protezione `.gitignore` per le ROM.
- [x] Clonazione ed esplorazione dei repository di supporto in `original/repos/`:
  - `mateusfavarin/crashbash` (analisi copertura decompilazione e lista `symbols/`).
  - `xan1242/CrashBashEx` (compilazione tool ed estrazione di `CRASHBSH.DAT`).
  - `Helias/Crash-Bandicoot-Resources` (raccolta specifiche formati file).
- [x] Creazione pipeline di estrazione automatica (`cb_extract`) per `CRASHBSH.DAT` e verifica file ISO `SCUS-94570`.
- [x] Mappatura completa del contenuto estratto da `CRASHBSH.DAT` (overlay dei minigiochi, modelli, texture TIM/VRAM, banchi audio VAB/SEQ).

---

## Fase 1: Esplorazione Decompilazione & Scelta Strategia di Recompiling
> **Obiettivo:** Far girare la logica di gioco su PC x86_64.

*Crash Bash* è stato scritto in C con l'SDK Psy-Q e divide il codice tra l'eseguibile principale (`SCUS_945.70`) e vari **overlay** caricati dinamicamente per ogni minigioco/warp room.

- [x] **Analisi dello stato di `mateusfavarin/crashbash`:**
  - Verificato che solo ~15-20% del codice è decompilato in C: l'approccio solo-decompilazione avrebbe richiesto mesi prima di un boot giocabile.
- [x] **Strategia Esecuzione Ibrida / Static Recompiler Core:**
  - Implementato interprete MIPS R3000A ad alte prestazioni con coprocessore GTE (fixed-point arithmetic e calcoli geometrici).
  - Supporto al caricamento dinamico degli overlay binari di tutti i minigiochi.
- [x] **Layer di Astrazione Psy-Q (`remaster/src/core/`):**
  - Implementazione HLE per gestione memoria, DMA, interrupt VSync, timer hardware e caricamento CD-ROM (`libcd` reindirizzato a `CRASHBSH.DAT`).

---

## Fase 2: Backend Grafico PC, 4K e Widescreen
> **Obiettivo:** Sostituire la GPU/GTE PS1 con un renderer moderno ad alta risoluzione.

- [x] **Renderer PC con supporto OpenGL (`remaster/src/renderer/`):**
  - Finestra nativa Win32 con context OpenGL.
  - Rasterizer GPU PS1 software ad alte prestazioni con upscaling e display centrico.
  - Correzione proporzioni aspect ratio (4:3 con centering dinamico, eliminato zoom/sbordamento).
  - Correzione bug istruzioni GTE (fix flag e calcolo matrici `OP`, eliminata la distorsione sui modelli di trofei, gemme e cristalli).
- [x] **Aumento della Risoluzione Interna di Rendering (1x, 2x, 3x Super HD, 4x Ultra HD):**
  - Rasterizzazione della geometria poligonale 3D con precisione sub-pixel a risoluzione aumentata (`1024x480` a 2x, `1536x720` a 3x, `2048x960` a 4x per 4K).
  - Rasterizzatore parallelo a bande orizzontali multi-core con **Thread Pool persistente** (zero overhead di creazione thread).
  - Ottimizzazione matematica DDA a gradiente costante (eliminati i calcoli baricentrici float e divisioni per-pixel; solo step additivi interi/float).
  - Lookup texture compatto con caching per blocco subpixel negli sprite 2D/rettangoli e `std::fill_n` ultra-rapido.
  - I contorni dei modelli 3D (Crash, boss, arene, oggetti) sono disegnati con precisione geometrica elevata, eliminando la scalettatura a blocchi nativa della PS1.
  - Supporto al **Texture Bilinear Filtering sui modelli 3D** (attivabile/disattivabile con `F8`).
  - Ciclo della risoluzione interna a caldo in tempo reale con tasto **`F7`** (1x $\to$ 2x $\to$ 3x $\to$ 4x).
- [x] **Widescreen Hack 3D Nativo (Stile DuckStation):**
  - Espansione del Field of View (FOV) 3D nel coprocessore GTE (`div_x = (div * 3) / 4`).
  - La telecamera 3D inquadra il 33% di scenario in più a destra e a sinistra (mostrando i bordi dell'arena nascosti nelle TV 4:3).
  - I modelli 3D mantengono le loro proporzioni esatte e non vengono allargati/schiacciati (`0.75 * 1.3333 = 1.0`).
  - Gli elementi dell'interfaccia 2D (HUD, icone, punteggi) rimangono posizionati correttamente senza distorsioni anamorfiche.
  - Switch aspect ratio e Widescreen Hack a caldo con tasto **`F5`** (16:9 Widescreen $\leftrightarrow$ 4:3 Originale).
- [x] **Pipeline Shader GLSL Post-Processing (4K & Retro):**
  - Sharp-Bilinear Pixel-Perfect HD per scaling 4K senza effetto sfocato.
  - Retro CRT Scanlines Trinitron a fosfori.
  - Ciclo filtri a caldo con tasto **`F6`**, Schermo Intero Borderless con **`F11`** o **`Alt + Invio`**.
- [x] **Framerate e Performance:**
  - Ottimizzazione Release `-O3`, esecuzione a 60 FPS stabili.

---

## Fase 3: Input Nativo (XInput & SteamInput) e Audio
> **Obiettivo:** Esperienza plug-and-play su PC e Steam per 1-4 giocatori.

- [x] **Sottosistema Input (`remaster/src/input/`):**
  - Supporto input da tastiera e controller XInput collegati ai registri del pad PS1.
- [ ] **UI Prompts Dinamici:**
  - Mostrare a schermo icone personalizzate Xbox / PlayStation.
- [x] **Sottosistema Audio (`remaster/src/audio/` e `remaster/src/core/spu.cpp`):**
  - Emulazione SPU a 24 canali ADPCM con decodifica audio.
  - Sincronizzazione sequencer Psy-Q / Eurocom (Timer 2 calibrato a 60Hz per eliminare l'audio a rallentatore).
  - Output audio stereo a 44.1 kHz via backend nativo Windows WaveOut.

---

## Fase 4: Multiplayer Locale e Remoto (Online)
> **Obiettivo:** Giocare in 4 sia sul divano che online a bassa latenza.

- [x] **Multiplayer Locale 4P:**
  - Supporto logico per i 4 slot giocatore esposto nel core input.
- [ ] **Step 1 Online — Steam Remote Play Together:**
  - Grazie all'integrazione XInput e finestra nativa PC, abilitare il supporto immediato per sessioni condivise.
- [ ] **Step 2 Online — Rollback Netcode Nativo (`remaster/src/netcode/`):**
  - Struttura `RollbackManager` predisposta.
  - Snapshot `SaveState()` / `LoadState()` della memoria arena per rollback deterministico.
  - Sincronizzazione P2P input.

---

## Fase 5: Miglioramento Poligoni e Texture HD (Remaster+)
> **Obiettivo:** Modernizzare l'impatto visivo mantenendo lo stile artistico originale.

- [x] **Reverse Engineering del formato Modelli/Arene di Crash Bash:**
  - Realizzato tool `cb_model2obj` che estrae ed esporta i modelli originali in `.obj` con texture.
- [ ] **Custom Asset Replacement Loader:**
  - Creare nel renderer PC un sistema di *override*: caricamento modelli high-poly sostitutivi da cartella PC.
- [ ] **Upgrading Graduale:**
  - Sostituzione mesh originali con modelli HD e upscaling texture.


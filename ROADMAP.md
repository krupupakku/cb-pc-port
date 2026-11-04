# Roadmap di Sviluppo — Crash Bash: Remaster+

Questa roadmap definisce il percorso tecnico step-by-step per portare *Crash Bash* da binario PS1 (MIPS R3000A + SDK Sony Psy-Q) a gioco nativo PC con funzionalità moderne.

---

## Fase 0: Ambiente di Lavoro, Studio e Pipeline Asset
> **Obiettivo:** Avere un ambiente riproducibile tra i membri del team, estrarre tutti i dati dal disco originale e comprendere l'architettura degli eseguibili Eurocom.

- [x] Definizione struttura directory (`original/` vs `remaster/`) e protezione `.gitignore` per le ROM.
- [ ] Clonazione ed esplorazione dei repository di supporto in `original/repos/`:
  - `mateusfavarin/crashbash` (analisi copertura decompilazione e lista `symbols/`).
  - `xan1242/CrashBashEx` (compilazione tool ed estrazione di `CRASHBSH.DAT`).
  - `Helias/Crash-Bandicoot-Resources` (raccolta specifiche formati file).
- [ ] Creazione script di setup e **verifica SHA1/MD5** della ROM `SCUS-94570` (per garantire che entrambi gli sviluppatori usino gli stessi identici binari).
- [ ] Mappatura completa del contenuto estratto da `CRASHBSH.DAT` (overlay dei minigiochi, modelli, texture TIM/VRAM, banchi audio VAB/SEQ).

---

## Fase 1: Esplorazione Decompilazione & Scelta Strategia di Recompiling
> **Obiettivo:** Far girare la logica di gioco su PC x86_64.

*Crash Bash* è stato scritto in C con l'SDK Psy-Q e divide il codice tra l'eseguibile principale (`SCUS_945.70`) e vari **overlay** caricati dinamicamente per ogni minigioco/warp room.

- [ ] **Analisi dello stato di `mateusfavarin/crashbash`:**
  - Verificare quali file e sottosistemi in `decomp/` sono già stati tradotti in C leggibile.
  - Compilare e testare i programmi di ispezione presenti in `mateusfavarin/crashbash/tools/`.
- [ ] **Prototipo di Ricompilazione Statica / Ibrida:**
  - Per le parti non ancora decompilate a mano, configurare un traduttore **MIPS R3000A -> C/C++** (approccio *Static Recompilation*) guidato dai file dei simboli (`symbols/`) di `mateusfavarin`.
  - Unire le funzioni già decompilate in C pulito con quelle ricompilate staticamente.
- [ ] **Layer di Astrazione Psy-Q (`remaster/src/core/`):**
  - Scrivere le implementazioni PC ("shim") per le librerie base Sony: gestione memoria, matematica, caricamento file da disco (`libcd` reindirizzato ai file estratti su PC).

---

## Fase 2: Backend Grafico PC, 4K e Widescreen
> **Obiettivo:** Sostituire la GPU/GTE PS1 con un renderer moderno ad alta risoluzione.

- [ ] **Shim `libgte` & `libgpu` su PC (`remaster/src/renderer/`):**
  - Intercettare le liste di primitive grafiche PS1 (*Ordering Tables* - `OT`, poligoni `POLY_F3`, `POLY_FT4`, `POLY_G4`, sprite `SPRT`).
  - Tradurre le primitive in chiamate **SDL3 + OpenGL / Vulkan / Direct3D 11**.
- [ ] **Risoluzione 4K e Precisione Floating-Point:**
  - Sostituire i calcoli interi *fixed-point* della GTE con matrici *floating-point* a 32/64 bit per eliminare il tremolio dei poligoni (*PS1 vertex wobble*) e le distorsioni delle texture (*affine texture warping* -> *perspective-correct texturing*).
  - Scalare il framebuffer interno fino al **4K (3840x2160)** mantenendo l'UI nitida.
- [ ] **Supporto Widescreen (16:9 / 21:9):**
  - Espandere il frustum della telecamera nelle arene 3D senza deformare l'aspect ratio degli elementi 2D dell'interfaccia.
- [ ] **Framerate (30 -> 60+ FPS):**
  - Analizzare i timer della logica di gioco (originariamente legati al VSync a 60Hz NTSC / 30Hz per frame logico) e implementare l'interpolazione delle trasformazioni per il rendering a 60/120/144Hz senza accelerare la fisica dei minigiochi.

---

## Fase 3: Input Nativo (XInput & SteamInput) e Audio
> **Obiettivo:** Esperienza plug-and-play su PC e Steam per 1-4 giocatori.

- [ ] **Sottosistema Input (`remaster/src/input/`):**
  - Sostituire `libpad` / Multitap PS1 con **SDL3 Gamepad API** + **Steamworks SDK (`ISteamInput`)**.
  - Supporto nativo **XInput** (controller Xbox/PC standard) con hot-plugging (collegamento/scollegamento a caldo).
  - Rilevamento automatico dell'avvio tramite **Steam** per attivare **SteamInput** (Action Sets, vibrazione/rumble, supporto DualSense/Switch Pro/Steam Deck).
- [ ] **UI Prompts Dinamici:**
  - Mostrare a schermo le icone corrette dei pulsanti (Xbox, PlayStation, Steam Deck) in base al controller rilevato da XInput/SteamInput.
- [ ] **Sottosistema Audio (`remaster/src/audio/`):**
  - Intercettare le chiamate `libspu` per riprodurre gli effetti sonori ADPCM e i sintetizzatori musicali del gioco con qualità cristallina (senza il limite di campionamento compresso PS1).

---

## Fase 4: Multiplayer Locale e Remoto (Online)
> **Obiettivo:** Giocare in 4 sia sul divano che online a bassa latenza.

- [ ] **Multiplayer Locale 4P:**
  - Abilitare di default i 4 slot giocatore senza bisogno di configurare il Multitap virtuale.
- [ ] **Step 1 Online — Steam Remote Play Together:**
  - Grazie all'integrazione XInput/SteamInput e finestra nativa PC, abilitare il supporto immediato a *Steam Remote Play Together* (1 host, fino a 3 amici invitati via overlay Steam).
- [ ] **Step 2 Online — Rollback Netcode Nativo (`remaster/src/netcode/`):**
  - Isolare lo stato della memoria di gioco di una singola arena (poche decine di KB).
  - Creare funzioni veloci di `SaveState()` e `LoadState()` in memoria.
  - Integrare una libreria di **Rollback Netcode** (es. *GGPO* / *Backroll*) sopra **Steam Networking Sockets** (P2P con relay Valve gratuiti, niente porte da aprire sul router) per sincronizzare gli input dei 4 giocatori con zero input lag.

---

## Fase 5: Miglioramento Poligoni e Texture HD (Remaster+)
> **Obiettivo:** Modernizzare l'impatto visivo mantenendo lo stile artistico originale.

- [ ] **Reverse Engineering del formato Modelli/Arene di Crash Bash:**
  - Utilizzare i tool di `mateusfavarin/crashbash/tools/` per documentare ed esportare in `.obj` / `.gltf` tutti i modelli dei personaggi, casse, proiettili e arene estratti da `CRASHBSH.DAT`.
- [ ] **Custom Asset Replacement Loader:**
  - Creare nel renderer PC un sistema di *override*: quando il gioco carica un modello o una texture tramite il suo ID dal `.DAT`, il motore controlla se esiste una versione migliorata in `remaster/assets_hd/`.
- [ ] **Upgrading Graduale:**
  - **Personaggi principali:** Sostituzione delle mesh PS1 (pochi triangoli non saldati) con modelli high-poly dotati di skeletal/vertex animation morbida.
  - **Texture HD & Materiali:** Upscaling / ridisegno delle texture delle arene e aggiunta di illuminazione dinamica opzionale (attivabile/disattivabile con un tasto in stile *Halo Anniversary* / *Diablo II Resurrected* per passare in tempo reale da grafica PS1 a grafica Remaster+).


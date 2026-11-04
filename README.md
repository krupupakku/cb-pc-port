<p align="center">
  <img src="cb.png" alt="Crash Bash Logo" width="420"/>
</p>

# Crash Bash: Remaster+ (PC Native Port & Recompilation)

Progetto di **ricompilazione nativa per PC** e **Remaster+** di *Crash Bash* (PS1 - Eurocom, 2000).
L'obiettivo è trasformare l'originale per PlayStation 1 in un eseguibile nativo moderno per PC Windows/Steam, senza emulazione tradizionale, supportando risoluzioni fino al **4K**, framerate sbloccato, **XInput / SteamInput**, **Multiplayer Locale e Online** e una pipeline per il **miglioramento poligonale (High-Poly)** e texture HD.

---

## Obiettivi Principali

1. **Ricompilazione Nativa PC:** Esecuzione diretta del codice di gioco su architettura x86_64 tramite decompilazione C (`mateusfavarin/crashbash`) + ricompilazione statica MIPS e layer di astrazione per l'SDK Sony Psy-Q (`libgte`, `libgpu`, `libspu`, `libcd`, `libpad`).
2. **Grafica 4K & Widescreen:** Rendering moderno (OpenGL / Vulkan / D3D11 / SDL3), coordinate in virgola mobile per eliminare il *vertex wobble* della GTE PS1, supporto nativo 16:9 / 21:9 e 4K.
3. **Input Moderno Nativo:** Supporto nativo **XInput** e **SteamInput** (con icone dinamiche dei tasti e gestione fino a 4 controller senza configurazioni di emulatore).
4. **Multiplayer Locale & Remoto:**
   - **Locale:** Fino a 4 giocatori su schermo condiviso.
   - **Remoto:** Integrazione con *Steam Remote Play Together* (immediato) e architettura **Rollback Netcode** su *Steam Networking Sockets* per partite online a latenza zero.
5. **Asset Pipeline & High-Poly (Remaster+):** Caricamento custom degli asset estratti da `CRASHBSH.DAT` con possibilità di sostituire al volo modelli low-poly e texture 4-bit/8-bit con mesh ad alto numero di poligoni e texture HD.

---

## Struttura del Progetto

Il workspace è diviso nettamente in due macro-aree per mantenere separato il materiale di studio/originale dal nostro codice:

```text
cb/
├── original/                  # 1. MATERIALE ORIGINALE E REPOSITORY DI RIFERIMENTO
│   ├── disc/                  # [IGNORATO DA GIT] Immagine disco PS1 originale (SCUS-94570 / bash.bin)
│   ├── extracted/             # [IGNORATO DA GIT] Asset estratti da CRASHBSH.DAT
│   └── repos/                 # Repository GitHub clonati per studio, simboli e tool:
│       ├── crashbash/         # -> mateusfavarin/crashbash (Decompilazione & Simboli C)
│       ├── CrashBashEx/       # -> xan1242/CrashBashEx (Estrattore CRASHBSH.DAT)
│       └── Crash-Resources/   # -> Helias/Crash-Bandicoot-Resources (Documentazione formati)
│
├── remaster/                  # 2. IL NOSTRO CODICE NUOVO E MIGLIORATO (PC REMASTER+)
│   ├── src/
│   │   ├── core/              # Game loop, memoria e traduzione chiamate SDK Sony Psy-Q
│   │   ├── renderer/          # Backend grafico 4K, Widescreen, GTE float & High-Poly loader
│   │   ├── input/             # Astrazione gamepad: XInput + SteamInput API
│   │   ├── audio/             # Emulazione/sostituzione SPU PS1 e streaming tracce audio
│   │   └── netcode/           # Serializzazione stato RAM e Rollback Netcode / Steam P2P
│   ├── assets_hd/             # Nuovi modelli 3D high-poly, texture HD e shader
│   └── tools/                 # Script di build, verifica hash ROM, estrattori e convertitori custom
│
├── .gitignore                 # Protezione legale: blocca il commit di ROM/ISO/DAT protetti da copyright
├── README.md                  # Questo file
└── ROADMAP.md                 # Piano di sviluppo dettagliato per fasi e milestone
```

---

## Requisiti Legali & Setup Iniziale (Bring Your Own ROM)

Per motivi di copyright, **nessun file binario originale del gioco PS1 (`bash.bin`, `SCUS_945.70`, `CRASHBSH.DAT`) deve mai essere caricato su Git**.

1. Ogni sviluppatore deve possedere una copia legale di **Crash Bash (Versione NTSC-U - `SCUS-94570`)**, che è la versione di riferimento su cui sono mappati tutti gli indirizzi di memoria della decompilazione.
2. Posizionare il file immagine del gioco in `original/disc/bash.bin`.
3. Usare i tool in `original/repos/CrashBashEx` (o lo script di setup che creeremo in `remaster/tools/`) per estrarre `CRASHBSH.DAT` dentro `original/extracted/`.

---

## Riferimenti & Crediti Community

- **[mateusfavarin/crashbash](https://github.com/mateusfavarin/crashbash):** Decompilazione C e mappatura simboli di *Crash Bash (PSX)*.
- **[xan1242/CrashBashEx](https://github.com/xan1242/CrashBashEx):** Estrattore per l'archivio dati `CRASHBSH.DAT`.
- **[Helias/Crash-Bandicoot-Resources](https://github.com/Helias/Crash-Bandicoot-Resources):** Documentazione e risorse di reverse engineering sull'ecosistema *Crash Bandicoot*.


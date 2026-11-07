# Texture Remastered — Crash Bash Remaster+

In questa cartella puoi inserire le tue versioni **rimasterizzate in alta definizione (HD / 4K)** delle texture del gioco.

## Come Funziona (Approccio A — Real-time HD Replacement)

L'engine di **Crash Bash: Remaster+** carica ed applica le tue texture in tempo reale durante il rendering.
Non devi convertire nulla nei vecchi formati PS1 a 4-bit / 8-bit con palette ridotte: puoi usare normalissimi file **PNG a colori reali a 32-bit (RGBA)** a qualsiasi risoluzione!

### Struttura delle Cartelle

Per sostituire una texture, mantieni lo stesso percorso e nome che trovi in `textures/extracted/`:

```text
textures/
├── extracted/                  <-- Tutte le 15.165 texture originali estratte
│   ├── 000_tex/
│   │   ├── tex_0.png          (es. 64x64, 4-bit)
│   │   ├── tex_1.png
│   │   └── ...
│   ├── 002_tex/
│   │   └── ...
│   └── splashes/
│       ├── 136.png            (Schermata di caricamento / splash)
│       └── ...
│
└── remastered/                 <-- METTI QUI I TUOI FILE RIMASTERIZZATI
    ├── 000_tex/
    │   ├── tex_0.png          (es. 256x256 o 512x512 RGBA 32-bit!)
    │   └── ...
    └── splashes/
        └── 136.png            (es. 1920x1080 o 3840x2160 HD Splash Screen!)
```

### Regole Semplici:
1. **Risoluzione Libera**: Puoi creare versioni a **2x**, **4x**, **8x** o qualsiasi risoluzione desiderata (es. 128x128, 256x256, 1024x1024). Mantieni le stesse proporzioni (aspect ratio) della texture originale.
2. **Formato**: Salva in formato **PNG a 32-bit con canale Alfa (trasparenza)**.
3. **Sostituzione Graduale**: Puoi fare una sola texture alla volta! Le texture non presenti in questa cartella continueranno ad usare automaticamente la grafica originale senza alcun problema.
4. **Hot-Reload in Gioco**: Quando salvi o modifichi un file PNG, puoi premere **F7** in gioco (oppure selezionare *"Ricarica Texture"* nel menu Remaster+) per ricaricare istantaneamente le texture senza riavviare il gioco!


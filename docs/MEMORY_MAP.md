# Crash Bash (SCUS-94570) — RAM, VRAM & Overlay Memory Map

> **IMPORTANTE PER I FUTURI PROMPT**: Consulta **sempre** questo file (`docs/MEMORY_MAP.md`) e l'header C++ corrispondente (`remaster/src/core/cb_memory_map.h`) prima di effettuare scansioni o disassemblaggi dei binari PS1 (`SCUS_945.70`, `991.code.bin`, `722.type_0x69.bin`, ecc.).

---

## 1. Mappa Generale della RAM PS1 (`2 MB`: `0x80000000` – `0x801FFFFF`)

| Range Indirizzi RAM | Binario / Sorgente | Offset File | Descrizione |
| :--- | :--- | :--- | :--- |
| `0x80000000` – `0x8000FFFF` | BIOS / Kernel PS1 | — | Vettori eccezione, kernel PS1, tabelle A0/B0/C0 |
| `0x80010000` – `0x80059667` | `SCUS_945.70` (Codice + RODATA) | `0x000800` (`base = 0x8000F800`) | Eseguibile principale sempre residente in RAM (`ram_addr = file_off + 0x8000F800`) |
| `0x80059668` – `0x8005A550` | `SCUS_945.70` (Menu & String Tables) | `0x049E68` – `0x04AD50` | Tabella stringhe globali (`0x80059668`) e descrittori pagine Pause/Options Menu (`0x8005975C` – `0x8005A550`) |
| `0x8005A5E0` – `0x80078C8F` | `SCUS_945.70` (DATA / BSS) | `0x04ADE0`+ | Variabili globali motore, stato gioco/menu (`0x8005A614`), tabella glyph font (`0x80063EB8`) |
| `0x80078C90` – `0x800B32B3` | `991.code.bin` (Core Engine Overlay) | `0x000000` (`code @ 0x14 = 0x80078CA4`) | Overlay principale del motore di gioco, renderer dei menu (`0x800809A0`), logica Pause/Options (`0x8007FEBC`, `0x80080FBC`) |
| `0x800B32B4` – `0x800D7147` | Slot Overlay Dinamico (`722`, `487`, `716`–`728`) | `0x000000` (`data @ 0x04 = 0x800B32B8`) | Slot condiviso per l'overlay della modalità corrente: Main Menu (`722.type_0x69.bin`) oppure minigiochi/warp room (`487.bank.bin`, `716`..`728.bank.bin`) |
| `0x800D7148` – `0x801FFF00` | Heap Dinamico & Stack | — | Buffer asset caricati da CD (`CRASHBSH.DAT`), geometria, audio, stack (`0x801FFF00`) |

---

## 2. Tabella Completa degli Overlay di Codice (`0x80078C90` & `0x800B32B4`)

Tutti i file `.bank.bin` (`487`, `716`–`728`) e `722.type_0x69.bin` **non sono banchi audio**, bensì **overlay eseguibili MIPS (codice + stringhe + descrittori)** caricati tutti allo stesso indirizzo base **`0x800B32B4`** (i primi 4 byte contengono il `type_id`, quindi `offset 0x04` corrisponde a **`0x800B32B8`**):

| Asset File | Type ID | Base RAM (`off 0`) | Primo Codice MIPS (`f_off` -> `RAM`) | Ruolo / Modalità |
| :--- | :--- | :--- | :--- | :--- |
| `991.code.bin` | — | `0x80078C90` | `0x0014` -> `0x80078CA4` | **Core Gameplay & Menu Engine** (sempre presente dopo il boot) |
| `722.type_0x69.bin` | `0x69` | `0x800B32B4` | `0x0508` -> `0x800B37BC` | **Main Menu / Title Screen** (`SELECT GAME TYPE`, `OPTIONS`, `CHARACTER SELECT`, ecc.) |
| `728.bank.bin` | `0x5D` | `0x800B32B4` | `0x0784` -> `0x800B3A38` | Minigame Overlay `0x5D` |
| `727.bank.bin` | `0x5E` | `0x800B32B4` | `0x02F8` -> `0x800B35AC` | Minigame Overlay `0x5E` |
| `726.bank.bin` | `0x5F` | `0x800B32B4` | `0x0870` -> `0x800B3B24` | Minigame Overlay `0x5F` |
| `725.bank.bin` | `0x60` | `0x800B32B4` | `0x0554` -> `0x800B3808` | Minigame Overlay `0x60` |
| `724.bank.bin` | `0x61` | `0x800B32B4` | `0x0274` -> `0x800B3528` | Minigame Overlay `0x61` (Papu Pummel / Boss) |
| `487.bank.bin` | `0x62` | `0x800B32B4` | `0x048C` -> `0x800B3740` | **Warp Room / Adventure Hub** Overlay (`type 0x62`) |
| `723.bank.bin` | `0x63` | `0x800B32B4` | `0x01A8` -> `0x800B345C` | Minigame Overlay `0x63` (Polar Push) |
| `720.bank.bin` | `0x64` | `0x800B32B4` | `0x0174` -> `0x800B3428` | Minigame Overlay `0x64` (Pogo Padlock) |
| `719.bank.bin` | `0x65` | `0x800B32B4` | `0x0124` -> `0x800B33D8` | Minigame Overlay `0x65` (Crate Crush) |
| `718.bank.bin` | `0x66` | `0x800B32B4` | `0x00F0` -> `0x800B33A4` | Minigame Overlay `0x66` (Tank Wars) |
| `717.bank.bin` | `0x67` | `0x800B32B4` | `0x0368` -> `0x800B361C` | Minigame Overlay `0x67` (Crashball) |
| `716.bank.bin` | `0x68` | `0x800B32B4` | `0x0084` -> `0x800B3338` | Minigame Overlay `0x68` (Ring Ding / Medieval Mayhem) |
| `721.bank.bin` | `0x6A` | `0x800B32B4` | `0x13E0` -> `0x800B4694` | Minigame Overlay `0x6A` |

---

## 3. Variabili Globali & Stato del Gioco / Controller / Audio

| Indirizzo RAM | Tipo | Nome Simbolico | Descrizione |
| :--- | :--- | :--- | :--- |
| `0x80051338` | `PadSlot[4]` | `g_PadSlots` | Array di 4 slot controller (88 byte = `0x58` ciascuno). `+0x00`: ptr stato pad (`+0x0C` byte = vibration enable `0`/`1`), `+0x48` (`72`): bitmask tasti premuti in questo frame (`16-bit`) |
| `0x80051380` | `uint32_t` | `g_P1ButtonsTapped` | Bitmask tasti appena premuti (edge-triggered) da Player 1 (`0x0010`=Su, `0x0040`=Giù, `0x0080`=Sx, `0x0020`=Dx, `0x4000`=Cross/A, `0x1000`=Triangle/Y, `0x8000`=Square/X, `0x2000`=Circle/B) |
| `0x800515F8` | `uint32_t` | `g_MultitapPlayerCount` | Numero porte giocatore attive (default `4` se multitap o `< 5`) |
| `0x8005A604` | `int32_t` | `g_MusicVolumePct` | Volume musica `0`..`100` (step di `5`) |
| `0x8005A608` | `int32_t` | `g_SfxVolumePct` | Volume effetti sonori `0`..`100` (step di `5`) |
| `0x8005A60C` | `uint32_t` | `g_AudioMonoFlag` | `0` = `<STEREO>`, `1` = `< MONO >` |
| `0x8005A614` | `GameState` | `g_GameState` | Struttura principale stato partita/pausa |
| `0x8005A618` | `uint32_t` | `g_GameState.flags04` | Flag stato (`0x80010000` in partita) |
| `0x8005A624` | `uint32_t` | `g_PauseMenuBitmask` | (`+0x10` da `0x8005A614`) Bitmask pagina attiva nel Pause Menu: `0x0200` = Pausa attiva, `0x0020` = Root Pause (`CONTINUE`/`OPTIONS`/`QUIT`), `0x0100` = `OPTIONS` (`0x0108` con bit `0x08`), `0x0080` = Submenu |
| `0x8005A63A` | `uint8_t` | `g_ActiveControllerCount` | (`+0x26` da `0x8005A614`) Numero di controller attivi (`1`..`4`) |
| `0x8005A640` | `uint32_t` | `g_LanguageIndex` | (`+0x2C` da `0x8005A614`) Indice lingua (`0` = Inglese US) |
| `0x8005A646` | `uint16_t` | `g_GameModeFlags` | (`+0x32` da `0x8005A614`) Flag modalità partita |
| `0x8005A648` | `int16_t` | `g_CurrentLevelId` | (`+0x34` da `0x8005A614`) ID livello/stanza corrente (`0x20` = Warp Room) |
| `0x8005DDD0` | `void*` | `g_Overlay722Ptr` | Puntatore dati `722.type_0x69.bin` in RAM |
| `0x800655B8` | `uint32_t` | `g_MemCardPort` | Porta Memory Card attiva (`0x00` = Slot 1, `0x10` = Slot 2) |
| `0x800655C0` | `uint32_t` | `g_MemCardStatus` | Bitmask stato Memory Card: `0x02`=Presente, `0x04`=Non formattata, `0x08`=Corrotta, `0x10`=File `"BASCUS-94570"` valido, `0x20`=Spazio libero (`>= 8 KB`), `0x40`=Controllo in corso, `0x80`=Salvataggio/Formattazione in corso, `0x100`=Operazione fallita (`SAVE FAILED`) |
| `0x800655C4` | `uint32_t` | `g_MemCardCommand` | Comando richiesto alla state machine (`0` = nessuno, `1` = Salva `"BASCUS-94570"`, `2` = Formatta card) |
| `0x800655C8` | `uint8_t[8192]` | `g_MemCardBuffer` | Buffer di 1 blocco PS1 (`8192` byte): `+0x000..0x1FF` header `"SC"` + titolo `"Crash Bash"` + 3 icone; `+0x200..0x84F` (`0x800657C8`) 4 slot di salvataggio da `404` (`0x194`) byte; `+0x85C` (`0x80065E24`) checksum additivo a 32 bit di `[0x800657C8, 0x80065E18)` |
| `0x800675CC` | `uint32_t` | `g_MemCardStateStep` | Stato corrente (`0..7`) della state machine `0x8002C97C` (`SCUS_945.70`) |
| `0x80078800` | `uint32_t[8]` | `g_LibmcrdEventFlags` | Flag eventi BIOS `libmcrd`: `0x80078800..0x8007880C` = `SwCARD` (`0xF4000001`: IOE, ERROR, TIMEOUT, NEW); `0x80078810..0x8007881C` = `HwCARD` (`0xF0000011`: IOE, ERROR, TIMEOUT, NEW) |
| `0x8009AEE8` | `uint32_t` | `g_PauseOptionsSubPage` | Sotto-pagina di `OPTIONS` in Pausa: `0` = `OPTIONS` (`SOUND`/`CONTROLLER`/`EXIT`), `1` = `SOUND`, `2` = `CONTROLLER` |
| `0x8009AEEC` | `uint32_t` | `g_PauseMenuCursorIdx` | Indice voce selezionata (`0, 1, 2, ...`) nel menu di Pausa corrente |
| `0x8009AEF0` | `uint32_t` | `g_PauseMenuPlayerPort` | Indice porta controller (`0`..`3`) che controlla il menu di Pausa |
| `0x8009AF00` | `uint32_t` | `g_MenuRenderHideFlag` | Se `!= 0`, nasconde il testo e decrementa a fine frame |
| `0x8009BD18` | `int16_t` | `g_MenuBoxOriginY` | Coordinata base Y del box del menu di Pausa (es. `0x0070` = `112`) |
| `0x8009BD1C` | `uint16_t[8]` | `g_MenuDynamicState` | Array di stati dinamici per gli item con `type` `16..18`, `25`, `32` (es. `STEREO`/`MONO` e `ON`/`OFF` vibrazione P1..P4) |

---

## 4. Funzioni Chiave (Menu, Testo, Audio, UI)

| Indirizzo RAM | Binario (`Offset`) | Firma / Parametri | Descrizione |
| :--- | :--- | :--- | :--- |
| `0x800243A0` | `SCUS_945.70` (`0x14BA0`) | `void DrawMenuText(int16_t x, int16_t y, const char* str, uint32_t flags, uint32_t colorTop, uint32_t colorBot)` | **Funzione universale di rendering testo/font di Crash Bash**. Se `x == -32768` (`0x8000`), centra automaticamente la stringa orizzontalmente! `flags`: `0` = grande pulsante (se selezionato), `1` = piccolo centrato, `5` = grande statico, `8` = piccolo |
| `0x800248A0` | `SCUS_945.70` (`0x150A0`) | `void DrawVolumeBar(...)` | Disegna la barra del volume in `SOUND OPTIONS` |
| `0x80022660` | `SCUS_945.70` (`0x12E60`) | `void PlayMenuSfx(int sfxId, ...)` | Riproduce effetti sonori UI (`400`/`0x190` = cursore, `402`/`0x192` = conferma Cross, `405`/`0x195` o `900`/`0x384` = indietro Triangle) |
| `0x8007EFC0` | `991.code.bin` (`0x06330`) | `int MenuHandleUpDownCross(int port, int maxIdx)` | Gestisce Su/Giù su `0x8009AEEC` (`0..maxIdx`) e restituisce `1` se viene premuto Cross (`X` PS1 / `A` Xbox) |
| `0x8007FEBC` | `991.code.bin` (`0x0722C`) | `void PauseOptionsInput Tick()` | Gestisce l'input del menu `OPTIONS` in Pausa (`0x800802D8` = pagina principale `OPTIONS`, `0x8007FEE0` = `SOUND`, `0x800801B0` = `CONTROLLER`) |
| `0x800809A0` | `991.code.bin` (`0x07D10`) | `void ProcessAndDrawMenuPage(MenuPageDesc* page, int16_t baseY)` | Disegna il box wireframe verde e tutte le voci di un descrittore `MenuPageDesc` in Pausa |
| `0x80080FBC` | `991.code.bin` (`0x0832C`) | `void PauseMenuRenderTick()` | Dispatcher di rendering del menu di Pausa (`0x80081280` smista `OPTIONS` in base a `0x8009AEE8`) |
| `0x80095BEC` | `991.code.bin` (`0x1CF5C`) | `void MainMenuSetWidgetPage(int slot, WidgetDesc* list)` | Imposta la lista di widget attiva nel Main Menu (`slot = 4` per `OPTIONS` / `SOUND` / `CONTROLLER`) |
| `0x800B4910` | `722.type_0x69.bin` (`0x165C`) | `void MainMenuOptionsInputTick()` | Gestisce l'input del menu `OPTIONS` nel Main Menu (`0x800B950C` = subPage `0/1/2`, `0x800B9508` = cursor `0..2`) |
| `0x800B4F1C` | `722.type_0x69.bin` (`0x1C68`) | `void MainMenuOptionsUpdateTick()` | Aggiorna lo stato visivo dei widget di `OPTIONS` / `SOUND` / `CONTROLLER` nel Main Menu |

---

## 5. Struttura dei Descrittori Menu

### 5.1 Pause Menu (`SCUS_945.70`: `0x80059668` – `0x8005A550`)
* **String Table (`0x80059668`, puntata da `0x80059748[0..4]`)**:
  * `[11]` = `"PAUSED"` (`0x800594E0`)
  * `[12]` = `"CONTINUE"` (`0x800594E8`)
  * `[13]` = `"OPTIONS"` (`0x800594F4`)
  * `[15]` = `"QUIT GAME"` (`0x80059508`)
  * `[35]` = `"SOUND"` (`0x800595EC`)
  * `[36]` = `"AUDIO MODE"` (`0x800595F4`)
  * `[37]` = `"<STEREO>"` (`0x80059600`)
  * `[38]` = `"< MONO >"` (`0x8005960C`)
  * `[39]` = `"MUSIC VOLUME"` (`0x80059618`)
  * `[40]` = `"FX VOLUME"` (`0x80059628`)
  * `[41]` = `"EXIT"` (`0x80059634`)
  * `[42]` = `"CONTROLLER"` (`0x8005963C`)
  * `[43]` = `"VIBRATION"` (`0x80059648`)
  * `[44]` = `"ON"` (`0x80059654`)
  * `[45]` = `"OFF"` (`0x80059658`)
* **Formato `MenuPageDesc` (24 byte per entry = `0x18`)**:
  * **Entry 0 (Header Box, 24 byte)**:
    * `int16_t type` (`2` o `7` = disegna box wireframe verde)
    * `int16_t y_off` (`-10` = `0xFFF6`)
    * `int16_t half_w` (es. `234` = `0x00EA`)
    * `int16_t height` (es. `172` = `0x00AC` per 3 voci; `212` = `0x00D4` per 4 voci)
    * `uint32_t w2 = 0, w3 = 0, w4 = 0x00008080, w5 = 0`
  * **Entry 1..N (Voci Menu, 24 byte ciascuna)**:
    * `int16_t type`:
      * `0` = Voce selezionabile con font grande (usa `g_PauseMenuCursorIdx == selectable_item_counter`)
      * `5` = Titolo/testo statico con font grande
      * `8` = Testo statico con font piccolo
      * `-1` (`0xFFFF`) = Terminatore fine lista
    * `int16_t x`: `-32768` (`0x8000`) = centrato orizzontalmente
    * `int16_t y_rel`: offset Y relativo a `g_MenuBoxOriginY` (`10` = titolo, `70` = voce 0, `110` = voce 1, `150` = voce 2, `190` = voce 3)
    * `int16_t str_idx`: indice nella tabella stringhe `0x80059668`
    * `uint32_t color_normTop` (`0x0000D0FF`), `color_normBot` (`0x000000FF`)
    * `uint32_t color_selTop` (`0x00FFFFFF`), `color_selBot` (`0x0000FFFF`)
  * **Indirizzi Descrittori Pause Menu**:
    * `0x8005975C` / `0x8005981C` / `0x800598DC`: Pagine `PAUSED` (`CONTINUE`, `OPTIONS`, `QUIT`)
    * `0x80059C9C`: Pagina `OPTIONS` (`OPTIONS` titolo, `SOUND`, `CONTROLLER`, `EXIT`)
    * `0x80059D2C`: Pagina `SOUND`
    * `0x80059E4C`: Pagina `CONTROLLER`

### 5.2 Main Menu (`722.type_0x69.bin`, Base RAM `0x800B32B4`)
* **Variabili Stato `OPTIONS` nel Main Menu**:
  * `0x800B9508`: Indice voce selezionata (`0` = `SOUND`, `1` = `CONTROLLER`, `2` = `EXIT`)
  * `0x800B950C`: Sotto-pagina attiva (`0` = `OPTIONS` root, `1` = `SOUND`, `2` = `CONTROLLER`)
* **Descrittori Widget Main Menu (36 byte = `0x24` per widget)**:
  * `0x800B8F20` (`f_off 0x5C6C`): Pagina `OPTIONS` nel Main Menu
  * `0x800B901C` (`f_off 0x5D68`): Pagina `SOUND` nel Main Menu
  * `0x800B91CC` (`f_off 0x5F18`): Pagina `CONTROLLER` nel Main Menu
* **Tabella Stili Widget Main Menu (`0x8005689C` in `SCUS_945.70`, 28 byte per stile)**:
  * L'offset `+0x7C` (`+124`) dell'istanza widget (inizializzato da `w6` nel descrittore da 36 byte) è l'indice nella tabella `0x8005689C` (valutata da `0x8001C448` ad ogni frame, che sovrascrive `widget+120` `font_size`, `widget+112` `colTop` e `widget+116` `colBot`):
    * `Style 0`: `font0=0` (Large), `col1=0x0000D0FF`, `col2=0x000000FF`, `mode=0` (Arancio/Rosso normale)
    * `Style 2`: `font0=0` (Large), `col1=0x0000D0FF`, `col2=0x000000FF`, `mode=2` (Evidenziato giallo pulsante)
    * `Style 4`: `font0=1` (Compact), `col1=0x0000D0FF`, `col2=0x000000FF`, `mode=0` (Compatto Arancio/Rosso normale)
    * `Style 8`: `font0=1` (Compact), configurato dal Remaster+ con `mode=2, font1=1` per il font compatto giallo pulsante.
* **Pause Menu `OPTIONS` Root Input (`0x800802D8` in `991.code.bin`)**:
  * `0x800802DC` contiene `jal 0x8007EFC0` (`MenuHandleUpDownCross`) e non va sovrascritto; il suo argomento `maxIdx` è nel delay slot a `0x800802E0` (`addiu $a1, $zero, 2` -> `3`), mentre l'indice di `EXIT` è a `0x800802F0` (`addiu $v0, $zero, 2` -> `3`).

---

## 6. Font & Icone Tasti PS1 / Xbox / Tastiera

* **Tabella Glifi Font (`0x80063EB8` in `SCUS_945.70`)**:
  * Struttura di 6 byte per glifo: `uint8_t u, v, width, height; int8_t x_off, y_off`.
* **Codici Carattere Speciali per i Tasti nelle Stringhe**:
  * `0xFC` (`\xfc`) = **Cross** (PS1 `X` / Xbox `A` / Tastiera `Z`) — `u=64..95, v=96..117`
  * `0xFD` (`\xfd`) = **Circle** (PS1 `O` / Xbox `B` / Tastiera `X`) — `u=160..191, v=96..117`
  * `0xFE` (`\xfe`) = **Triangle** (PS1 `△` / Xbox `Y` / Tastiera `D`) — `u=96..127, v=96..117`
  * `0xFF` (`\xff`) = **Square** (PS1 `□` / Xbox `X` / Tastiera `S`) — `u=128..159, v=96..117`
  * *(Nota: Nel texture page del font `tpage=(1, 0, 640, 0)`, `u=128..159` è Square/Xbox X e `u=160..191` è Circle/Xbox B).*


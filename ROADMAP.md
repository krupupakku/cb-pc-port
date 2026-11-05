# Development Roadmap — Crash Bash: Remaster+

This roadmap outlines the technical roadmap for bringing *Crash Bash* from its original PS1 architecture (MIPS R3000A + Sony Psy-Q SDK) into a modern native PC port with enhanced fidelity and features.

---

## Phase 0: Environment Setup, Research & Asset Pipeline
> **Objective:** Establish a reproducible build system, extract assets from the original disc, and understand Eurocom's executable and overlay architecture.

- [x] Establish workspace directory structure (`original/` vs `remaster/`) and `.gitignore` protection for game dumps.
- [x] Clone and evaluate research repositories:
  - `mateusfavarin/crashbash` (decompilation coverage and `symbols/` analysis).
  - `xan1242/CrashBashEx` (data archive unpacker evaluation).
  - `Helias/Crash-Bandicoot-Resources` (file format specifications).
- [x] Implement native asset extraction tool (`cb_extract`) for `CRASHBSH.DAT` and ISO hash verification (`SCUS-94570`).
- [x] Map all extracted archive contents (minigame code overlays, 3D world meshes, VRAM/TIM textures, VAB/SEQ soundbanks).

---

## Phase 1: Architecture Selection & Execution Core
> **Objective:** Execute gameplay logic natively on x86_64 PC hardware.

*Crash Bash* was written in C using the Sony Psy-Q SDK and splits its code between a main binary (`SCUS_945.70`) and dynamically swapped minigame/warp room overlays.

- [x] **Evaluation of Decompilation Coverage (`mateusfavarin/crashbash`):**
  - Determined ~15-20% C coverage; a pure decompilation re-write would require years before a playable state.
- [x] **Hybrid Execution / Static Recompiler Core:**
  - High-performance MIPS R3000A interpreter with integrated GTE coprocessor (fixed-point arithmetic and 3D coordinate transformations).
  - Dynamic loading and execution of all minigame binary overlays.
- [x] **Sony Psy-Q HLE Shim Layer (`remaster/src/core/`):**
  - High-level emulation for system memory, DMA controller, VSync interrupts, hardware timers, and CD-ROM streaming (`libcd` redirected to local `CRASHBSH.DAT` data).

---

## Phase 2: Modern 4K Graphics Backend & 3D Widescreen
> **Objective:** Replace the original PS1 GPU/GTE with a modern, high-resolution rendering pipeline.

- [x] **Native PC OpenGL Renderer (`remaster/src/renderer/`):**
  - Win32 native windowing with modern OpenGL context.
  - Software-based, sub-pixel accurate PS1 polygon rasterizer with dynamic display centering.
  - Proper aspect ratio correction (4:3 centering without zoom clipping).
  - Corrected GTE instruction bugs (fixed `OP` matrix calculations and color flags to resolve distortions on trophy, gem, and crystal models).
- [x] **Internal Resolution Scaling (1x Native, 2x HD, 3x Super HD, 4x Ultra HD 4K):**
  - Sub-pixel geometry rasterization at increased internal resolutions (`1024x480` at 2x, `1536x720` at 3x, `2048x960` at 4x).
  - Multi-threaded horizontal-band rasterizer powered by a persistent thread pool (zero per-frame thread allocation overhead).
  - Exact 64-bit barycentric interpolation eliminating gouraud banding, color wrapping, and underflow artifacts.
  - Precise texel sampling preventing spurious outlines and atlas bleed on HUD fonts and 2D elements.
  - Optional 3D Bilinear Texture Filtering (toggle with `F8`).
  - Real-time internal resolution cycling with hotkey `F7` (1x $\to$ 2x $\to$ 3x $\to$ 4x).
- [x] **True 3D Widescreen Hack (DuckStation Style):**
  - Extended GTE Field of View (FOV) rendering 33% more 3D world geometry on lateral borders.
  - 3D models retain exact geometry proportions without anamorphic stretching (`0.75 * 1.3333 = 1.0`).
  - 2D interface elements (HUD, portraits, scores, pause menus) retain proper positioning and aspect ratio.
  - Real-time aspect ratio switching with hotkey `F5` (16:9 Widescreen $\leftrightarrow$ 4:3 Original).
- [x] **Post-Processing GLSL Shader Pipeline:**
  - Sharp-Bilinear Pixel-Perfect scaling for crisp 4K presentation.
  - Authentic CRT Trinitron scanline emulation.
  - Real-time shader cycling with hotkey `F6`, Borderless Fullscreen via `F11` or `Alt + Enter`.
- [x] **Framerate & Performance Stability:**
  - Optimized Release `-O3` profile maintaining solid 60 FPS across all minigames and warp rooms.

---

## Phase 3: Native Input, DualShock Rumble & Audio
> **Objective:** Seamless plug-and-play experience on PC for 1 to 4 players.

- [x] **Native Input Subsystem (`remaster/src/input/`):**
  - Full XInput support for up to 4 simultaneous gamepads, alongside keyboard fallback for Player 1.
  - Native analog stick input mapped to PS1 DualShock controller protocol (ID `0x73`).
- [x] **Force Feedback & DualShock Vibration:**
  - Hardware rumble support via XInput for connected gamepads.
  - Dual-motor actuator emulation (high-frequency small motor buzz, low-frequency large motor heavy rumble).
  - Direct integration with Eurocom's internal game actuator state (`_controllers` array at `0x80051338`, stride `0x58`) and BIOS `PadSetAct` (`0x800363AC`).
  - Full synchronization with the in-game Options menu toggle ("Vibration: ON / OFF").
- [ ] **Dynamic On-Screen Button Prompts:**
  - Display context-aware Xbox / PlayStation glyphs based on connected input device.
- [x] **Audio Subsystem (`remaster/src/audio/` & `remaster/src/core/spu.cpp`):**
  - 24-channel SPU ADPCM sound synthesis and voice mixing.
  - Hardware Timer 2 calibrated to 60Hz to prevent audio desynchronization and sluggish playback.
  - 44.1 kHz stereo audio streaming output via native Windows WaveOut.

---

## Phase 4: Local & Online Multiplayer
> **Objective:** Enable 4-player gameplay locally and online with low latency.

- [x] **4-Player Local Multiplayer:**
  - Logical multitap implementation on Port 1 supporting 4 distinct player controller buffers.
- [ ] **Step 1: Steam Remote Play Together:**
  - Out-of-the-box shared-screen multiplayer via XInput and borderless fullscreen display.
- [ ] **Step 2: Deterministic Rollback Netcode (`remaster/src/netcode/`):**
  - Core `RollbackManager` architecture.
  - Fast in-memory state snapshots (`SaveState()` / `LoadState()`) for zero-latency rollback frames.
  - Peer-to-peer input packet synchronization over UDP / Steam Networking Sockets.

---

## Phase 5: High-Poly Meshes & HD Textures (Remaster+)
> **Objective:** Enhance visual fidelity while staying faithful to the original art direction.

- [x] **Model & Level Geometry Extraction:**
  - Dedicated `cb_model2obj` tool to unpack and export original 3D meshes with UV mappings into `.obj`.
- [ ] **Custom Asset Override System:**
  - Dynamic filesystem loader allowing higher-polygon 3D meshes and HD textures to replace low-poly assets at runtime.
- [ ] **Asset Remastering:**
  - Community-driven replacement of original character models (Crash, Cortex, Tiny, Dingodile, etc.) with modern high-polygon equivalents.

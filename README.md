# Selfano

[![CI](https://github.com/tuasananh/selfano/actions/workflows/ci.yml/badge.svg)](https://github.com/tuasananh/selfano/actions/workflows/ci.yml)

**Offline, zero-latency, cross-platform C++23 piano sight-reading, harmony, ear training, and repertoire practice trainer.**

Selfano runs natively on **Linux, macOS, and Windows** (SDL3 + Dear ImGui + `libremidi`) and offline in a local browser via **Emscripten WebAssembly** (WebGL via SDL3 + WebMIDI + WebAudio + IndexedDB `IDBFS`). All theory, sight-reading, accompaniment matching, SM-2 spaced repetition, ear training, and audio synthesis work 100% locally with no network calls or accounts.

---

## Daily 30-Minute Autopilot Practice Routine

Launch **Start Today's Practice (30:00 Autopilot)** from the Home Dashboard (or jump directly to any module via **Free Practice**):

| Stage | Duration | Focus |
| :--- | :--- | :--- |
| **1. Key & Harmony Warm-Up** | `05:00` | 24-key classical/anime weekly cycle; 1-2 octave scales (Harmonic/Natural minor toggle) and `I -> IV -> V -> I` cadences with Green/Red Grand Staff feedback. |
| **2A. Adaptive Flashcards** | `03:00` | Landmark anchors, melodic/harmonic intervals (2nds-octaves), and triad inversions. Weighted sampler prioritizes shapes with `< 85%` accuracy or `> 1500 ms` EMA reaction time. |
| **2B. Continuous Etudes** | `07:00` | 2-system rolling lookahead procedural 4/4 sight-reading (`Quarter`, `Half`, `Whole` notes & rests), **Steady-Pulse** metronome (`40-120 BPM`, 4-beat count-in, `±40%` beat window) + **Wait-for-Note** toggle, and RH / LH / Easy Hands-Together modes. |
| **3. Companion Mode** | `10:00` | Flexible rolling-window harmonic verifier for chords & slash chords (`C`, `Dm`, `Bb`, `F#m`, `Cmaj7`, `Dm/F`), ignoring octaves/inversions and tolerating passing tones so eyes stay on paper sheet music. |
| **4A. SRS Repertoire Deck** | `02:30` | 3-button SM-2 (`Again` / `Hard` / `Good`) play-from-memory scheduler with CRUD section management and optional chord roadmap hint peek. |
| **4B. Ear Trainer** | `02:30` | Procedural in-key tonic anchor + 3-tier melodies (Steps, 3rds, 5ths) with note-by-note feedback, directional half-step hints, `Space` / Soft Pedal (`CC 67`) replay, and staff reveal on completion. |

No hardware keyboard nearby? Use the toggleable **Virtual Piano** dock or **QWERTY keyboard** (`A..'` white/black keys, `Z`/`X` octave shift, `Space` replay/play-pause) backed by Selfano's built-in polyphonic **SDL3 AudioSynth**.

---

## Architecture

```text
selfano (src/main.cpp)
  ├── piano_ui   (src/ui/)   -> Dear ImGui + SDL3 + procedural Grand Staff & Bravura SMuFL glyphs
  ├── piano_midi (src/midi/) -> Bidirectional libremidi / WebMIDI + lock-free SPSC queue + SDL3 AudioSynth
  └── piano_core (src/core/) -> Pure C++23 core (zero SDL, ImGui, libremidi, or Emscripten dependencies)
```

- All third-party dependencies (`SDL3 release-3.4.20`, `imgui v1.93.0`, `libremidi v5.4.3`, `nlohmann/json v3.12.0`, `Catch2 v3.16.1`) are managed via CMake `FetchContent`.
- **Persistence:** `profile.json` is saved atomically to `SDL_GetPrefPath("selfano", "selfano")` on desktop (overridable via `--profile <path>`), and mounted under `/persist/profile.json` via Emscripten `IDBFS` (IndexedDB) in browser builds.

---

## Build & Run (Native Desktop: Linux / macOS / Windows)

### Prerequisites
- CMake `3.25+`
- Ninja (recommended for CMake Presets) or platform C++23 build generator
- C++23 compiler (GCC 13+, Clang 16+, or MSVC 19.36+)
- On Linux: standard audio/video development headers (ALSA/PipeWire/PulseAudio, Wayland/X11, Mesa) for SDL3 and `libremidi`

### Configure, Build & Test

```bash
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
```

For an optimized release build:

```bash
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
```

### VS Code with clangd

Open the repository root in VS Code and use the `llvm-vs-code-extensions.vscode-clangd`
extension with `clangd` on your `PATH`. Run `cmake --preset native-debug` from the
repository root to generate `build/native-debug/compile_commands.json`. The
project's `.clangd` file points to that directory for C++23 flags and include
paths. If diagnostics from before configuration are still visible, run
**clangd: Restart language server** from the Command Palette.

### Run

```bash
./build/native-debug/selfano
```

Optional CLI flags:
- `--profile /path/to/profile.json` — override the default `SDL_GetPrefPath` storage file.
- `--tab <home|autopilot|warmup|flashcards|etude|companion|srs|ear|sandbox|showcase>` — open directly into a specific view.

---

## Build & Run (Emscripten WebGUI — Offline Browser)

### Prerequisites
- Emscripten SDK (`emcc` / `emcmake`) installed and activated in `PATH`
- Chromium or Google Chrome (WebMIDI works natively; all other WebGL2 browsers support Virtual Piano, QWERTY, WebAudio, and `IDBFS` persistence)

### Configure & Build

```bash
emcmake cmake --preset web-release
cmake --build --preset web-release
```

This generates `selfano.html`, `selfano.js`, `selfano.wasm`, and `selfano.data` (with `assets/fonts/Bravura.otf` preloaded) in `build/web-release/`.

### Serve Locally

Browsers require HTTP(S) (even localhost) for WebAssembly streaming and WebMIDI:

```bash
python3 -m http.server 8089 --directory build/web-release
```

Open `http://localhost:8089/selfano.html` in Chromium/Chrome. Click or press any key once to unlock browser WebAudio playback. Progress and SRS state automatically persist in the browser's IndexedDB (`/persist/profile.json`).

---

## Korg LP-380 Hardware MIDI Setup (5-Pin DIN to USB)

Because a 5-pin DIN MIDI-to-USB adapter carries separate directional cables, connect them **crossed** (`IN <-> OUT`):

```text
+-------------------+                              +---------------------------+
|    Korg LP-380    |                              |    Computer / Browser     |
|                   |                              |        (Selfano)          |
|  [5-Pin MIDI OUT] |======( USB Cable "IN"  )====>|  MIDI IN  (notes played)  |
|  [5-Pin MIDI IN ] |<=====( USB Cable "OUT" )=====|  MIDI OUT (ear/prompts)   |
+-------------------+                              +---------------------------+
```

1. **Label check:** Plug the connector labeled **MIDI IN** on your USB adapter into **MIDI OUT** on the bottom-rear jack panel of the LP-380, and the connector labeled **MIDI OUT** into **MIDI IN** on the LP-380.
2. **Avoid double-sounded notes on LP-380 speakers:** Keep **Monitor MIDI IN** **OFF** in Selfano's top MIDI bar when listening directly to the LP-380's built-in speakers/headphones. Turn **Monitor MIDI IN** ON only if LP-380 Local Control is disabled or speakers are muted.
3. **Controls & feel:**
   - **Soft Pedal (`CC 67` left pedal)** triggers prompt replay (Warm-Up / Flashcards / Ear Trainer) and bar/phrase reset in Companion Mode (with a 200 ms debounce).
   - **Chord Window (`80-120 ms`):** Adjust in the top bar if rolled left/right hand chords split prematurely.
   - **Panic:** Click **Panic** (or switch stages) to send All Notes Off (`CC 123`) to hardware MIDI OUT and silence the software synth immediately.

---

## Releases & GitHub Pages

Selfano uses GitHub Actions for continuous integration ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) and automated multi-platform releases ([`.github/workflows/release.yml`](.github/workflows/release.yml)).

### One-Time Repository Setup
To enable automated WebAssembly hosting on GitHub Pages (HTTPS, required for WebMIDI):
1. Open **Settings -> Pages -> Build and deployment** in your GitHub repository.
2. Set **Source** to **GitHub Actions**.

### Cutting a Release
Push a version tag matching `v*` (tags containing `-` such as `v0.2.0-rc.1` are marked as prereleases and do not overwrite the live GitHub Pages site):

```bash
git tag v0.1.0
git push origin v0.1.0
```

The release workflow builds, tests, checksums (`SHA256SUMS.txt`), and attaches:
- `selfano-<tag>-linux-x64.tar.gz` and `selfano-<tag>-linux-arm64.tar.gz`
- `selfano-<tag>-macos-arm64.zip` and `selfano-<tag>-macos-x64.zip` (`Selfano.app` bundle)
- `selfano-<tag>-windows-x64.zip`
- `selfano-<tag>-web.zip` (also deployed to GitHub Pages on stable tags)

> **macOS note:** Release `.app` bundles are ad-hoc signed. If Gatekeeper blocks launch on first open, run `xattr -cr Selfano.app` in Terminal or right-click -> **Open**.

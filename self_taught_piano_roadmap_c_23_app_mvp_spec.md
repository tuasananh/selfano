# Self-Taught Piano Roadmap & C++23 App MVP Spec

## 1. Learner Profile & Diagnostic Summary

### Hardware & Environment
* **Instrument:** Korg LP-380 (88-key weighted hammer-action digital piano; pre-2021 model equipped with 5-pin DIN `MIDI IN` and `MIDI OUT` ports).
* **Required Adapter:** Standard bidirectional 5-pin DIN MIDI-to-USB interface cable:
  * Connect Cable `IN` $\rightarrow$ Piano `MIDI OUT` (captures your keystrokes).
  * Connect Cable `OUT` $\rightarrow$ Piano `MIDI IN` (lets the app play ear-training prompts directly through the Korg LP-380's built-in speakers).
* **Schedule:** Minimum $30\text{ minutes/day}$ on weekdays and weekends (flexible around university coursework and exam deadlines).
* **Background:** Self-taught CS student; plays classical and anime arrangements (previously memorized HalcyonMusic's *Jane Doe* and *Gate of Steiner* piano version).

### Root Causes of Current Bottlenecks
1. **Note-by-Note Decoding ($\approx 5\text{ seconds}$ per note):**
   * *Symptom:* Reading music by translating individual dots and verifying uncertain notes against YouTube or Synthesia recordings.
   * *Underlying Pattern:* Steps of $1\text{--}2$ half-steps are recognized instantly (tracking melodic contour), but jumps of $3$ or more half-steps and vertical chords trigger slow line/space counting.
2. **Pure Muscle-Memory Retention:**
   * *Symptom:* Completely forgetting older pieces once active repetition stops.
   * *Underlying Pattern:* Pieces are stored as raw motor choreography rather than harmonic progressions (chords and key centers), leaving no structural framework to reconstruct the music.
3. **Repertoire-Only Practice:**
   * *Symptom:* Jumping directly into advanced arrangements without daily foundational reading, ear training, or key-familiarity drills.

---

## 2. Piano Learning Priorities & 30-Minute Daily Routine

### Skill Priority Order
1. **Visual Interval & Chord Shape Recognition:** Replace single-note counting with **anchor notes** (Treble G4, Middle C4, Bass F3) plus geometric shapes (2nds = line-to-space steps, 3rds = line-to-line skips, root triads = 3-note "snowmen").
2. **High-Volume Easy Sight-Reading:** Build automatic eye-to-finger reflexes on material well below maximum technical difficulty.
3. **Harmonic Mapping for Long-Term Memory:** Analyze chord progressions before memorizing notes so pieces are retained structurally.
4. **Classical Technique, Ear Training & Advanced Repertoire:** Layer relaxed mechanics and ear-to-key fluency onto a solid reading foundation.

### The 30-Minute Daily Practice Split
* **$00:00\text{ -- }05:00$ | Block 1 — Key & Harmony Warm-Up:**
  * Focus on **one key per week** (e.g., C major, A minor, G major, E minor, D major, B minor).
  * Play the $1\text{--}2$ octave scale and primary triads ($I \rightarrow IV \rightarrow V \rightarrow I$) hands-together to map the key's black/white key topography.
* **$05:00\text{ -- }15:00$ | Block 2 — High-Volume Sight-Reading (No Stopping):**
  * Warm up with rapid landmark/interval/triad flashcards, then read brand-new, easy procedural etudes in the weekly key.
  * Maintain a slow, steady pulse; never stop to correct mistakes; read strictly by interval shapes and landmarks.
* **$15:00\text{ -- }25:00$ | Block 3 — Harmonic Mapping & Repertoire:**
  * Work on a target classical or anime piece in 4-bar phrases.
  * Write chord symbols (e.g., `Dm`, `Bb`, `F`, `C`) above the measures on your sheet music before hands-on practice to anchor muscle memory to harmony.
* **$25:00\text{ -- }30:00$ | Block 4 — Retention SRS & Ear Check:**
  * Recall a due section of a previously learned piece from memory using its chord map, then complete short ear-training melodic/chord playback prompts.

---

## 3. App Vision & Architecture (Offline Multi-Platform C++23 App)

### Core UX Goal: Zero-Decision-Fatigue Autopilot
When you open the app and click **"Start Today's Practice"**, the app automatically builds today's 30-minute session based on your current weekly key, adaptive sight-reading weaknesses, active repertoire chunks, and due spaced-repetition items—guiding you sequentially from Block 1 through Block 4 without requiring you to decide what to practice next.

### Architectural Goals
* **100% Offline & Zero-Latency:** No cloud dependencies, accounts, or network latency between pressing a key on the Korg LP-380 and receiving visual feedback.
* **Cross-Platform Desktop + WebGUI (Native + Emscripten Pattern):**
  * **Language Standard:** Modern **C++23** (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`) using `std::expected`, `std::span`, `std::format`, and `constexpr` music theory tables.
  * **GUI & Platform Layer:** **Dear ImGui + SDL2/SDL3** — renders immediate-mode UI natively on **Linux, macOS, and Windows**, and compiles straight to **WebAssembly via Emscripten (`emcmake cmake`)** for a local browser WebGUI from a single codebase.
  * **Sheet Music Rendering:** Custom procedural vector rendering using `ImDrawList` (drawing crisp 5-line staves, ledger lines, clefs, accidentals, and noteheads at $\ge 60\text{ FPS}$ without external image assets).
  * **Bidirectional MIDI Backend:** **`libremidi`** — unified C++20/23 wrapper supporting ALSA (Linux), CoreMIDI (macOS), WinMM/WinRT (Windows), and WebMIDI in Emscripten for both `midi_in` (reading keystrokes) and `midi_out` (sending ear-training notes to the Korg LP-380's speakers).
  * **Concurrency Model:** Thread-safe SPSC ring buffer transferring timestamped MIDI messages from `libremidi`'s OS hardware callback thread to the main SDL/ImGui render thread.
  * **Build & Dependency Management:** **CMake + `FetchContent`** fetching `SDL`, `imgui`, `libremidi`, `nlohmann/json` (offline stats, repertoire, and SRS persistence), and `Catch2` (unit tests) directly from source with zero OS package-manager friction.

### Recommended CMake Project Layout
```text
piano-trainer/
├── CMakeLists.txt             # Root CMake config (C++23, FetchContent deps, Native + Emscripten targets)
├── src/
│   ├── main.cpp               # SDL + Dear ImGui main loop & frame lifecycle
│   ├── core/                  # Pure C++23 theory engine, procedural generator, chord matcher, SRS & session state machine
│   ├── midi/                  # Bidirectional libremidi wrapper (IN + OUT) + SPSC queue + QWERTY/synth fallback
│   └── ui/                    # ImDrawList procedural staff renderer, virtual piano, Guided Autopilot runner UI
└── tests/
    └── core_tests.cpp         # Catch2 unit tests for intervals, triads, procedural etudes, and chord matching
```

---

## 4. MVP Requirements (For Antigravity Implementation)

### Feature 1: Bidirectional MIDI & Virtual Fallback Engine (`piano_midi`)
* **Device Management:** Auto-detect connected MIDI `IN` and `OUT` ports with UI dropdown selectors and hot-reconnect support.
* **Thread-Safe Event Bridge:** Push `MidiNoteEvent { uint8_t note; uint8_t velocity; bool is_note_on; double timestamp_ms; }` from the `libremidi` input callback thread into a lock-free SPSC queue drained once per UI frame.
* **Polyphonic Chord Window:** Group simultaneous `NoteOn` events arriving within an $80\text{ ms} \text{ to } 120\text{ ms}$ tolerance window so slightly rolled human chords still register cleanly.
* **Hardware `MIDI OUT` + Software Audio Fallback:** Send `NoteOn`/`NoteOff` messages out to the Korg LP-380 for ear training; if no MIDI output hardware is connected, synthesize a lightweight SDL audio sine/triangle tone so ear training still works offline on a laptop.
* **Virtual Keyboard Input:** Include an interactive on-screen piano (`ImDrawList`) and QWERTY key-mapping mode for hardware-free testing.

### Feature 2: Procedural Staff Renderer (`piano_ui`)
* Render **Treble Clef**, **Bass Clef**, and **Grand Staff** views dynamically via `ImDrawList`.
* Accurately draw key signatures, ledger lines (e.g., Middle C4, high A5, low E2), accidentals, barlines, and stacked harmonic noteheads (horizontally offsetting adjacent 2nds so noteheads do not overlap).
* Color-code feedback immediately upon key press:
  * **Neutral:** Pending target notes.
  * **Green:** Correctly played notes.
  * **Red:** Incorrect notes played (displaying both the target note name and the played note name).

### Feature 3: Guided 30-Minute Daily Autopilot Session (`piano_core` + `piano_ui`)
Clicking **"Start Today's Practice"** walks the user automatically through 4 sequential stages (with manual skip/repeat controls):

1. **Stage 1 ($05:00$) — Procedural Key & Harmony Warm-Up:**
   * Automatically selects the active weekly key (rotating weekly through common classical/anime keys).
   * Procedurally renders the $1\text{--}2$ octave scale and primary triad cadence ($I \rightarrow IV \rightarrow V \rightarrow I$) in Treble and Bass clefs, advancing automatically as the user plays each step accurately via MIDI.
2. **Stage 2 ($10:00$) — Procedural Sight-Reading & Shape Trainer (MVP):**
   * *Part A ($3\text{ min}$) — Adaptive Flashcards:* Landmark anchors, 2-note interval shapes, and 3-note triad inversions ("snowmen") weighted toward shapes where accuracy is $< 85\%$ or reaction time exceeds $1500\text{ ms}$.
   * *Part B ($7\text{ min}$) — Procedural Continuous Etudes:* Generates endless 4-to-8-bar sight-reading passages constrained to the weekly key and target interval jumps so the user practices reading continuously without stopping.
3. **Stage 3 ($10:00$) — Repertoire Harmonic Mapping (Companion Mode):**
   * User selects their active piece and defines 4-bar chord progression roadmaps (e.g., *Gate of Steiner — Phrase A*: `Dm -> Bb -> F -> C`).
   * **Zero Screen-Switching Workflow:** User keeps their eyes strictly on their external PDF or printed sheet music while the app runs the 10-minute chunk timer, passively listens via MIDI to verify that played pitch-classes match the target bar's chord harmony, and plays a subtle confirmation chime upon completing clean reps.
4. **Stage 4 ($05:00$) — Retention SRS & Procedural Ear Check:**
   * *Part A ($2.5\text{ min}$) — Repertoire Retention Deck (SRS):* User logs learned piece sections (e.g., *Gate of Steiner — Intro*, *Jane Doe — Chorus*); the app schedules due sections using spaced repetition for play-from-memory recall without sheet music and logs pass/needs-review ratings.
   * *Part B ($2.5\text{ min}$) — Procedural `MIDI OUT` Ear Training:*
     1. Hides the staff and plays the weekly key's tonic chord (e.g., D–F–A) through the Korg LP-380's speakers to anchor the ear.
     2. Plays a procedurally generated 3-to-5 note melody (scaling from stepwise motion to 3rds/5ths) over `MIDI OUT` (`Spacebar` or left soft pedal `CC 67` triggers instant replay).
     3. Evaluates user playback note-by-note, offering directional hints on mistakes (e.g., *"Target is $1$ half-step higher"*) and revealing the played phrase on the visual staff once matched.

---

## 5. Agent Implementation Guardrails & Roadmap Phases

### Guardrails for Antigravity
* **Keep `piano_core` 100% Hardware & UI-Agnostic:** Zero `#include <imgui.h>`, `#include <SDL.h>`, or `#include <libremidi/...>` inside `src/core/`; verify all music theory, procedural etude generation, interval/triad math, chord pitch-class matching, and SRS scheduling with `Catch2` + `ctest`.
* **Enforce Warning-Free Modern C++23:** Enable `-Wall -Wextra -Wpedantic` (GCC/Clang) or `/W4` (MSVC); avoid raw owning `new`/`delete` pointers; prefer `std::span`, `std::array`, and `std::expected`.
* **Persist All User Data Locally:** Store daily practice logs, reaction-time telemetry, weekly key rotation, repertoire chord maps, and SRS retention decks in a human-readable local `profile.json` file (`nlohmann/json`) with Emscripten IndexedDB/localStorage sync on Web builds.

### Build Order (Vertical Slices)
1. **Slice 1 (Build & Staff Rendering):** Configure `CMakeLists.txt` with `FetchContent` (`SDL`, `imgui`, `libremidi`, `nlohmann_json`, `Catch2`) and render a static Grand Staff with key signatures and stacked triads.
2. **Slice 2 (MIDI + Virtual Input & Core Drills):** Wire up bidirectional `libremidi` (`IN` + `OUT`), SPSC event queue, QWERTY/virtual piano fallback, and the procedural landmark/interval/triad drills with `std::chrono` reaction-time telemetry.
3. **Slice 3 (Guided 4-Stage Autopilot Session):** Implement the unified **"Start Today's Practice"** state machine integrating Stage 1 (procedural scale/cadence), Stage 2 (procedural continuous etudes), Stage 3 (Companion Mode chord-roadmap listener), and Stage 4 (SRS retention deck + `MIDI OUT` ear trainer) with `profile.json` persistence.
4. **Slice 4 (WebGUI Packaging):** Verify cross-compilation via `emcmake cmake` so the exact same C++23 app runs offline in a browser via WebMIDI.

### Planned v2 Enhancements (Post-MVP)
* **Block 2 Hybrid Expansion:** Add a lightweight **MusicXML / Standard MIDI File (`.mid`) importer** so public-domain beginner collections (e.g., *Czerny Op. 599*, *Bach's Anna Magdalena Notebook*, *Burgmüller Op. 100*) can be mixed alongside procedural sight-reading etudes.
* **Block 3 Bar-by-Bar Score Verification:** Add an optional in-app MIDI/MusicXML score view for repertoire chunks that validates exact bar-by-bar note execution in addition to Companion Mode.
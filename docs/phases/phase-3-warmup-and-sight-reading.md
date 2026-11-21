# Phase 3: Stage 1 (Key & Harmony Warm-Up) & Stage 2 (Flashcards + Continuous Etudes)

**Goal:** Implement the first 15 minutes of the daily routine—weekly key rotation, procedural scale/cadence warm-up, adaptive shape flashcards with reaction-time telemetry, and procedural continuous sight-reading etudes on the Grand Staff.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## Deliverables & Vertical Slices

### Slice 3.1 — Weekly Key Rotation & Stage 1 Warm-Up Engine (`src/core/` via `cpp23-tdd`)
- Ordered weekly key cycle starting with common classical/anime keys (`C Maj`, `A Min`, `G Maj`, `E Min`, `D Maj`, `B Min`, `F Maj`, `D Min`, `A Maj`, `F# Min`, `Bb Maj`, `G Min`, `Eb Maj`, `C Min`, ...).
- Procedural **1–2 octave scale** generator (ascending & descending, Treble + Bass clefs, hands-together or separate) and **primary triad cadence** (`I -> IV -> V -> I`) generator in the active weekly key.
- Step-by-step evaluator advancing automatically on accurate MIDI/virtual input with per-step Green/Red feedback.

### Slice 3.2 — Stage 2A Adaptive Flashcard Engine (`src/core/` via `cpp23-tdd`)
- Prompt bank covering:
  - **Landmark anchors** (Treble G4, Middle C4, Bass F3 + adjacent octaves)
  - **2-note interval shapes** (2nds, 3rds, 4ths, 5ths, 6ths, octaves — melodic & harmonic)
  - **3-note triad inversions** ("snowmen": root, 1st, 2nd inversions in Treble & Bass)
- Deterministic weighted sampler prioritizing shapes with accuracy `< 85%` or average reaction time `> 1500 ms` (using injected timestamps in core, fed from `std::chrono::steady_clock` at the UI boundary).
- Per-shape telemetry struct (`attempts`, `correct`, `ema_reaction_ms`) ready for Phase 5 JSON persistence.

### Slice 3.3 — Stage 2B Procedural Continuous Etude Engine (`src/core/` via `cpp23-tdd`)
- Seedable procedural generator creating endless 4-to-8-bar sight-reading passages constrained to:
  - Active weekly key signature
  - Configurable difficulty / max interval jump (steps -> 3rds -> 5ths -> chord skips)
  - Selectable **Hand Mode**: Right Hand (Treble), Left Hand (Bass), or **Easy Hands-Together** (stepwise/small-skip RH melody + whole/half-note root/5th/triad LH accompaniment).
- Dual timing controller:
  - **Steady-Pulse Mode (Default):** Moving playhead driven by BPM metronome (`40–120 BPM`) with optional audible click; missed beats mark Red and playback keeps advancing without stopping ("never stop to correct mistakes").
  - **Wait-for-Note Toggle:** Pauses playhead on current beat until correct note(s) are played.

### Slice 3.4 — Stage 1 & Stage 2 Practice UI (`src/ui/`)
- Stage 1 view: Weekly key badge, scale/cadence progress bar, Grand Staff with live step cursor.
- Stage 2A view: Rapid flashcard staff, live RT (ms) + accuracy readout, weakest-shapes mini-summary.
- Stage 2B view: Multi-bar Grand Staff with scrolling/advancing playhead, BPM slider, Hand Mode selector (`RH` / `LH` / `Hands-Together`), Pulse vs. Wait-for-Note toggle, and metronome mute toggle.

---

## Exit Gate (Verification Criteria)

1. Warning-free build and 100% passing `ctest` verifying:
   - Scale & `I-IV-V-I` cadence correctness across major and minor keys.
   - Adaptive flashcard weighting strongly favors `< 85%` accuracy and `> 1500 ms` RT shapes.
   - Procedural etudes stay strictly within weekly key, pitch range, and interval constraints for RH, LH, and Hands-Together.
   - Both Steady-Pulse (non-stopping) and Wait-for-Note state transitions behave deterministically.
2. `src/core/` purity check passes.
3. Manual play-through confirms smooth auto-advancing warm-up, adaptive flashcards with live RT telemetry, and continuous Grand Staff sight-reading etudes.

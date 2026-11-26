# Phase 16: Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal Sensing)

**Status:** `[x] Complete` (Depends on Phase 14 & Phase 15)  
**Goal:** Elevate Selfano from pitch-and-rhythm verification to genuine pianistic coaching by analyzing MIDI velocity balance (melody projection vs. accompaniment), sustained/tied note hold duration (`NoteOff`), and damper pedal (`CC 64`) cleanliness—surfaced as calm, non-punitive feedback that never penalizes non-velocity virtual/QWERTY keyboards.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `midi-emscripten-interop`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Completes the Pianist's Loop

Every digital piano (including Korg LP-380) already transmits per-note velocity, exact `NoteOff` release timestamps, and `CC 64` damper pedal changes over MIDI. Without inspecting them, a student can bang a left-hand accompaniment at `vel=115`, whisper the melody at `vel=40`, chop tied whole notes short, or smear the sustain pedal across chord changes—and still earn `100% Mastered`. Surfacing gentle, non-blocking musicality coaching turns every run into a real tone-production lesson.

---

## 2. Deliverables & Vertical Slices

### Slice 16.1 — Lock-Free Velocity, `NoteOff` & `CC 64` Propagation (`src/midi/midi_event.hpp`, `chord_grouper.*`, `midi_engine_*.cpp`)
- Extend `PlayedChord` (`src/midi/chord_grouper.hpp`) with `std::vector<std::uint8_t> velocities` (parallel to `notes`, keeping existing `notes` API intact).
- Parse and forward `CC 64` (Damper Pedal: `value >= 64` down, `< 64` up) through the existing lock-free `SpscQueue<MidiNoteEvent, 1024>` without any allocation, logging, or locking inside MIDI callbacks (native `libremidi` + Emscripten WebMIDI).
- Forward `NoteOff` events and `CC 64` pedal state transitions into `LessonEngine` during active runs.

### Slice 16.2 — Pure Core `MusicalityMetrics` Evaluator (`src/core/lesson_engine.*`)
- Add non-punitive `MusicalityMetrics` to `LessonRunSummary`:
  - **Voicing Balance (Hardware MIDI only):** Compare average RH melody velocity vs. LH accompaniment velocity (and top-note soprano velocity vs. inner chord tones on homophonic chord lessons like `t5_02`). Active only when `from_hardware == true` and velocity variance `> 0`.
  - **Sustain & Tie-Hold Rate:** Track whether sustained notes (`duration >= Half` or `tie_start`) remain held for `>= 65%` of their written duration via finger hold or active `CC 64` damper pedal.
  - **Pedal Cleanliness (when `PedalSpan` exists and `CC 64` was used):** Detect whether damper pedal lifts/refreshes cleanly on harmonic changes instead of holding across clashing chord boundaries.
- Mastery graduation (`>= 90%` accuracy at target BPM) remains governed by pitch/rhythm accuracy so musicality cues stay encouraging and never block virtual/QWERTY learners.

### Slice 16.3 — Musicality Coach Line in Post-Run UI (`src/ui/lesson_view.cpp`, `src/app/app_controller.cpp`)
- Add a clean, non-intrusive **Musicality Row** inside the Phase 14 Post-Run Coach Strip:
  - Example positive cue: *"Voicing: Melody +14 vel ✓ · Sustained Ties: 94% ✓ · Clean Pedal ✓"*
  - Example constructive tip: *"Tip: Soften LH accompaniment so the melody sings · Hold tied notes longer"*
- Gracefully omit velocity/pedal metrics when playing via QWERTY or mouse Virtual Piano (displaying only Sustain/Tie Hold when meaningful).

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` passes 100% with 0 warnings, including deterministic Catch2 tests for `PlayedChord` velocities, `CC 64` parsing, Voicing Balance math, Tie-Hold ratio (finger vs. pedal), Pedal Cleanliness detection, and QWERTY/virtual-piano graceful fallback.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with 0 warnings (including WebMIDI `CC 64` forwarding).
3. `src/core/` purity check returns 0 matches; zero allocations/locks added inside MIDI callbacks; `clang-format --dry-run --Werror` clean.
4. Visual Light & Dark screenshots verify the Post-Run Musicality Coach row; manual hardware MIDI spot-check confirms live velocity balance and damper pedal sensing on a physical digital piano.

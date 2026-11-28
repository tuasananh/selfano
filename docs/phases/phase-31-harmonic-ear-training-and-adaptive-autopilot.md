# Phase 31: Harmonic Ear Training & Tier-Adaptive Autopilot (Stage 4B + Shell)

**Status:** `[x] Complete` (Depends on Phase 21 & Phase 30 — Track C: Daily Drills & MIDI Technique Diagnostics)  
**Goal:** Upgrade Stage 4B (`EarTrainer`) with **Chord-Quality Recognition** and **Harmonic Progression Dictation** (crucial for hearing and memorizing Chopin structurally), and upgrade `AutopilotSession` with **tier-adaptive time allocation** and selectable **`30 / 45 / 60 min`** practice profiles.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Memorizing a 9-minute Romantic piece like *Ballade No. 1* depends far more on **harmonic hearing** (instantly hearing the difference between `Gm`, `D7`, `F#dim7`, `A7/C#`, and `Ab/C` Neapolitan) than on 3-note stepwise diatonic melodies. At the same time, a rigid 30-minute Autopilot that always gives `10 min` to beginner sight-reading and only `7–10 min` to active repertoire slows down intermediate learners once their flashcard reaction time drops below `900 ms`.

---

## 2. Deliverables & Vertical Slices

### Slice 31.1 — Harmonic Modes in `EarTrainer` (`src/core/ear_trainer.hpp/.cpp`, `tests/ear_trainer_tests.cpp`)
- Add `enum class EarDrillMode : std::uint8_t { Melody = 0, ChordQuality = 1, Progression = 2 };` to `EarTrainer`:
  - **`ChordQuality`:** Plays the tonic anchor + a target chord (`Major`, `Minor`, `Dominant 7th`, `Diminished 7th`, `Half-Diminished m7b5`, `Neapolitan bII6`). Learner answers via clickable quality pills OR by playing the chord on MIDI; reveals spelled chord on Grand Staff + Roman label.
  - **`Progression`:** Plays tonic anchor + a 3-to-4 chord functional progression in the weekly key (e.g., `i -> iv -> V7 -> i`, `i -> bII6 -> V7 -> i`, `I -> V7/V -> V -> I`). Learner plays back the bass/chord progression on MIDI (pitch-class matching ignoring octave) or identifies the Roman numerals, then reveals on staff.
- Persist `EarDrillMode` preference backward-compatibly in `profile.json` v1.
- Expand `tests/ear_trainer_tests.cpp` covering chord-quality generation, MIDI + label grading, progression matching, and replay scheduling.

### Slice 31.2 — Tier-Adaptive `AutopilotSession` & Duration Presets (`src/core/autopilot.hpp/.cpp`, `src/core/profile_store.hpp/.cpp`, `tests/autopilot_tests.cpp`)
- Add `enum class AutopilotDurationPreset : std::uint8_t { Standard30 = 0, Focused45 = 1, Conservatory60 = 2 };` and `build_autopilot_schedule(AutopilotDurationPreset preset, int current_tier, double avg_flashcard_rt_ms)` returning a dynamic `std::array<AutopilotBlockSpec, 6>`:
  - **Beginners (Tiers 1–2 or Flashcard RT `>= 1100 ms`):** Keeps the classic reading-heavy split (`5m WarmUp / 3m Flash / 7m Etude / 10m Lesson / 2.5m SRS / 2.5m Ear` at 30m).
  - **Intermediate–Advanced (Tiers 3–10 & Flashcard RT `< 950 ms`):** Tapers beginner Flashcards/Etudes (`1.5m / 3.5m`) and reallocates those minutes straight into **Stage 3 Guided Lessons & Speed Lab (`15m` in 30m mode, `24m` in 45m mode, `34m` in 60m mode)** plus **Harmonic Ear Training**.
- Keep all existing Autopilot controls (Pause, Skip, Repeat, Safe Boundary guard, Transition banner, Recap modal) 100% intact.

### Slice 31.3 — Ear Trainer & Home Dashboard UI Wiring (`src/ui/ear_trainer_view.hpp/.cpp`, `src/ui/home_dashboard.cpp`, `src/ui/autopilot_chrome.cpp`, `src/app/app_controller.cpp`)
- Add the `Melody | Chords | Progressions` segmented toggle in `EarTrainerView` with clean quality answer pills and instant replay (`Space` / `CC 67`).
- Add a subtle `30m | 45m | 60m` duration selector pill next to `"Start Today's Practice"` on `HomeDashboard`, displaying the live adaptive block split summary underneath.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (all 3 ear modes, adaptive Autopilot schedules for `30/45/60m` across tiers, and `profile.json` round-trip).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots of `--tab ear` (Chord Quality & Progression modes) and `--tab home` (`30m | 45m | 60m` adaptive preview) verify clean Apple HIG layout with zero clipping.

### Verification Evidence
- `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
- `ctest --preset native-debug --output-on-failure`: 139/139 passed (`ear_trainer_tests`, `autopilot_tests`, `profile_store_tests`, and full suite).
- `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings.
- `clang-format --dry-run --Werror`: clean.
- Core purity check (`grep -rnE --include='*.hpp' --include='*.cpp' '#include\s*[<"](imgui|SDL|libremidi|emscripten)' src/core/`): 0 matches.
- Headless Light & Dark screenshots verified (`--tab ear --ear-mode chords`, `--tab ear --ear-mode progressions`, `--tab home --autopilot-preset 45`, `--tab home --autopilot-preset 60`).

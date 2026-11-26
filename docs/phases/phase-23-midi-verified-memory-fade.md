# Phase 23: MIDI-Verified Memory Fade Mode (Unified Stage 3 + Stage 4A)

**Status:** `[ ] Planned` (Depends on Phase 20 — Track A: Learning Speed Multipliers)  
**Goal:** Replace fragile muscle-memory memorization with **MIDI-verified Memory Fade** directly inside `LessonEngine` and `StaffRenderer`—progressively hiding noteheads behind **Harmony-Only Maps** (chords + Roman numerals + barlines) and then a **Blank Staff**, feeding verified recall accuracy straight into SM-2 retention scheduling.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Selfano's MVP spec identifies **pure muscle-memory retention** (forgetting pieces once repetition stops) as Root Cause #2. Currently `Stage 4A` (`SrsDeck`) relies on manual self-rating cards with no MIDI verification. By letting the learner play any mastered or in-progress phrase with noteheads hidden—first guided only by the **harmonic roadmap** (`Gm -> Ab/C -> Gm/D -> D7 -> Gm`), then completely blind, with gentle peek-on-mistake reveal—the brain is forced to retrieve notes from harmonic structure rather than visual reading or raw finger reflexes.

---

## 2. Deliverables & Vertical Slices

### Slice 23.1 — Core `MemoryFadeLevel` & Peek-on-Mistake Tracking (`src/core/lesson_engine.hpp/.cpp`, `src/core/lesson_progress.cpp`, `src/core/profile_store.cpp`)
- Add `enum class MemoryFadeLevel : std::uint8_t { FullScore = 0, HarmonyMapOnly = 1, BlankMemory = 2 };` to `LessonConfig`.
- Track `memory_peek_count` and `memory_verified_clean` (`bool memory_map_clean`, `bool memory_blank_clean`) in `LessonProgress` (backward-compatible `profile.json` v1 persistence).
- In `LessonEngine`:
  - Pitch/rhythm verification stays identical to `WaitForNote` / `SteadyPulse`, but when a target is missed in `HarmonyMapOnly` or `BlankMemory`, mark that target for temporary visual reveal ("peek") and increment `memory_peek_count` in `LessonRunSummary`.
  - Compute an automatic **Suggested SM-2 Rating** from recall performance (`0` peeks & `>= 92%` -> `Good`, `<= 2` peeks & `>= 80%` -> `Hard`, otherwise `Again`), updating `LessonProgress` review intervals directly on completion of a guided memory review.
- Add Catch2 tests in `tests/lesson_engine_tests.cpp` and `tests/profile_store_tests.cpp`.

### Slice 23.2 — Selective Notehead Hiding & Peek Reveal in `StaffRenderer` (`src/ui/staff_renderer.hpp/.cpp`, `src/ui/lesson_staff_adapter.cpp`, `tests/staff_renderer_tests.cpp`)
- Add `MemoryFadeLevel` visibility controls to `StaffRenderOptions`:
  - `FullScore`: normal engraving.
  - `HarmonyMapOnly`: draw grand staff, clefs, key/time signatures, barlines, phrase measure numbers, playhead, **chord symbols & Roman numerals**, and subtle rhythm beat markers—hide noteheads/stems/beams/fingerings EXCEPT for already-played targets and any currently peeked missed target.
  - `BlankMemory`: hide both noteheads and harmonic labels (showing only clean empty bars, playhead, and live Green/Red feedback on played/missed beats).
- Ensure layout stability: hidden notes reserve the exact same horizontal spacing as `FullScore` so revealing a peeked note never causes layout jitter.
- Add `tests/staff_renderer_tests.cpp` regression tests verifying stable bar widths and visibility across all 3 fade levels.

### Slice 23.3 — Memory Mode Controls & Stage 4A Deep-Link (`src/ui/lesson_view.hpp/.cpp`, `src/ui/srs_view.hpp/.cpp`, `src/app/app_controller.cpp`)
- Add a clean **Memory Fade** 3-way segmented control (`Score | Chords Only | Memory`) in `Practice settings` and an active chip (`Memory: Chords Only  ×`) in the Practice strip.
- Add a **"Recall from Memory (MIDI)"** launch button on Stage 4A SRS cards linked to built-in/imported lessons (and on Mastered lessons in the Guided Next-Step flow), auto-applying the verified SM-2 rating after the recall run.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including peek counting, SM-2 rating suggestion, `profile.json` round-trip, and layout stability tests).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified C++ files.
4. Headless Light & Dark screenshots on `t5_02` and `t6_04` across `HarmonyMapOnly` and `BlankMemory` (including a peeked missed target) confirm crisp visual hierarchy and zero layout shift.

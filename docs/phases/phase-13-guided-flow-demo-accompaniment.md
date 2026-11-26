# Phase 13: Guided Learning Flow, Animated Demo & Play-Along Accompaniment

**Status:** `[x] Complete` (Depends on Phase 11; integrates smoothly with Phase 12 `brief_read`)  
**Goal:** Replace the unsequenced "15-button cockpit" with a clear teacher-guided practice flow, add a visually animated Demo mode (moving playhead + lighting Virtual Piano keys + Stop toggle), add **Auto-Play Other Hand** accompaniment in `RH`/`LH` practice, and track granular per-phrase & hands-separate progress.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `midi-emscripten-interop`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Today, a beginner opening `LessonView` sees `Skeleton | Wait | Pulse`, `RH | LH | Both`, `60% | 80% | 100%`, `Phrase A..D`, `Demo`, `Pulse`, `Reset`, and `Options` with no suggested sequence. Furthermore:
- `Demo` plays audio blind (no playhead tracking, no piano key lights, no Stop button).
- Practicing `RH` alone in `SteadyPulse` plays in dead silence instead of letting the learner hear the LH waltz/Alberti accompaniment.
- `LessonProgress` only credits full-piece `HandsTogether` `SteadyPulse >= 90%`, giving learners zero recorded progress while building up phrases or hands separately.

---

## 2. Deliverables & Vertical Slices

### Slice 13.1 — Animated Demo Transport & Auto-Play Other Hand (`src/core/lesson_engine.*`)
- Add `LessonPlaybackState::DemoPlaying` with `start_demo()` and `stop_demo()`:
  - Advances `elapsed_ticks_` at the selected tempo ladder BPM over the active slice (phrase or full piece).
  - Exposes `active_demo_midi_notes()` for live Virtual Piano highlighting and returns newly triggered `LessonPlaybackNote` events from `update(dt_ms)` so audio and playhead stay sample-tight and stoppable instantaneously.
- Add `bool play_other_hand_accomp{true}` to `LessonConfig`:
  - When `practice_mode == SteadyPulse` and `hand_mode` is `RightHand` or `LeftHand`, `LessonEngine::update(dt_ms)` emits timed accompaniment notes for the inactive hand at softer velocity (`~64`), synchronized with the count-in and moving playhead.

### Slice 13.2 — Per-Phrase Progress & Guided Next-Step Planner (`src/core/lesson_engine.hpp`, `lesson_progress.cpp`, `profile_store.*`)
- Extend `LessonProgress` (100% backward-compatible `profile.json` `v1`):
  - `bool skeleton_clean{false}`
  - `std::uint32_t phrase_rh_mask{0}`
  - `std::uint32_t phrase_lh_mask{0}`
  - `std::uint32_t phrase_both_mask{0}`
- Update `apply_run_to_progress` to record clean `ChordSkeleton` passes (`>= 85%`) and clean per-phrase `WaitForNote` / `SteadyPulse` passes (`>= 90%`) in the corresponding bitmask.
- Add pure planner `suggest_next_practice_step(const LessonScore&, const LessonProgress&)`:
  - Returns a concrete `GuidedPracticeStep` (`label`, `rationale`, `target_config`, `open_learn_brief`) progressing naturally: `1. Read & Listen` → `2. Chord Skeleton` → `3. Phrase A..N (RH → LH → Both)` → `4. Full Piece 60% → 80% → 100%`.

### Slice 13.3 — Guided Stepper Strip, Animated Demo & `Accomp` UI (`src/ui/lesson_view.*`, `src/app/app_controller.cpp`)
- Wire `Demo` ↔ `Stop Demo` toggle with live moving playhead on the staff and highlighted target/demo keys on the Virtual Piano dock.
- Wire `LessonTickResult::playback_notes` from `LessonView::update(...)` into `AppController`'s outbound MIDI / `AudioSynth` path (with clean `panic_all_notes_off` on Stop/Pause/Stage switch).
- Add an **`Accomp`** toggle pill inside the scaffold bar (shown when `RH` or `LH` is selected in `SteadyPulse`).
- Add a sleek **Guided Next-Step Strip** above the staff showing the 4-milestone progress track (`Brief · Skeleton · Phrases · Ladder`), green check indicators on completed Phrase pills, and a one-click **"Next Step"** button that configures mode/hand/phrase/tempo automatically while keeping all manual controls accessible.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` passes 100% with 0 warnings, including new Catch2 tests for `DemoPlaying` transport, `play_other_hand_accomp` event timing, per-phrase bitmasks, `suggest_next_practice_step`, and `profile.json` backward-compatible round-trips.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with 0 warnings.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots and `--profile` persistence round-trips verify the Guided Next-Step Strip, phrase checkmarks, `Accomp` toggle, and live Demo playhead/piano highlights with zero layout clipping.

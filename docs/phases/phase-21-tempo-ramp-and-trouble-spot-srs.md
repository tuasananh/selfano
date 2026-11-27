# Phase 21: Micro-Increment Tempo Ramp & Trouble-Spot Micro-SRS

**Status:** `[x] Complete` (Depends on Phase 20 — Track A: Learning Speed Multipliers)  
**Goal:** Eliminate the steep `60% -> 80% -> 100%` speed cliff by adding an automatic **Micro-Increment Tempo Ramp** (`+4 BPM` after 2 clean loops, `-4 BPM` on slips) to `LessonEngine`, and automatically capture detected `TroubleSpot` bar ranges into a lightweight next-day **Trouble-Spot Micro-SRS** queue so learners drill weak 2-bar seams via spaced interleaving.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Jumping from `80%` to `100%` tempo on dense passages (e.g., `88 BPM -> 112 BPM` in Burgmüller's *Arabesque*, or `128 BPM -> 160 BPM` in Chopin) causes tension and sloppy motor memory. Meanwhile, learners who finish a practice session with a weak 2-bar seam rarely remember to revisit those exact bars first thing tomorrow. Automating `+4 BPM` micro-ramps and next-day trouble-spot recall applies two of the strongest motor-learning multipliers (micro-progressive overload + spaced interleaving) across every existing and imported lesson.

---

## 2. Deliverables & Vertical Slices

### Slice 21.1 — Core Tempo Ramp State Machine & Trouble-Spot Queue (`src/core/lesson_engine.hpp/.cpp`, `src/core/lesson_progress.cpp`, `src/core/profile_store.hpp/.cpp`)
- Extend `LessonConfig` (`src/core/lesson_engine.hpp`) with `bool auto_tempo_ramp{false};` and `double ramp_step_bpm{4.0};` (clamped `[2.0, 8.0]`).
- Inside `LessonEngine` (`src/core/lesson_engine.cpp`), when `config_.practice_mode == LessonPracticeMode::SteadyPulse && config_.loop_phrase && config_.auto_tempo_ramp`:
  - Track consecutive clean loops (`>= 95%` accuracy, `0` misses). After 2 consecutive clean loops, step `custom_bpm` up by `+ramp_step_bpm` (capped at `score_.meta.target_bpm`, switching `tempo_step = TempoLadderStep::Custom`) and emit a `tempo_ramped_delta` on `LessonTickResult`.
  - If a loop finishes `< 80%` accuracy or `>= 2` misses, step down by `-ramp_step_bpm` (floored at `50%` of `target_bpm`, `>= 40 BPM`) so the learner stays in the clean-execution zone.
- Add `struct TroubleSpotCard` (`lesson_id`, `start_bar_index`, `end_bar_index`, `target_bpm`, `current_clean_bpm`, SM-2 `interval_days`/`ease_factor`/`repetitions`/`due_epoch_day`) in `src/core/lesson_engine.hpp` (or `src/core/srs_deck.hpp`) and persist `std::vector<TroubleSpotCard> trouble_spot_queue;` backward-compatibly in `profile_store.hpp/.cpp` (`profile.json` v1).
- Add pure helpers: `enqueue_or_update_trouble_spot(...)` (deduplicating overlapping bar windows per lesson, capping queue cleanly) and `complete_trouble_spot_review(...)` (advancing SM-2 interval or graduating when cleared at `100%` target BPM).
- Add Catch2 unit tests in `tests/lesson_engine_tests.cpp` and `tests/profile_store_tests.cpp`.

### Slice 21.2 — Tempo Ramp Toggle & Trouble-Spot Queue UI (`src/ui/lesson_view.hpp/.cpp`, `src/ui/home_dashboard.cpp`, `src/app/app_controller.cpp`)
- Add an **Auto-Ramp (`+4 BPM`)** toggle switch inside `LessonView::draw_practice_settings` (active whenever looping in `Steady beat` mode) and show a crisp animated pill (`Ramping · +4 BPM on 2 clean`) in the Practice strip when active.
- On the Post-Run Coach card when a `TroubleSpot` is found, automatically offer (or one-click pin) **`Save Bars X–Y for tomorrow`** alongside **`Drill Bars X–Y (Ramp Loop)`** (which enables `loop_phrase + auto_tempo_ramp` at `60%`).
- Surface due trouble-spot micro-drills cleanly on `HomeDashboard` and at the start of `Stage3Lessons` (one click jumps straight into `lesson_id` with `custom_bar_range` + `loop_phrase` + `auto_tempo_ramp` preloaded).

### Slice 21.3 — Verification & Polish (`src/app/profile_sync.cpp`, `tests/`)
- Wire `profile_sync.cpp` so trouble-spot updates flush to desktop `profile.json` and Emscripten `IDBFS` `/persist/profile.json`.
- Verify zero regressions on standard `60% / 80% / 100%` Tempo Ladder graduation and full-piece mastery logic.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including ramp up/down/cap bounds, trouble-spot deduplication, SM-2 graduation, and `profile.json` v1 backward-compatible load).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified C++ files.
4. Headless Light & Dark screenshots verify the active `Ramping` pill, Practice settings Auto-Ramp switch, Post-Run Coach `Drill (Ramp Loop)` action, and Home Dashboard due trouble-spot cue with zero clipping.

**Verification Evidence:**
- `cmake --build --preset native-debug` compiled cleanly with **0 warnings** under `-Wall -Wextra -Wpedantic`.
- `ctest --preset native-debug --output-on-failure` passed **101/101** test cases (100%) in `1.54 s`.
- `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with **0 warnings**.
- `src/core/` purity grep returned **0** matches; `clang-format --dry-run --Werror` passed cleanly on all modified C++ files.
- Headless Light & Dark screenshots verified on `t3_04 --bar-range 1-2 --auto-ramp`, `--open-lesson-settings`, `t5_02 --simulate-coach imperfect`, and `home --seed-trouble-spot`.

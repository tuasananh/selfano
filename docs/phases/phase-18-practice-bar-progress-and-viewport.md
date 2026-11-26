# Phase 18: Interactive Practice Strip, Visible Lesson Progress & Above-the-Fold Score Fit

**Status:** `[x] Complete` (Depends on Phase 17)  
**Goal:** Make the current lesson exercise mode, passage, hand, tempo, and 4-milestone roadmap immediately legible above the score; surface one-click **Restart** and **Exit active bar loop** controls without hiding them in a popover; upgrade `Practice settings` with real progress details and custom BPM stepping; and tighten vertical chrome so System 1's full Grand Staff fits above the fold at `1360×860`.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters

Right now `LessonView::draw_practice_prompt` (`src/ui/lesson_view.cpp`) renders static plain text (`Full piece · Both hands`) that omits the active practice mode (`Chord shapes` vs. `Wait for notes` vs. `Steady beat`) and hides the 4-stage mastery roadmap (`Brief -> Chords -> Phrases -> Tempo -> Mastered`). High-frequency controls like **Restart passage**, custom BPM adjustment, and escaping an active `"Loop Bars X–Y"` micro-loop (such as one launched from the Post-Run Coach) are trapped inside `Practice settings`, while the `"Lesson progress"` section inside settings shows no actual progress—only `"Mark lesson as mastered"`. Additionally, on `1360×860` with the Virtual Piano dock visible, the stacked top chrome pushes System 1's bass clef on `t1_01` just below the scroll fold.

---

## 2. Deliverables & Vertical Slices

### Slice 18.1 — Pure Core `LessonProgressSummary` Helper (`src/core/lesson_engine.hpp`, `src/core/lesson_progress.cpp`, `tests/lesson_engine_tests.cpp`)
- Add `struct LessonProgressSummary` in `src/core/lesson_engine.hpp`:
  - `bool brief_done{false};`
  - `bool skeleton_applicable{false};`
  - `bool skeleton_done{false};`
  - `std::size_t phrases_cleared{0};`
  - `std::size_t phrases_total{0};`
  - `std::size_t ladder_cleared{0};` (`0..3` for 60% / 80% / 100%)
  - `std::uint8_t milestone_index{0};` (`0..4` matching `suggest_next_practice_step`)
  - `std::string milestone_label;` (concise status string, e.g., `"Step 2/4 · Phrases 1/2"` or `"Mastered"`)
- Implement pure `summarize_lesson_progress(const LessonScore&, const LessonProgress&)` in `src/core/lesson_progress.cpp` using the exact same phrase-hand applicability and harmony rules as `suggest_next_practice_step` and `is_phrase_cleared`.
- Expand `tests/lesson_engine_tests.cpp` with Catch2 tests covering `New`, `Brief` read, `ChordSkeleton` clean, partial/full phrase bitmasks (including solo-hand phrases), `60%`/`80%`/`100%` Tempo Ladder stages, `Mastered`, and imported scores without harmonic chord symbols (`skeleton_applicable == false`).

### Slice 18.2 — Interactive Practice Strip, Active Loop Exit & Direct Restart (`src/ui/lesson_view.hpp/.cpp`)
- Upgrade `LessonView::draw_practice_prompt` into a clean, compact strip above the staff:
  - Render clickable context chips for active **Mode** (`Chord shapes` / `Wait for notes` / `Steady beat`), **Passage** (`Full piece` / `Phrase A` / `Bars X–Y`), **Hand** (`Right hand` / `Left hand` / `Both hands`), and **Tempo** (`60% · 48 BPM` / `Custom · XX BPM`)—clicking any chip opens `##lesson_settings_pop` so users know where to customize them.
  - Display a compact 4-step milestone indicator (`Brief · Chords · Phrases · Tempo`) reflecting `summarize_lesson_progress()` so learners always know where they stand in the lesson.
  - When `cfg.custom_bar_range` is active, display a highlighted exit chip **`Looping Bars X–Y  ×`** that immediately clears `custom_bar_range`, disables `loop_phrase`, stops playback, and returns to the active phrase/full piece.
  - Surface a compact **Restart** `icon_button` (`IconId::Refresh`, tooltip `"Restart passage"`, key hint `"G7"`) directly next to `Start count-in` / `Listen to demo`.

### Slice 18.3 — Upgraded `Practice settings` Popover & Above-the-Fold Staff Layout (`src/ui/lesson_view.cpp`)
- Add a custom **`-5` / `+5` BPM** stepper alongside the `60% / 80% / 100%` Tempo Ladder controls inside `draw_practice_settings` (calling `adjust_bpm(-5.0)` / `adjust_bpm(+5.0)`), keeping `TempoLadderStep::Custom` clearly visible and persistent during the session.
- Replace raw `ImGui::CollapsingHeader` sections inside `##lesson_settings_pop` with Apple HIG `section_kicker` + `hairline_separator` dividers, and display real readouts under `LESSON PROGRESS` (Brief status, Chord Shapes status, Phrases `X / Y`, Tempo Ladder `60% / 80% / 100%`, Best Clean BPM & Accuracy) alongside `"Mark lesson as mastered"` and `"Add to retention deck"`.
- Tighten vertical spacing above the staff (merging title/metadata cleanly and trimming redundant vertical `Dummy` gaps) so System 1's complete Grand Staff (both treble and bass clefs) on `t1_01` fits completely above the fold at `1360×860` with the Virtual Piano dock open.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including new `summarize_lesson_progress` unit tests).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified files.
4. Headless Light & Dark screenshots on `t1_01` and `t6_04` at `1360×860` with the Virtual Piano dock open verify that System 1's full Grand Staff (treble + bass clefs) is visible without scrolling, the clickable mode/passage/hand/tempo chips and Restart button render cleanly, `--bar-range 2-3` shows the one-click `Looping Bars 3–4  ×` exit chip, and `Practice settings` displays live progress and `-5/+5` BPM controls.

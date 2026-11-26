# Phase 20: Apple HIG Learn Brief, System Measure Numbers & Input Polish

**Status:** `[ ] Planned` (Depends on Phase 17 & Phase 18)  
**Goal:** Replace the collapsed raw-ImGui details block on the Learn Brief with scannable Apple HIG cards and a direct Tier Chapter Guide shortcut; add quiet classical system-start measure numbers so bar-range loops map effortlessly to the score; surface a subtle harmony clickability hint; and unify `Space` key transport across Continuous Etudes and Steady-Beat Lessons.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters

Even with clear navigation and a streamlined Practice screen, four polish gaps make the Lessons flow feel unfinished:
1. **Collapsed & generic Learn Brief (`src/ui/lesson_brief_view.cpp`):** The lower half of the Learn tab sits blank while Composer/Source, Phrase Map, Practice Recipe, technique cues, and named audio examples are tucked behind an unstyled blue `ImGui::CollapsingHeader("Lesson details")`. The primary audio button uses a generic `"Listen to an example"` label instead of the example's real title, and there is no direct link from a lesson's brief to its parent Tier's 2-minute **Chapter Guide**.
2. **Missing bar-number anchors on lesson scores (`src/ui/staff_renderer.*`):** Measure numbers were removed from individual bar headers (`14bb271`) to reduce visual noise, leaving no visual landmark on the staff for `"Loop Bars 3–4"` or the `First bar / Last bar` selectors in `Practice settings`.
3. **Undiscoverable harmonic popovers (`src/ui/lesson_view.cpp`):** Clickable chord / Roman numeral labels have only a faint underline, so learners rarely realize clicking `V7/V` or `bII6` opens an interactive theory explanation and chord preview.
4. **Inconsistent `Space` shortcut (`src/app/app_controller.cpp`):** Pressing `Space` starts/pauses the beat in Continuous Etudes, whereas in Lessons `Space` always triggers the Demo—even when the learner is in *Steady beat* mode and expects `Space` to start/pause the count-in.

---

## 2. Deliverables & Vertical Slices

### Slice 20.1 — Expanded Apple HIG Learn Brief & Tier Guide Shortcut (`src/ui/lesson_brief_view.hpp/.cpp`, `src/ui/lesson_view.cpp`)
- Add `bool open_tier_guide{false};` to `LessonBriefActions` (`src/ui/lesson_brief_view.hpp`) and wire it in `LessonView::draw` (`src/ui/lesson_view.cpp`) to `curriculum_modal_.open_chapter_guide(meta.tier)` (available for built-in lessons where `meta.tier > 0`).
- Refactor `draw_lesson_brief` (`src/ui/lesson_brief_view.cpp`):
  - Keep the Concept Title, opening summary paragraph, primary **`Start practicing`** / **`Back to practice`** CTA, the first **named** audio preview button (using `theory.audio_examples.front().label` instead of generic `"Listen to an example"`), and a secondary **`Tier N Chapter Guide`** pill button above the fold inside the hero card.
  - Replace `ImGui::CollapsingHeader("Lesson details")` with clean, always-readable Apple HIG cards below: **Hear & Compare** (remaining examples + captions), **Form & Phrase Map**, **Technique & Musicality Cues** (`pattern_tip`, `technique_tip`, `ballade_link`), and **Practice Recipe** (`practice_steps` + `composer_source`), omitting empty sections cleanly.

### Slice 20.2 — Classical System-Start Measure Numbers & Harmony Discoverability (`src/ui/staff_renderer.hpp/.cpp`, `src/ui/lesson_view.cpp`, `tests/staff_renderer_tests.cpp`)
- Add `std::optional<std::uint32_t> system_start_bar_number{};` to `StaffRenderOptions` (`src/ui/staff_renderer.hpp`).
- In `src/ui/staff_renderer.cpp`, when `system_start_bar_number.has_value() && *system_start_bar_number > 0U`, render a quiet classical measure number (small `Caption` / subtle tertiary ink) at the top-left of the system, accounting for it in vertical layout measurement so it never collides with `tempo_marking`, chord symbols, or high ledger-line fingerings.
- Populate `system_start_bar_number` in `LessonView::draw_staff_systems` for both System 1 and `UPCOMING` System 2 (respecting bar `0` pickups by leaving `std::nullopt` or skipping `0`).
- Render a quiet single-line `Caption` hint below the staff when `show_harmony_` is enabled and `hitboxes` is non-empty (`"Tip: click any chord or Roman numeral to hear and explain it"`).
- Add Catch2 layout/regression tests in `tests/staff_renderer_tests.cpp` covering system-start measure number placement on `t1_01`, `t5_01` (pickup bar `0`), and clearance against `tempo_marking` and `harmony_labels`.

### Slice 20.3 — Consistent `Space` Key Transport (`src/app/app_controller.cpp`)
- Update `AppController::poll_sdl_events` (`SDLK_SPACE` handler in `src/app/app_controller.cpp`):
  - When `active_practice_stage() == core::AutopilotStage::Stage3Lessons`, `lesson_view_.active_tab() == ui::LessonViewTab::Practice`, and `lesson_view_.engine().config().practice_mode == core::LessonPracticeMode::SteadyPulse`, toggle Play/Pause (`lesson_view_.toggle_play_pause(...)` with metronome click & panic cleanup on stop), matching `Stage2BEtudes`.
  - Keep `Space` bound to `trigger_replay_for_active_view(now_ms)` (Demo / Replay) in untimed modes (`ChordSkeleton` / `WaitForNote`), on the `Learn` tab, and in other stages.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including new `staff_renderer_tests.cpp` measure-number clearance tests).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. Live Chromium CDP verification on `build/web-release` at `1360×717` and `980×710` confirms **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors** across Learn Brief, Tier Chapter Guide, Practice, and Harmony Popover.
4. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified C++ files.
5. Headless Light & Dark screenshots of the expanded Learn Brief, `t1_01`, `t3_01`, `t5_01` (pickup), and `t6_04` confirm clean card spacing, crisp system-start measure numbers with zero collisions, and the harmony discoverability caption.

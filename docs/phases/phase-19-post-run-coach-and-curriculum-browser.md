# Phase 19: Non-Blocking Post-Run Coach & Upgraded Curriculum Browser

**Status:** `[ ] Planned` (Depends on Phase 17 & Phase 18)  
**Goal:** Eliminate dead-end waits after finishing a passage by making the Post-Run Coach non-blocking and actionable (inline accuracy `%`, `Try again`, `Dismiss`, play-to-retry in untimed modes, trouble-spot staff focus, and loop-mode accuracy pills), and upgrade the Curriculum & `My Books` browser with Tier filters, mastery counts, active-lesson auto-scroll, full-row clickability, and clipping-free `My Books` actions.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters

Two high-traffic flows currently frustrate keyboard-first learners:
1. **Post-Run Coach dead ends (`src/ui/lesson_view.cpp`):** While `show_post_run_coach_` is open, `LessonView::on_played_chord` silently drops all MIDI/QWERTY input—so a pianist who immediately replays Bar 1 gets no response until clicking with a mouse. On imperfect runs with a trouble spot (`draw_result`), the card hides the accuracy `%` inside `"Result details"` and offers no direct `"Try again"` or `"Dismiss"` button; on multi-system pieces (e.g., 8 bars), the staff remains parked on System 2 (Bars 5–8) even if the trouble spot was in Bars 3–4; and when `loop_phrase` is enabled (`finalize_completed_run`), completed passes give zero visual accuracy feedback.
2. **Curriculum Browser friction & `My Books` clipping (`src/ui/curriculum_modal.cpp`):** All 28 lessons are stacked in one long scroll list always starting at Tier 1, with no Tier filter pills, no overall/per-tier mastery totals, and no auto-scroll to the active lesson. Users must hit the small `"Open"` button rather than clicking anywhere on the lesson card (and clicking `"Current"` needlessly reloads the active lesson). Furthermore, `draw_lesson_row` reserves `open_w = 98.0f` without accounting for `"Edit"` and `"Trash"` buttons on `My Books` rows, causing button clipping on narrower widths.

---

## 2. Deliverables & Vertical Slices

### Slice 19.1 — Non-Blocking Post-Run Coach & Trouble-Spot Staff Focus (`src/ui/lesson_view.hpp/.cpp`, `tests/lesson_view_tests.cpp`)
- Update `LessonView::draw_result`:
  - Show the **accuracy `%`** badge and trouble-spot bar range inline on the coach card (e.g., `68% accuracy · Trouble spot: Bars 3–4`) rather than hiding accuracy inside `Result details`.
  - Provide secondary **`Try again`** (keeps current passage/tempo and restarts) and compact **`Dismiss`** buttons alongside the primary recommended CTA and `Result details`.
- In untimed modes (`ChordSkeleton` and `WaitForNote`), update `LessonView::on_played_chord` so playing on the MIDI keyboard or QWERTY while `show_post_run_coach_` is true automatically dismisses the coach, clears `coach_focus_bar_`, resets the passage, and evaluates the played chord against target `0` immediately (preserving `DemoPlaying` input isolation and `SteadyPulse` count-in gating).
- Add `std::optional<std::size_t> coach_focus_bar_{}` set to `spot->start_bar_index` whenever an imperfect run finishes with a trouble spot; in `draw_staff_systems`, use `*coach_focus_bar_` while `show_post_run_coach_` is true so the staff jumps directly to the system containing the missed/partial bars for immediate visual inspection (cleared on retry, dismiss, or config change).
- Record `std::optional<double> last_loop_accuracy_{}` on looped run completions (`engine_.config().loop_phrase == true`) and display a calm non-blocking `"Last loop: XX%"` status pill in the practice strip without interrupting continuous looping.
- Expand `tests/lesson_view_tests.cpp` with Catch2 tests verifying:
  - Play-to-retry auto-dismissal and immediate target `0` scoring in `WaitForNote` / `ChordSkeleton` after a completed run (while keeping `DemoPlaying` input blocked).
  - Trouble-spot `coach_focus_bar_` assignment on imperfect runs and clearing on retry/config change.
  - Loop-mode `last_loop_accuracy_` updates without raising `show_post_run_coach_`.

### Slice 19.2 — Curriculum Browser Filter, Progress & `My Books` Layout Fix (`src/ui/curriculum_modal.hpp/.cpp`)
- Add a Tier quick-filter bar (`All | T1 | T2 | T3 | T4 | T5 | T6`) inside the `Tiers 1-6` tab of `CurriculumModal`, an overall curriculum mastery pill (`X / 28 Mastered`), and per-tier mastery counts (`Y / N`) next to each tier header.
- Set `scroll_to_active_pending_` when opening the modal (`open()`) so the scroll area centers the currently active lesson on first frame, and pre-select the `My Books` tab automatically when the active lesson is imported.
- Make the entire lesson row card clickable to open the lesson (while keeping `Edit`, `Trash`, and `Chapter Guide` hitboxes independent), and simply close the modal without reloading/resetting progress state when clicking the already-active lesson.
- Fix right-aligned button width calculation in `draw_lesson_row` (`src/ui/curriculum_modal.cpp`) so `Open`/`Current`, `Edit`, and `Trash` on `My Books` rows reserve their combined width and never clip past the right edge of the card.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including new `lesson_view_tests.cpp` coach and loop assertions).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified files.
4. Headless Light & Dark screenshots verify:
   - Imperfect Post-Run Coach displaying inline `%`, trouble-spot label, primary loop CTA, `Try again`, `Result details`, and `Dismiss`, with the staff focused on the trouble-spot system.
   - Active loop mode showing the non-blocking `"Last loop: XX%"` pill.
   - Curriculum Browser showing the Tier filter strip, `X / 28 Mastered` pill, per-tier counts, and aligned `My Books` (`Open` / `Edit` / `Trash`) buttons with zero clipping.

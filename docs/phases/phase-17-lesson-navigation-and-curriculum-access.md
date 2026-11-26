# Phase 17: Lesson Navigation Bar, Prev/Next Stepping & Discoverable Curriculum Access

**Status:** `[x] Complete` (Depends on Post-Phase 16 Guided lesson experience)  
**Goal:** Replace the hidden title-chevron dropdown in `LessonView` with a clear, self-explanatory single-row lesson navigation bar featuring an explicit **All Lessons** curriculum button, direct **Previous / Next** lesson stepping across both the 28-lesson curriculum and `My Books`, inline lesson metadata, and an always-visible **Brief | Practice** tab switcher.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters

Currently `LessonView::draw_top_strip` (`src/ui/lesson_view.cpp`) places a bare ghost `ChevronDown` icon to the left of the lesson title and hides `"Change lesson"`, `"Lesson brief"`, and lesson metadata (`Tier · Key · Meter`) inside `##lesson_menu_pop`. Meanwhile, `LessonLibrary` (`src/core/lesson_library.hpp`) only exposes `next_lesson_id()` with no `previous_lesson_id()` or position counter, there are no Prev/Next controls in the UI (nor `IconId::ChevronLeft` in `src/ui/icons.hpp`), and switching between `Learn` and `Practice` is asymmetric (the `Learn` tab has no top-bar segmented control to switch back to `Practice` without clicking inside the card, and `Practice` hides `"Lesson brief"` inside the dropdown). Making lesson navigation explicit removes the guesswork on first visit.

---

## 2. Deliverables & Vertical Slices

### Slice 17.1 — Core Sequential Navigation Helpers (`src/core/lesson_library.hpp/.cpp`, `tests/lesson_library_tests.cpp`)
- Add `struct LessonPosition { std::size_t index{0}; std::size_t total{0}; bool is_imported{false}; };` (1-based `index` for UI display, e.g., `1 / 28`, or `0 / 0` when not found).
- Add `LessonLibrary::previous_lesson_id(std::string_view current_id) const` and `LessonLibrary::lesson_position(std::string_view current_id) const`.
- Support sequential `previous_lesson_id` and `next_lesson_id` across **both** `built_in_lessons()` (the 28-lesson curriculum chain) and `imported_lessons()` (`My Books`), returning `std::nullopt` at track boundaries (first/last built-in, first/last imported, or unknown ID) so built-in and imported tracks never bleed into one another.
- Expand `tests/lesson_library_tests.cpp` with Catch2 cases covering built-in boundaries (`t1_01` prev == `nullopt`, `t6_04` next == `nullopt`, mid-chain round-trip), single and multi-item `My Books` stepping, unknown IDs, and `lesson_position()` counts.

### Slice 17.2 — `IconId::ChevronLeft` Procedural Vector Icon (`src/ui/icons.hpp/.cpp`)
- Add `IconId::ChevronLeft` to `selfano::ui::IconId` in `src/ui/icons.hpp`.
- Draw `IconId::ChevronLeft` in `src/ui/icons.cpp` as a crisp horizontal mirror of `IconId::ChevronRight` matching Apple HIG stroke weight and proportions.

### Slice 17.3 — Single-Row Lesson Navigation Bar & Keyboard Shortcuts (`src/ui/lesson_view.hpp/.cpp`, `src/app/app_controller.cpp`, `tests/lesson_view_tests.cpp`)
- Update `LessonView::draw_top_strip` to accept `const core::LessonLibrary& library` and `const core::LessonProgress& progress`, replacing `##lesson_menu_pop` and the bare `ChevronDown` trigger with a clean, responsive top navigation bar:
  - Explicit **`All Lessons`** pill button (with `IconId::Layers` and position cue such as `All Lessons · 1/28`, or `My Books · 1/N` for imported scores) that opens `curriculum_modal_.open()` (or `curriculum_modal_.open_my_books()` when an imported score is active).
  - Direct **Previous / Next** `icon_button` controls (`IconId::ChevronLeft` / `IconId::ChevronRight`), cleanly disabled at track boundaries, with hover tooltips naming the adjacent lesson (`"Previous: ..."` / `"Next: ..."`), emitting `actions.select_lesson_id`.
  - Lesson title paired with a compact inline metadata line (`Tier N · Key · Meter` + mastery `status_pill` / `Guided Revision` badge).
  - Always-visible **`Brief | Practice`** `segmented_control` on built-in lessons (omitted on imported scores that have no brief) so switching between reading and playing is one click from either tab.
  - Right-aligned **`Practice settings`** button on the `Practice` tab.
- Ensure graceful wrapping/layout at both `1360×860` and `980×710` with zero button overflow or text clipping.
- Add `Alt+Left` / `Alt+Right` keyboard shortcuts in `AppController::poll_sdl_events` (`src/app/app_controller.cpp`) when `Stage3Lessons` is active and `!io.WantTextInput` to step to the previous/next lesson with a HUD toast (`"Lesson: ..."`).
- Add regression checks in `tests/lesson_view_tests.cpp` verifying Prev/Next lesson navigation state and tab switching.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including new `lesson_library_tests.cpp` and `lesson_view_tests.cpp` assertions).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check (`#include` boundary for `imgui|SDL|libremidi|emscripten` and `steady_clock::now|system_clock::now|random_device`) returns 0 matches; `clang-format --dry-run --Werror` passes on all modified C++ files.
4. Headless Light & Dark screenshots at `1360×860` and `980×710` across `Learn`, `Practice`, and an imported `My Books` lesson confirm crisp alignment, clear `All Lessons · X/28` + `<` / `>` + `Brief | Practice` controls, inline metadata, and zero truncation or overflow.

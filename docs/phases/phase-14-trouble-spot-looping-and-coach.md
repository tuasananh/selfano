# Phase 14: Custom Trouble-Spot Bar Looping & Post-Run Coach

**Status:** `[ ] Planned` (Depends on Phase 13)  
**Goal:** Enable pianists to isolate any arbitrary 1–2 bar transition (including seams *across* phrase boundaries), soften harsh all-or-nothing polyphonic grading in `SteadyPulse`, and surface an actionable **Post-Run Coach Strip** that pinpoints trouble-spot bars and launches targeted slow loops in one click.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Real piano practice happens in 1-to-2 bar micro-loops—especially at the exact transition *between* Phrase A and Phrase B where hands shift position. Today, Selfano only allows selecting fixed 4-bar phrases and only reports an aggregate `Target X/Y · %` score after a run, leaving learners to figure out on their own where they broke down.

---

## 2. Deliverables & Vertical Slices

### Slice 14.1 — Custom Bar-Range Slicing & Per-Bar Diagnostics (`src/core/lesson_engine.*`)
- Add `std::optional<std::pair<std::size_t, std::size_t>> custom_bar_range{}` to `LessonConfig` (`0`-based inclusive `[start_bar, end_bar]`, clamped safely to `score.measures.size()`).
  - When set, overrides `phrase_index` for slicing, demo playback, accompaniment, and looping; selecting a preset Phrase or Full Piece clears `custom_bar_range`.
- Add `BarDiagnostic { std::size_t bar_index; std::uint32_t bar_number; std::uint32_t hits; std::uint32_t partial_hits; std::uint32_t misses; }` to `LessonRunSummary`.
- Add pure helper `find_trouble_spot(const LessonRunSummary&)` that identifies the highest-miss contiguous 1–2 bar window (preferring a 2-bar seam when adjacent bars both have misses).

### Slice 14.2 — Partial-Chord Feedback in `SteadyPulse` (`src/core/lesson_engine.*`, `src/ui/lesson_staff_adapter.*`, `staff_renderer.hpp`)
- Add `LessonTargetStatus::PartialHit` alongside `Pending`, `Hit`, and `Missed`:
  - On multi-note targets (`expected_midi.size() >= 2`) in `SteadyPulse`, if the player hits `>= 50%` of expected notes with zero wrong pitches (e.g., 3 of 4 notes in a Tier 5 Chopin chord), grade as `PartialHit` (counting `0.5` credit in accuracy, amber visual highlight on the staff, and annotating the specific missed note name like `Missed F4` instead of failing the entire chord red).
- Keep `WaitForNote` strict on exact target completion so slow accuracy stays rock-solid.

### Slice 14.3 — Bar-Range Selector & Post-Run Coach UI (`src/ui/lesson_view.*`)
- Add a compact **`Bars [start]–[end]`** micro-range selector in the scaffold bar (plus quick-clear back to Phrase / Full Piece).
- Render a calm, dismissible **Post-Run Coach Strip** whenever a run reaches `LessonPlaybackState::Completed`:
  - **Clean run (`>= 90%`):** Celebrates accuracy and offers a one-click button for the next logical step (e.g., *"Try 80% Tempo →"* or *"Play Full Piece →"*).
  - **Imperfect run (`< 90%`):** Highlights the top hotspot from `find_trouble_spot(...)` (e.g., *"Focus spot: Bars 6–7 (3 misses)"*) with a one-click **`Drill Bars 6–7 at 60% (Loop)`** button that sets `custom_bar_range`, enables `loop_phrase`, drops tempo to 60%, and readies the transport.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` passes 100% with 0 warnings, including unit tests for `custom_bar_range` clamping/looping, `find_trouble_spot` 1-bar & 2-bar seam detection, and `PartialHit` chord grading.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with 0 warnings.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots confirm crisp custom bar-range controls, amber `PartialHit` staff annotation, and Post-Run Coach Strip with zero clutter or clipping.

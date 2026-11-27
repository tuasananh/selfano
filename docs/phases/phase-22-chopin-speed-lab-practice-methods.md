# Phase 22: Chopin Speed Lab (Rhythmic Alteration, `4+1` Bursts & Hand Blocking)

**Status:** `[x] Complete` (Depends on Phase 21 — Track A: Learning Speed Multipliers)  
**Goal:** Equip `LessonEngine` with four conservatory-proven technical practice transforms—**Dotted Long–Short**, **Dotted Short–Long**, **Stop-and-Prepare Bursts (`4+1`)**, and **Hand-Position Blocking**—applicable to any phrase or custom bar loop so learners build relaxed velocity and hand-shift mapping 2–3× faster than linear repetition.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Linear metronome grinding hits a hard speed ceiling once 8th/16th-note passages speed up, because the brain tries to fire too many unfamiliar finger transitions back-to-back without recovery time. Concert pianists unlock Chopin runs by alternating **Long–Short** and **Short–Long** dotted rhythms (training half the transitions at full speed while resting on alternate notes), playing **4+1 stop-and-prepare bursts** (chunking 4 rapid notes onto a relaxed downbeat anchor), and **blocking** broken patterns (Alberti bass, arpeggios) into simultaneous hand-position chords.

---

## 2. Deliverables & Vertical Slices

### Slice 22.1 — Pure Core Score Transform Engine (`src/core/practice_transforms.hpp/.cpp`, `tests/lesson_engine_tests.cpp`)
- Add `enum class PracticeTransform : std::uint8_t { Straight = 0, DottedLongShort = 1, DottedShortLong = 2, Burst4Plus1 = 3, HandBlocking = 4 };` in `src/core/practice_transforms.hpp`.
- Implement pure, deterministic slice transforms on `std::span<const LessonMeasure>`:
  - **`DottedLongShort` / `DottedShortLong`:** Pair consecutive equal-duration running subdivisions (`8th` or `16th`) into `3:1` (or `2:1` readable dotted + short) rhythmic pairs while keeping every pitch, fingering, and bar boundary valid.
  - **`Burst4Plus1`:** Group running subdivisions into 4 fast notes landing on a held arrival note (`+1` beat freeze/prepare rest) so the learner plays each 5-note hand gesture at near-target velocity with a full tension reset between bursts.
  - **`HandBlocking`:** Collapse sequential broken-chord/Alberti/arpeggio notes within each half-bar or harmonic change (same staff, non-overlapping pitch classes within an ergonomic octave span `<= 12` semitones) into simultaneous chord blocks with ascending fingerings (`1–5`).
- Validate every transformed measure through `validate_lesson_score` invariants (no negative ticks, no out-of-range pitches, clean ties/rests).
- Add thorough Catch2 unit tests verifying each transform on representative scores (`t2_01` Alberti bass, `t3_04` 16th-note bursts, `t4_02` Bach Invention, `t4_04` arpeggios).

### Slice 22.2 — `LessonEngine` Integration & Non-Destructive Mastery Rules (`src/core/lesson_engine.hpp/.cpp`, `src/core/lesson_progress.cpp`)
- Add `PracticeTransform practice_transform{PracticeTransform::Straight};` to `LessonConfig`.
- When `practice_transform != Straight`, `LessonEngine::rebuild_slice_and_targets()` builds `display_measures_`, `targets_`, and Demo playback from the transformed slice while preserving the original `score_` untouched.
- Ensure transformed drill runs count as valid practice / phrase warm-up and give full Post-Run Coach feedback, while **Mastery / Tempo Ladder graduation (`60%/80%/100%`)** requires `PracticeTransform::Straight` so drills never falsely graduate a piece.

### Slice 22.3 — Speed Lab Selector & Visual Cue in Lessons UI (`src/ui/lesson_view.hpp/.cpp`, `src/app/cli_options.hpp/.cpp`)
- Add a clean **Speed Lab** segmented/pill selector inside `Practice settings` (and a compact active badge in the Practice strip when a transform is active, e.g., `Speed Lab: Long–Short  ×` with one-click reset to `Straight`).
- Update the Post-Run Coach strip on fast/dense trouble spots to suggest `"Try Long–Short"` or `"Try Block Chords"` alongside `"Drill Bars X–Y"`.
- Add `--practice-transform straight|long-short|short-long|burst|block` CLI flag for reproducible headless screenshot verification.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including all 4 transforms, ergonomic span limits, Demo playback alignment, and `Straight`-only mastery guardrail).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all new/modified C++ files.
4. Headless Light & Dark screenshots on `t2_01` (`block`), `t3_04` (`long-short`), and `t4_02` (`burst`) verify legible notation, clear active transform exit pill, and zero engraving collisions.

**Verification Evidence:**
- `cmake --build --preset native-debug` compiled cleanly with **0 warnings** under `-Wall -Wextra -Wpedantic`.
- `ctest --preset native-debug --output-on-failure` passed **106/106** test cases (100%) in `4.65 s`.
- `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with **0 warnings**.
- `src/core/` purity grep returned **0** matches; `clang-format --dry-run --Werror` passed cleanly on all modified/created C++ files.
- Headless Light & Dark screenshots verified on `t2_01 --practice-transform block`, `t3_04 --practice-transform long-short`, and `t4_02 --practice-transform burst`.

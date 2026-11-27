# Phase 27: Automatic Harmonic Analyzer for Imported Scores

**Status:** `[x] Complete` (Depends on Phase 20 — Track B: Romantic Score & Importer Foundations)  
**Goal:** Build a pure C++23 key-aware **Harmonic Analyzer** that infers bar- and half-bar-level chord symbols and Roman numerals (`i`, `iv`, `V7`, `V7/V`, `vii°7`, `bII6`) automatically when importing `.musicxml`/`.mxl` files—unlocking Selfano's `ChordSkeleton`, clickable Theory Popover, and Memory Fade on *any* classical score from IMSLP or MuseScore.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Selfano's single biggest differentiator for self-taught learners is **harmonic chunking** (`ChordSkeleton` + Roman-numeral explanations + harmony-anchored memory). Today, virtually all public-domain classical `.musicxml`/`.mxl` files lack `<harmony>` tags, so imported scores open with empty harmonic analysis unless the user manually types chord symbols for 50–264 bars. Automated bar-level harmonic estimation immediately gives every imported Chopin, Bach, or anime piece a usable harmonic skeleton (which the user can still tweak).

---

## 2. Deliverables & Vertical Slices

### Slice 27.1 — Pure C++23 Pitch-Class & Bass-Weighted Chord Estimator (`src/core/harmonic_analyzer.hpp/.cpp`, `tests/harmonic_analyzer_tests.cpp`)
- Implement `analyze_score_harmony(const LessonScore& score, HarmonicAnalysisOptions opts = {}) -> std::vector<std::vector<HarmonicAnnotation>>` in pure C++23 (`src/core/`):
  - Slice each measure into harmonic windows (1 per bar or 2 half-bar windows when strong harmonic rhythm change is detected).
  - Weight pitch classes by metrical strength (downbeats & bass notes weighted highest; short passing/neighbor 16ths weighted lowest) and score candidate templates: diatonic triads/7ths, secondary dominants (`V7/V`, `V7/iv`, `V7/VI`, etc.), diminished 7ths (`vii°7`), half-diminished (`iiø7`), and Neapolitan (`bII6`).
  - Apply lightweight Viterbi / transition smoothing (favoring functional root motion by 5ths, common-tone voice leading, and cadential formulas `ii/iv -> V7 -> I/i`) so passing tones never cause jittery false chord labels.
  - Format both `chord_symbol` (verified parseable via `parse_chord_symbol`) and `roman_numeral` (verified explainable via `explain_harmony`).
- Benchmark and test in `tests/harmonic_analyzer_tests.cpp` against the hand-annotated built-in curriculum (`t1_01`, `t2_01`, `t3_01`, `t5_02`, `t5_04`, `t6_03`, `t6_04` with harmony stripped), asserting high chord-root & function agreement (`>= 80%` on diatonic/classical scores, clean `bII6` detection on `t6_03`).

### Slice 27.2 — Import Pipeline Integration (`src/core/musicxml_importer.cpp`, `src/core/musicxml_importer.hpp`)
- Add `bool auto_analyze_harmony_if_missing{true};` to `MusicXmlImportOptions`.
- When an imported MusicXML score has zero `<harmony>` tags, automatically populate `measure.harmony` via `analyze_score_harmony(...)` and emit an informative non-fatal import note (`"Auto-analyzed harmony for N bars (editable in My Books)"`).
- Never overwrite explicit `<harmony>` tags already present in the source XML.

### Slice 27.3 — One-Click `"Auto-Detect Harmony"` in Import & Edit Modal (`src/ui/import_lesson_modal.hpp/.cpp`, `src/app/lesson_import_controller.cpp`)
- Add an **`Auto-Detect Harmony`** action button inside `ImportLessonModal` (for both new imports and editing existing `My Books` scores) so learners can run or re-run harmonic detection and refine any individual bar's chord/Roman label in seconds.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including accuracy benchmark against stripped curriculum scores, parseable `chord_symbol` invariant, and non-overwrite of explicit MusicXML `<harmony>`).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless `--import-lesson` smoke test on a harmony-free `.musicxml` fixture + Light/Dark screenshots confirm populated chord/Roman headers, working clickable Theory Popover, and active `ChordSkeleton` mode.

# Phase 24: Fine Subdivisions & Tuplets (32nd Notes, Triplets, Quintuplets & Sextuplets)

**Status:** `[x] Complete` (Depends on Phase 20 — Track B: Romantic Score & Importer Foundations)  
**Goal:** Remove Selfano's 16th-note / binary-only rhythmic ceiling by adding **32nd notes**, **double-dotted values**, and **general tuplets** (`3:2`, `5:4`, `6:4`, `7:4`) across `LessonScore`, JSON codec, MusicXML importer, `StaffRenderer`, and `LessonEngine`—unblocking authentic Chopin passagework and Urtext `.mxl` imports.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

`kTicksPerQuarter = 480` already divides cleanly by `2, 3, 4, 5, 6, 8, 10, 12, 16` (`480 / 8 = 60` for a 32nd note, `480 / 3 = 160` for an 8th triplet, `480 / 6 = 80` for a 16th sextuplet, `480 / 5 = 96` for a 16th quintuplet). Yet `NoteBaseDuration` currently stops at `Sixteenth (120 ticks)` and `musicxml_importer.cpp` clamps/drops anything shorter than `120 ticks`. That corrupts Chopin's *Nocturnes*, *Waltzes*, *Études*, and *Ballade No. 1* on import.

---

## 2. Deliverables & Vertical Slices

### Slice 24.1 — Extended `RhythmicValue`, Tuplet Metadata & JSON Codec (`src/core/lesson_score.hpp/.cpp`, `src/core/lesson_score_json.cpp`)
- Extend `NoteBaseDuration` with `ThirtySecond` (`60` ticks) and add optional `std::uint8_t dots{0}` (backward-compatible with existing `"dotted": true` JSON -> `dots = 1`, plus `dots = 2` double-dotted).
- Add optional `struct TupletRatio { std::uint8_t actual{3}; std::uint8_t normal{2}; std::uint16_t group_id{0}; };` on `LessonEvent` so `event_ticks()` computes exact integer ticks (`(base_ticks * normal) / actual`, e.g., `160` for 8th triplet, `80` for 16th sextuplet, `96` for 16th quintuplet, `320` for quarter triplet) while keeping exact validation (`measure_ticks()` sum).
- Keep 100% backward compatibility with all existing 28 `assets/lessons/t*.json` files.
- Add Catch2 unit tests in `tests/lesson_score_tests.cpp` covering 32nd notes, double dots, triplets, quintuplets, sextuplets, and JSON round-trips.

### Slice 24.2 — High-Resolution MusicXML Tuplet & 32nd Import (`src/core/musicxml_importer.cpp`, `tests/musicxml_importer_tests.cpp`)
- Parse `<time-modification><actual-notes>` / `<normal-notes>` and `<notations><tuplet type="start|stop"/>` plus `<type>32nd</type>` and multiple `<dot/>` nodes.
- Replace the destructive `120`-tick quantization floor in `quantize_raw_ticks()` and `normalize_staff_events()` with exact tuplet-aware tick quantization (preserving `60`-tick 32nds and `80`/`96`/`160`-tick tuplets cleanly, warning only on unsupported micro-durations `< 30` ticks).
- Add Catch2 MusicXML fixtures containing triplets, 32nd runs, 2-against-3 polyrhythm, and sextuplets.

### Slice 24.3 — Triple Beams, Flags, Tuplet Brackets & Playback Timing (`src/core/staff_layout.*`, `src/ui/smufl_glyphs.hpp`, `src/ui/staff_renderer.*`, `src/core/lesson_engine.cpp`)
- Add 32nd-note SMuFL flags (`U+E244`/`U+E245`) and 32nd rest (`U+E4E8`) in `smufl_glyphs.hpp`.
- Extend `compute_beam_groups` (`staff_layout.cpp`) and `staff_renderer.cpp` to draw **3rd beams (32nd)**, **tuplet brackets + numeral (`3`, `5`, `6`, `7`)**, and double augmentation dots without colliding with slurs or fingerings.
- Verify `LessonEngine` target generation, `SteadyPulse` hit windows, and `DemoPlaying` playback handle 32nds and 2-against-3 polyrhythms cleanly (ensuring simultaneous vs. closely offset tick targets remain cleanly matchable).
- Add a Classical/Romantic showcase specimen (`--showcase-preset romantic`) exercising triplets, 32nds, and 2-against-3.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including 32nds, tuplets, 2-against-3 MusicXML import, beaming, and zero regression on all 28 existing lessons).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots of the new tuplet/32nd specimen verify crisp triple beams, tuplet numerals/brackets, double dots, and zero collisions.

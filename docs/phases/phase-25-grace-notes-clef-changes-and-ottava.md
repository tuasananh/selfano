# Phase 25: Grace Notes, Mid-Piece Clef Changes & `8va`/`8vb` Ottava Brackets

**Status:** `[ ] Planned` (Depends on Phase 24 — Track B: Romantic Score & Importer Foundations)  
**Goal:** Enable faithful reading and importing of Romantic piano scores by supporting **grace notes / appoggiaturas**, **mid-piece clef changes** (especially Left Hand treble clef), and **`8va`/`8vb` ottava brackets** across `LessonScore`, MusicXML import, `StaffRenderer`, and `LessonEngine`.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Chopin's *Ballade No. 1*, *Nocturnes*, and *Waltzes* constantly switch the left hand into **Treble Clef**, lift high right-hand climaxes under **`8va`** brackets, and ornament melodies with **acciaccaturas / grace notes**. Without clef changes and `8va`, reading real Romantic music devolves into 6–8 stacked ledger lines; without grace-note support, MusicXML import strips Chopin's signature melodic turns and sighs.

---

## 2. Deliverables & Vertical Slices

### Slice 25.1 — Core Model, JSON Codec & MusicXML Import (`src/core/lesson_score.hpp/.cpp`, `src/core/lesson_score_json.cpp`, `src/core/musicxml_importer.cpp`)
- Add optional, backward-compatible score primitives:
  - `LessonEvent::is_grace{false}`, `bool grace_slash{true}` (zero-duration rhythmic footprint in measure-tick validation, ordered cleanly before the host beat tick).
  - `struct ClefChange { int start_tick{0}; Clef clef{Clef::Treble}; bool treble_staff{false}; };` on `LessonMeasure`.
  - `struct OttavaSpan { std::size_t start_bar{0}; int start_tick{0}; std::size_t end_bar{0}; int end_tick{0}; bool treble{true}; int octave_shift{1}; /* +1 = 8va, -1 = 8vb */ };` on `LessonScore`.
- Update `musicxml_importer.cpp`:
  - Import `<grace slash="yes|no"/>` instead of skipping it (keeping non-fatal warning only for malformed ornaments).
  - Parse mid-piece `<attributes><clef number="1|2">` into `ClefChange`.
  - Parse `<direction><direction-type><octave-shift type="down|up|stop" size="8"/>` into `OttavaSpan` (remembering MusicXML's `type="down"` = `8va` written an octave lower while sounding an octave higher).
- Add Catch2 round-trip and MusicXML import tests in `tests/lesson_score_tests.cpp` and `tests/musicxml_importer_tests.cpp`.

### Slice 25.2 — Staff Engraving for Grace Notes, Mid-Bar Clefs & Ottava Brackets (`src/core/staff_layout.*`, `src/ui/staff_renderer.*`, `src/ui/lesson_staff_adapter.cpp`)
- Render **grace notes** at `~68%` scale with miniature stems/flags/slash immediately left of their host beat, reserving horizontal width cleanly.
- Render **mid-piece clef changes** at `~75%` clef scale (both start-of-bar and mid-bar), shifting subsequent staff-step geometry for that staff onto the active clef until changed back.
- Render **`8va ----┐`** / **`8vb ----┘`** dashed brackets above/below the staff while transposing displayed staff steps by `-7` / `+7` diatonic steps (sounding MIDI pitch remains exact!).
- Add layout regression tests in `tests/staff_renderer_tests.cpp` verifying ledger-line reduction under `8va` and clean LH treble-clef rendering.

### Slice 25.3 — Demo Playback & Ergonomic Grace-Note Grading (`src/core/lesson_engine.cpp`, `tests/lesson_engine_tests.cpp`)
- In `LessonEngine::build_demo_playback()` and live `DemoPlaying`, play grace notes as crisp `~65 ms` pre-beat/on-beat ornaments right ahead of the host note.
- In `LessonEngine` evaluation: allow grace notes to be graded cleanly in `WaitForNote` and **non-punitively accepted (or optional toggle)** in `SteadyPulse` so fast Chopin grace notes never break rhythm flow for learners.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots of a Romantic test fixture with grace notes, LH treble clef switch, and `8va` climax verify crisp engraving and zero collisions.

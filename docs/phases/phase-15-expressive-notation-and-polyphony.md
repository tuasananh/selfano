# Phase 15: Expressive Classical Notation & Two-Voice Polyphony

**Status:** `[ ] Planned` (Depends on Phase 11; independent of Phases 12–14)  
**Goal:** Bring authentic musical expression onto Selfano's Grand Staff—rendering tempo markings, dynamics (`pp`–`ff`), cresc./dim. hairpins, articulations (staccato, tenuto, accent, arpeggio roll), phrase slurs, damper pedal markings (`Ped. … *`), and true two-voice per-hand polyphony—while applying them to Demo playback and MusicXML import.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

A pianist does not read pitches and durations in isolation—slurs show phrasing breath, staccato/tenuto define touch, dynamics shape emotional contour, and `Ped. … *` teaches clean resonance. Currently `LessonScore` has zero expressive fields, even though lesson `technique_tip` text already references slurs, detached quarters, `ff`/`p`/`pp` echoes, and rolled chords.

---

## 2. Deliverables & Vertical Slices

### Slice 15.1 — Expressive `LessonScore` Model, JSON Codec & MusicXML Pass-Through (`src/core/lesson_score.*`, `musicxml_importer.cpp`)
- Extend `LessonScore` with 100% optional, backward-compatible expressive fields:
  - `LessonMetadata::tempo_marking` (e.g., *"Largo"*, *"Allegretto"*, *"Moderato cantabile"*)
  - `Articulation` flags on `LessonEvent`: `Staccato`, `Tenuto`, `Accent`, `Arpeggiate`
  - `DynamicEvent { int start_tick; DynamicLevel level; }` (`pp`, `p`, `mp`, `mf`, `f`, `ff`) and `HairpinSpan` (`Crescendo` / `Diminuendo`) on `LessonMeasure`
  - `SlurSpan { std::size_t start_bar; int start_tick; std::size_t end_bar; int end_tick; bool treble; }` on `LessonScore`
  - `PedalSpan { std::size_t start_bar; int start_tick; std::size_t end_bar; int end_tick; }` on `LessonScore`
- Extend `lesson_score_json.cpp` and `validate_lesson_score` to serialize, parse, and validate all expressive spans cleanly.
- Extend `musicxml_importer.cpp` to preserve `<dynamics>`, `<articulations>` (`<staccato/>`, `<tenuto/>`, `<accent/>`), `<arpeggiate/>`, `<slur>`, and `<pedal>` from imported `.musicxml`/`.mxl` files.

### Slice 15.2 — SMuFL Expressive Engraving & Two-Voice Stems (`src/ui/smufl_glyphs.hpp`, `staff_renderer.*`, `lesson_staff_adapter.*`)
- Add Bravura SMuFL glyphs (`smufl_glyphs.hpp`):
  - Dynamics (`p` `U+E520`, `m` `U+E521`, `f` `U+E522`, combined `pp`/`mp`/`mf`/`ff`)
  - Articulations (`staccato` `U+E4A2`/`U+E4A3`, `tenuto` `U+E4A4`/`U+E4A5`, `accent` `U+E4A0`/`U+E4A1`, `arpeggio` `U+EAA9`)
  - Pedal (`Ped.` `U+E650`, release `*` `U+E655`)
- Update `staff_renderer.cpp` & `lesson_staff_adapter.cpp`:
  - Render tempo marking above Bar 1, dynamics & hairpins centered in the Grand Staff gap, articulation marks opposite the stem, smooth cubic-Bezier phrase slurs, and `Ped. … *` lane below the bass staff.
  - Render multi-voice events cleanly (`voice 0` stems up, `voice 1` stems down with horizontal collision offset when seconds/unisons occur).
- Update `LessonEngine::build_demo_playback()` to apply dynamic velocity scaling, staccato gate shortening (`~45%` duration), accent boost, and rolled `Arpeggiate` chord stagger (`~35 ms`).

### Slice 15.3 — Expressive Annotation of All 28 Built-In Lessons (`assets/lessons/t*.json`)
- Annotate all 28 built-in lessons with authentic tempo words, dynamics, hairpins, phrase slurs, articulations, and pedal spans matching real editions (e.g., `ff / p / pp` echoes in `t5_02`, waltz pedal releases in `t1_04`/`t2_02`/`t5_04`/`t6_04`, detached Baroque quarters vs. legato 8ths in `t3_01`, rolled wide chords where appropriate).
- Add at least one true two-voice per-hand showcase passage (e.g., sustained soprano melody + inner voice in Tier 5/6) to exercise and verify polyphonic engraving.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` passes 100% with 0 warnings, including JSON round-trip, MusicXML expressive import, expressive Demo velocity/gate shaping, and 2-voice layout unit tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with 0 warnings.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots (`t3_01`, `t5_02`, `t6_03`, `t6_04`, plus `--tab showcase --showcase-preset classical`) and Chromium CDP WebGL verification confirm crisp, collision-free dynamics, slurs, articulations, pedal lanes, and 2-voice stems.

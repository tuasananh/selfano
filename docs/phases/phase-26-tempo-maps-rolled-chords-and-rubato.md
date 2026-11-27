# Phase 26: Multi-Section Tempo Maps, Rolled Chords & Elastic Rubato Mode

**Status:** `[x] Complete` (Depends on Phase 24 — Track B: Romantic Score & Importer Foundations)  
**Goal:** Support Chopin's real musical pacing by adding **per-measure tempo changes**, a **wider rolled-chord matching window (`~250 ms`)** for arpeggiated/wide chords, and an optional **Elastic Rubato** follower mode alongside `SteadyPulse`.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `midi-emscripten-interop`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Three realities of Romantic piano break strict mechanical grading:
1. Pieces change tempo across sections (*Ballade No. 1*: *Largo* `52` → *Moderato* `96` → *Agitato* → *Meno mosso* → *Presto con fuoco*).
2. Wide Romantic chords (`9ths`, `10ths`, or explicitly marked `Arpeggiate` chords) are rolled bottom-to-top over `150–250 ms`—exceeding the standard `80–120 ms` chord grouper window and causing false misses.
3. Once a learner has locked in steady rhythm, authentic Chopin phrasing requires *tempo rubato* (breathing slightly at phrase peaks and cadences) without fighting a rigid metronome click.

---

## 2. Deliverables & Vertical Slices

### Slice 26.1 — Per-Measure Tempo Map (`src/core/lesson_score.hpp/.cpp`, `src/core/lesson_score_json.cpp`, `src/core/musicxml_importer.cpp`, `src/core/lesson_engine.cpp`)
- Add optional `struct TempoEvent { int start_tick{0}; double bpm{96.0}; std::string label{}; };` on `LessonMeasure` (backward-compatible with `meta.target_bpm`).
- Update `musicxml_importer.cpp` to capture mid-piece `<sound tempo="...">`, `<metronome>`, and tempo `<words>` changes instead of stopping after the first tempo mark.
- Update `LessonEngine` transport, Tempo Ladder scaling (`60% / 80% / 100%` scales every section proportionally), and `build_demo_playback()` to follow the active section's BPM cleanly.
- Render mid-piece tempo labels (`Moderato`, `Più mosso`, `Presto`) above the staff in `staff_renderer.cpp`.

### Slice 26.2 — Arpeggiated / Wide Rolled-Chord Matching (`src/core/lesson_engine.hpp/.cpp`, `src/midi/chord_grouper.hpp/.cpp`)
- Mark targets whose chord has `ArticulationFlag::Arpeggiate` or a single-hand span `> 12` semitones as `allow_rolled_chord = true`.
- In `LessonEngine::submit_played_notes(...)`, maintain a short rolling accumulation buffer (`<= 260 ms` in `SteadyPulse` / `WaitForNote`) for `allow_rolled_chord` targets so an upward bottom-to-top arpeggio roll registers cleanly as a single `Hit` instead of splitting into `PartialHit` + `Missed`.
- Add Catch2 tests covering rolled 10ths and `Arpeggiate`-marked chords in both `WaitForNote` and `SteadyPulse` (plus verifying tight chords still reject sloppy smeared hits).

### Slice 26.3 — Elastic Rubato Follower Mode (`src/core/lesson_engine.hpp/.cpp`, `src/ui/lesson_view.cpp`)
- Add `LessonPracticeMode::RubatoFollow` (optional 4th mode in `Practice settings`, unlocked/highlighted for Romantic pieces):
  - Advances smoothly with the pianist's actual performed pace (tracking a damped local tempo estimate from recent inter-onset intervals so the playhead glides naturally and accompaniment/pedal cues stay synced) while grading pitch accuracy and flagging extreme rhythmic distortion (`> 2×` unintended ratio drift).
- Keep `SteadyPulse` as the default for Tempo Ladder mastery so rhythmic discipline stays rock-solid; `RubatoFollow` serves expressive phrasing runs and post-mastery polish.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (multi-tempo scaling, rolled-chord accumulation, and elastic rubato follower tests).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots verify mid-piece tempo headers (`Largo` -> `Moderato`) and the Practice settings `Rubato` option with zero UI clipping.

# Phase 11: Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons

**Status:** `[ ] Planned` (Depends on Phase 9 & Phase 10)  
**Goal:** Eliminate physically unplayable hand spans, anti-legato fingerings, shifted pickup bars, and wildly jumping Chord Skeletons across all 28 built-in classical lessons—backed by automated Catch2 ergonomic & voicing invariants so regressions can never re-enter the curriculum.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Comes First

Before adding richer theory text or guided practice flows, the sheet music itself must be physically playable, musically faithful, and rhythmically honest. Auditing the 28 Phase 9 scores revealed:
- **44 ergonomic/fingering anomalies**, including simultaneous solid 10ths (`15–16` semitones) in Tiers 2, 3, 5, and 6 (`t2_05`, `t3_03`, `t3_04`, `t5_01`, `t5_02`, `t6_02`, `t6_03`) and same-finger stepwise slides (`5–5`, `1–1`, `2–2`).
- **Missing pickup bars (anacrusis)** on canonical Chopin pieces (`t5_01` Op. 28 No. 7, `t5_03` Op. 28 No. 4, `t5_04` Waltz B. 150), shifting downbeats by a full beat.
- **Title/text discrepancies** (`t1_03` titled *Natural Minor Lament* while using Harmonic minor `G#`/`E7`; `t5_03` Bar 4 labeled `Am/E` with `E3–G3–C4` notes; simplified paraphrases labeled with exact canonical opus titles).
- **Non-voice-led `ChordSkeleton` generation** (`make_skeleton_chord_notes`), jumping every RH chord to root position on `C4..B4` and assigning `1–3–5–5` fingerings to 4-note chords.

---

## 2. Deliverables & Vertical Slices

### Slice 11.1 — Anacrusis / Pickup Measure Support (`src/core/lesson_score.*`, `lesson_engine.cpp`, `lesson_staff_adapter.cpp`)
- Add optional `std::optional<int> pickup_ticks{}` on `LessonMeasure` (backward-compatible JSON serialization in `lesson_score_json.cpp`).
- Add helper `effective_measure_ticks(const LessonMeasure&, TimeSignature)`:
  - When `pickup_ticks` is set, require `0 < *pickup_ticks < meter.measure_ticks()` and allow primary voice 0 to sum to `*pickup_ticks`.
  - Allow `bar_number == 0` for anacrusis measures so Bar 1 remains the first full downbeat measure.
- Update `LessonEngine` slicing, playhead beat calculation, `SteadyPulse` count-in alignment (counting in a full bar minus the pickup duration so the pickup enters smoothly on the correct beat), and `LessonStaffAdapter` / `StaffRenderer` spacing.
- Update `MusicXmlImporter` to preserve `<measure implicit="yes">` partial pickup bars when importing external scores.

### Slice 11.2 — Voice-Led `ChordSkeleton` Generator (`src/core/lesson_engine.cpp`)
- Upgrade `make_skeleton_chord_notes` to perform stateful voice-leading across consecutive harmonies:
  - Anchor the first RH chord comfortably around `C4–C5`.
  - For each subsequent `ChordSymbol`, evaluate all close-position inversions near the previous chord's centroid and select the inversion minimizing total semitone displacement (preserving common tones).
  - Assign valid, strictly ascending RH fingerings (`1–3–5` / `1–2–5` for triads; `1–2–3–5` / `1–2–4–5` for 4-note 7ths; never duplicate fingers like `1–3–5–5`).

### Slice 11.3 — 28-Lesson Ergonomic & Urtext Audit Fixes (`assets/lessons/t*.json`)
- Repair all **44 ergonomic/fingering anomalies** across Tiers 1–6:
  - Replace impossible solid 10ths (`t2_05`, `t3_03`, `t3_04`, `t5_01`, `t5_02`, `t6_02`, `t6_03`) with authentic playable voicings (`<= 12` semitones; `<= 9` semitones in Tiers 1–3).
  - Fix every adjacent same-finger stepwise progression (`5–5`, `1–1`, `2–2`).
- Restore musical fidelity & clear labeling:
  - `t1_03`: Align title and harmonic note (*A-Minor Lament (Leading Tone)* or pure Natural Minor).
  - `t5_01` (Op. 28 No. 7), `t5_03` (Op. 28 No. 4 — including Bar 4 `Am/E` fix), `t5_04` (Waltz B. 150): Restore authentic anacrusis pickup bars and Urtext notes.
  - `t4_02` (Bach Invention No. 1) & `t6_03` (Ballade No. 1 Op. 23 mm. 1–7): Upgrade to authentic Urtext counterpoint and two-hand unison octave opening.
  - Clearly label original/adapted studies (`t6_01`, `t6_04`, Czerny-style studies) as *"Study after..."* where not literal Urtext.

### Slice 11.4 — Automated Ergonomic & Voicing Guardrails (`tests/lesson_library_tests.cpp`, `tests/lesson_engine_tests.cpp`)
- Enforce across all 28 built-in lessons in Catch2:
  1. Max simultaneous span per hand `<= 12` semitones (`<= 9` semitones in Tiers 1–3).
  2. Strictly monotonic fingerings inside every multi-note chord (RH ascending, LH descending).
  3. Zero identical fingers on adjacent 1–2 semitone single-note steps within the same bar.
  4. Voice-led `ChordSkeleton` average centroid jump bounded and zero duplicate chord fingerings.
  5. Pickup-bar timing, count-in alignment, and `build_demo_playback` monotonicity tests.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug` compiles cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
2. `ctest --preset native-debug --output-on-failure` passes 100%, including all new ergonomic, pickup, and voice-leading invariants across all 28 lessons.
3. `emcmake cmake --preset web-release && cmake --build --preset web-release` builds cleanly with 0 warnings and bundles updated `assets/lessons/`.
4. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean on all modified C++ files.
5. Headless Light & Dark screenshots of `t2_05`, `t3_04`, `t5_01` (pickup bar), `t5_02`, and `t6_03` visually confirm clean anacrusis engraving, playable voicings, and smooth Chord Skeleton notation.

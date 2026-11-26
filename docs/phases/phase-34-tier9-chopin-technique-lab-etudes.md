# Phase 34: Tier 9 Curriculum — Chopin Technique Lab & Étude Excerpts (Henle 6–7)

**Status:** `[ ] Planned` (Depends on Phase 22, Phase 29 & Phase 33 — Track D: Missing Middle to Ballade No. 1)  
**Goal:** Author **Tier 9 (5 focused 12–24 bar Chopin Étude & virtuoso technique modules, Henle 6–7)** specifically engineered to isolate and master every physical barrier inside *Ballade No. 1*: voicing inside sweeping arpeggios, polyrhythmic lightness, fiery LH 16th-note endurance, relaxed octaves, and chromatic double-notes.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Jumping directly from *Nocturne Op. 55 No. 1* into *Ballade No. 1*'s *Agitato*, *Eb ff climax*, and *Presto con fuoco Coda* causes physical strain and sloppy execution. Chopin himself wrote the Op. 10 and Op. 25 Études as targeted mechanical laboratories. Mastering **focused 12–24 bar Urtext excerpts** from the five most relevant études—paired with Selfano's **Speed Lab (Phase 22)** and **MIDI Evenness Coaching (Phase 29)**—gives learners the exact physical machinery Op. 23 demands in a fraction of the time.

---

## 2. Planned Tier 9 Roster (5 Focused Technique Modules | Henle 6–7)

| ID | Title & Source | Key | Meter | Target BPM | Length | Exact *Ballade No. 1* Passage Unlocked |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t9_01` | **Aeolian Harp Voicing Study (Étude Op. 25, No. 1 — Opening 16 Bars)** (*F. Chopin*) | `Ab Major` | `4/4` | `96` | 16b | Unlocks *Meno mosso* & *Più animato* sweeping arpeggios with floating pinky soprano melody (`16th` sextuplets). |
| `t9_02` | **Polyrhythmic Lightness Study (Étude Op. 25, No. 2 — Opening 20 Bars)** (*F. Chopin*) | `F Minor` | `2/2` | `104` | 20b | Unlocks *A-major Scherzando* & *Animato* feather-light RH triplets against LH duple/triplet pulse. |
| `t9_03` | **Revolutionary LH Stamina Study (Étude Op. 10, No. 12 — Opening 18 Bars)** (*F. Chopin*) | `C Minor` | `4/4` | `132` | 18b | Unlocks *Agitato* & *Coda* rapid LH 16th-note torrents and fiery RH dotted-rhythm octaves. |
| `t9_04` | **Romantic Octaves & Wrist Bounce Study (Étude Op. 25, No. 10 — Excerpt / Octave Drill)** (*F. Chopin*) | `B Minor` | `2/2` | `108` | 16b | Unlocks *Eb ff Climax* & *Presto Coda* rapid parallel/chromatic octaves using relaxed wrist rotation (`1-4` / `1-5`). |
| `t9_05` | **Chromatic Double-Thirds & Rapid Shifts Study (Op. 10 No. 4 / Op. 25 No. 6 Idiom)** (*F. Chopin*) | `G Minor` | `4/4` | `104` | 16b | Unlocks *Ballade No. 1* Coda chromatic double-notes, interlocking chords, and high-speed hand shifts in **G Minor**. |

---

## 3. Deliverables & Vertical Slices

### Slice 34.1 — Author Tier 9 Étude Excerpts (`assets/lessons/t9_01.json`..`t9_05.json`)
- Author all 5 modules with verified Urtext pitches, tuplets (`6:4`, `3:2`), explicit `is_melody` tags (e.g., soprano top note on beat peaks in `t9_01`), ergonomic Chopin fingerings (`1-4`/`1-5` legato octave alternation in `t9_04`), harmonic/Roman maps, and Speed Lab practice recipes (`4+1` bursts, `Long–Short`, `Hand Blocking`).

### Slice 34.2 — Curriculum Manifest & Tier 9 Chapter Guide (`assets/lessons/curriculum.json`, `assets/lessons/tier_guides.json`)
- Register Tier 9 in `curriculum.json`, link `t8_04 -> t9_01 -> ... -> t9_05`, and author the **Tier 9 Chapter Guide** (*"The Chopin Mechanics Toolkit: Arm Weight, Wrist Rotation & Zero-Tension Velocity"*).

### Slice 34.3 — Integrity Suite & Visual Spot Checks (`tests/lesson_library_tests.cpp`)
- Validate all 5 Tier 9 scores in Catch2 (including polyrhythms, sextuplets, `is_melody` voicing checks, and Demo playback).
- Inspect headless Light & Dark screenshots of `t9_01` (sextuplets), `t9_03` (LH 16th torrent), and `t9_04` (octaves) and verify `web-release`.

---

## 4. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** and passes 100% of Catch2 tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots verify crisp sextuplet/triplet beaming, octave fingerings, and dynamic/slur engraving with zero collisions.

# Phase 33: Tier 8 Curriculum — Romantic Texture, Rubato & Wide LH Strides (Henle 5–6)

**Status:** `[ ] Planned` (Depends on Phase 26 & Phase 32 — Track D: Missing Middle to Ballade No. 1)  
**Goal:** Author **Tier 8 (4 extended authentic Romantic & polyphonic works, Henle 5–6)**—specifically chosen from the conservatory stepping-stone canon directly preceding Chopin's *Ballade No. 1*—training cantabile rubato, wide blind LH strides, Chopin *fioritura* filigree, and 3-voice singing polyphony.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

To play *Ballade No. 1* without mechanical stiffness, a pianist needs deep comfort with Chopin's actual language at intermediate difficulty: **wide LH bass-to-chord strides** in flat keys (`Eb`, `F minor`, `C# minor`), **graceful ornamental turns and triplets** over steady accompaniment, **dramatic chordal climaxes** (*stretto* / *più mosso*), and **three-voice polyphonic voicing** inside one hand.

---

## 2. Planned Tier 8 Roster (4 Lessons | Henle 5–6)

| ID | Title & Source | Key | Meter | Target BPM | Length | Technical & Harmonic Bridge to Op. 23 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t8_01` | **Waltz in C# Minor (Op. 64, No. 2 — Main & Più Mosso Themes)** (*F. Chopin*) | `C# Minor` | `3/4` | `132` | ~48b | Rapid legato 8th-note passagework (*Più mosso*), sharp-key fluency (4 sharps), waltz rubato & pedal clarity. |
| `t8_02` | **Nocturne in Eb Major (Op. 9, No. 2 — Complete Urtext)** (*F. Chopin*) | `Eb Major` | `12/8` | `46` (♩.) | ~34b | **Same key as Ballade No. 1's 2nd theme (`Eb`)!** Blind LH 2-octave leaps, bel canto voicing, ornamental *fioritura*, cadenza ending. |
| `t8_03` | **Nocturne in F Minor (Op. 55, No. 1 — Main & Più Mosso Climax)** (*F. Chopin*) | `F Minor` | `4/4` | `72` | ~48b | Direct architectural cousin of Op. 23: somber funeral LH stride → passionate triplet climax → sparkling scale descent. |
| `t8_04` | **Three-Part Sinfonia / WTC Prelude Study (BWV 787 or BWV 847)** (*J.S. Bach*) | `C Major / C Minor` | `4/4` | `80` | ~28–38b | True 3-voice polyphony & inner-voice hand-sharing needed for Op. 23's lyrical inner melodies. |

---

## 3. Deliverables & Vertical Slices

### Slice 33.1 — Author Tier 8 Urtext Scores (`assets/lessons/t8_01.json`..`t8_04.json`)
- Author all 4 extended scores leveraging Phases 24–26 notation features (tuplets, grace notes, LH treble-clef switches, `8va`, per-section tempo changes, `Arpeggiate` rolls, explicit `is_melody` tags where needed, authentic `Ped. … *` spans, fingerings `1–5`, harmonic/Roman maps, and rich `TheoryLens` briefs).

### Slice 33.2 — Curriculum Manifest & Tier 8 Chapter Guide (`assets/lessons/curriculum.json`, `assets/lessons/tier_guides.json`)
- Register Tier 8 in `curriculum.json`, chain `t7_05 -> t8_01 -> ... -> t8_04`, and author the **Tier 8 Chapter Guide** covering Chopin's rubato grammar, blind LH leap trajectory, and voicing 3-voice textures.

### Slice 33.3 — Integrity Suite & Visual Verification (`tests/lesson_library_tests.cpp`)
- Extend `tests/lesson_library_tests.cpp` for all Tier 8 scores (validating multi-section tempos, grace notes, tuplets, clef changes, `8va`, harmonic maps, and Demo playback).
- Inspect headless Light & Dark screenshots on `t8_01`, `t8_02`, and `t8_03` and verify `web-release` bundle size & clean WebGL rendering.

---

## 4. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** and passes 100% of Catch2 tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots verify crisp `12/8` / `3/4` / `4/4` engraving, grace notes, LH clef changes, and `Ped. … *` lanes with zero collisions.

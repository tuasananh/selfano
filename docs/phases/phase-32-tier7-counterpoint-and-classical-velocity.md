# Phase 32: Tier 7 Curriculum — Counterpoint & Classical Velocity (Henle 4–5)

**Status:** `[ ] Planned` (Depends on Phase 24 & Phase 25 — Track D: Missing Middle to Ballade No. 1)  
**Goal:** Begin bridging the gap from early-intermediate (Tier 6, Henle ~3.5) toward concert Chopin by authoring **Tier 7 (4–5 extended 24–40 bar Urtext/classical studies)** focused on full two-part Bach counterpoint, Classical sonata agility, and 16th-note finger velocity, complete with a **Tier 7 Chapter Guide** and automated musical-integrity verification.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Learners finishing Tier 6 have only played 8–17 bar excerpts and one 8-bar snippet of Bach's Invention No. 1. Before entering intermediate Chopin nocturnes and waltzes, pianists must build **sustained 16th-note finger independence across 24–40 bar complete forms**, clean **thumb-under scale velocity**, and **contrary-motion hand autonomy** without leaning on sustain pedal to hide uneven fingers.

---

## 2. Planned Tier 7 Roster (4–5 Lessons | Henle 4–5)

| ID | Title & Source | Key | Meter | Target BPM | Length | Technical & Harmonic Bridge to Op. 23 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t7_01` | **Two-Part Invention No. 4 in D Minor (BWV 775 — Full)** (*J.S. Bach*) | `D Minor` | `3/8` | `66` (♩.) | ~32–52b | Continuous 16th-note independence, compound minor phrasing, long trills/ornaments & sequences. |
| `t7_02` | **Two-Part Invention No. 8 in F Major (BWV 779 — Full)** (*J.S. Bach*) | `F Major` | `3/4` | `108` | ~34b | Canon imitation, crisp detached arpeggio leaps vs. legato 16th scales, stamina over full piece. |
| `t7_03` | **Two-Part Invention No. 13 in A Minor (BWV 784 — Full)** (*J.S. Bach*) | `A Minor` | `4/4` | `84` | ~25b | Rapid hand-to-hand broken-chord 16ths (directly trains Chopin arpeggio hand shifts & harmonic outline). |
| `t7_04` | **Sonata in C Major (K. 545 — Mvt. 1 Exposition)** (*W.A. Mozart*) | `C Major` | `4/4` | `124` | ~28b | Pearly 16th-note scalar velocity over supple Alberti bass; clean thumb crossings (`1-2-3-1`). |
| `t7_05` | **School of Velocity Study (Op. 299, No. 1 / No. 6)** (*C. Czerny*) | `C / G Major` | `4/4` | `116` | ~24b | Dedicated rapid 16th-note scale/arpeggio velocity & Speed Lab (`Long–Short` / `4+1` burst) showcase. |

---

## 3. Deliverables & Vertical Slices

### Slice 32.1 — Author Tier 7 Lesson Scores (`assets/lessons/t7_01.json`..`t7_05.json`)
- Author all 5 lessons with verified Urtext pitches, accurate measure ticks, ergonomic fingerings (`1–5`), phrase boundaries (`phrases` 4–8 bars each), harmonic/Roman-numeral annotations, dynamics, articulations, slurs, and rich `TheoryLens` briefs (including playable `Hear & Compare` chord examples).

### Slice 32.2 — Curriculum Manifest & Tier 7 Chapter Guide (`assets/lessons/curriculum.json`, `assets/lessons/tier_guides.json`, `src/ui/curriculum_modal.cpp`)
- Add Tier 7 metadata to `curriculum.json`, link `t6_04 -> t7_01 -> ... -> t7_05` in the progression chain, and author the 2-minute **Tier 7 Chapter Guide** in `tier_guides.json` covering Baroque voice independence, thumb-crossing ergonomics, and using Speed Lab bursts.
- Verify `CurriculumModal` scales cleanly past 6 tiers (scrollable tier list / jump pills).

### Slice 32.3 — Automated Musical Integrity & Visual Spot Checks (`tests/lesson_library_tests.cpp`)
- Expand `tests/lesson_library_tests.cpp` to validate all Tier 7 lessons under the full ergonomic, fingering, tie, harmony, demo-playback, and `explain_harmony` invariants.
- Capture headless Light & Dark screenshots of `t7_01`, `t7_03`, and `t7_04` and verify `web-release` bundles all assets cleanly.

---

## 4. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** and passes 100% of Catch2 tests (including full Tier 7 integrity & ergonomic checks).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots confirm crisp 16th-note beaming, ornaments, fingerings, and scrollable Tier 7 browser layout with zero collisions.

# Phase 35: Tier 10A — Chopin *Ballade No. 1, Op. 23* First Half (Urtext mm. 1–125)

**Status:** `[ ] Planned` (Depends on Phase 26 & Phase 34 — Track D: Missing Middle to Ballade No. 1)  
**Goal:** Deliver the complete, unsimplified Urtext **First Half of Chopin's *Ballade No. 1 in G Minor, Op. 23* (mm. 1–125)** as **5 sequenced mastery modules** in Tier 10—covering the *Largo*, full *Moderato*, *Agitato* buildup, *Meno mosso* Eb-major cantabile, and the towering *Eb-major ff climax*—with full harmonic maps, voicing tags, authentic fingerings, pedal markings, and conservatory briefs.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why Split *Ballade No. 1* Across Phases 35 & 36

Chopin's *Ballade No. 1* is 264 dense, multi-voice measures with shifting keys (`G minor -> Eb major -> A minor -> A major -> Eb major -> G minor`), shifting meters (`4/4 -> 6/4 -> 2/2`), complex polyrhythms, and virtuosic cadenzas. Authoring and hand-verifying every Urtext bar, fingering, slur, pedal span, and harmonic annotation in two clean halves (`mm. 1–125` in Phase 35, `mm. 126–264` in Phase 36) guarantees concert-grade musical accuracy and zero rushed engraving.

---

## 2. Planned Tier 10A Module Breakdown (mm. 1–125)

| ID | Module Title | Measures | Key / Meter | Target Focus |
| :--- | :--- | :--- | :--- | :--- |
| `t10_01` | **Op. 23 Part 1 — Largo & Full Moderato Theme** | mm. 1–35 | `G Minor`, `4/4 -> 6/4` | Full authentic Urtext opening (including m. 7 famous dissonant chord), complete `6/4` narrative theme & transitional cadence. |
| `t10_02` | **Op. 23 Part 2 — Agitato & Rising Storm** | mm. 36–67 | `G Minor`, `6/4` | Syncopated LH bass thrusts, rising chromatic inner tension, sweeping arpeggio climax & dramatic pause before the 2nd theme. |
| `t10_03` | **Op. 23 Part 3 — Meno Mosso (Eb Cantabile)** | mm. 68–93 | `Eb Major`, `6/4` | Sotto voce bel canto melody over wide undulating LH `6/4` chords; intimate voicing, syncopated pedaling & poetic rubato. |
| `t10_04` | **Op. 23 Part 4 — Agitato Transition (A Minor)** | mm. 94–105 | `A Minor`, `6/4` | Restless harmonic modulation, rapid chromatic swells, LH stride precision leading into the great climax. |
| `t10_05` | **Op. 23 Part 5 — Eb Major Grand Climax (`ff`)** | mm. 106–125 | `Eb Major`, `6/4` | Full orchestral *fortissimo* chords, sweeping 2-octave LH leaps, ringing top-note voicing, and triumphant cadence into the bridge. |

---

## 3. Deliverables & Vertical Slices

### Slice 35.1 — Author Modules `t10_01.json`..`t10_05.json` (`assets/lessons/`)
- Author complete Urtext mm. 1–125 across 5 modules using all Phase 24–26 score capabilities (32nds, tuplets, grace notes, LH treble-clef switches, `8va`, multi-section tempo markings, `Arpeggiate`, `is_melody`, `voice: 0/1`, dynamics `pp`–`ff`, hairpins, slurs, `Ped. … *`).
- Include complete bar-by-bar harmonic/Roman-numeral maps (enabling `ChordSkeleton` and `Memory Fade` across every bar of Op. 23), practical concert fingerings (`1–5`), and Speed Lab practice recipes in each `TheoryLens`.

### Slice 35.2 — Curriculum Registration & Tier 10 Chapter Guide (`assets/lessons/curriculum.json`, `assets/lessons/tier_guides.json`)
- Add Tier 10 (*"Summit: Chopin Ballade No. 1 in G Minor, Op. 23"*) to `curriculum.json`, chain `t9_05 -> t10_01 -> ... -> t10_05`, and author the **Tier 10 Chapter Guide** mapping the Ballade's complete dramatic arch form and rehearsal strategy.

### Slice 35.3 — Automated Urtext Integrity Suite & Engraving Verification (`tests/lesson_library_tests.cpp`)
- Validate all 5 modules in Catch2 (exact measure tick sums, pitch spelling, ties, fingerings, parseable chord symbols, `explain_harmony` coverage, multi-tempo Demo playback).
- Inspect headless Light & Dark screenshots of `t10_01` (mm. 1–8 `4/4 -> 6/4`), `t10_03` (`Eb` Meno mosso), and `t10_05` (`ff` climax) and verify `web-release`.

---

## 4. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** and passes 100% of Catch2 tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots confirm clean engraving across `4/4 -> 6/4` meter transitions, LH treble clefs, `8va` climaxes, and dense chord voicings with zero collisions.

# Phase 36: Tier 10B — Chopin *Ballade No. 1, Op. 23* Second Half, Coda & Seam Stitching (Urtext mm. 126–264)

**Status:** `[ ] Planned` (Depends on Phase 35 — Track D: Missing Middle to Ballade No. 1)  
**Goal:** Complete the entire 264-measure Urtext **Chopin *Ballade No. 1 in G Minor, Op. 23*** by authoring **mm. 126–264** across **5 sequenced modules** (the *A-major Scherzando waltz*, *Più animato* filigree, *Grand Return*, and the legendary *Presto con fuoco Coda*), plus a lightweight **Section Seam-Stitching / Continuous Run** flow linking Tier 10 modules end-to-end.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Completes Selfano's Ultimate Goal

With mm. 126–264 authored, fingered, harmonically mapped, and paired with Speed Lab + Tempo Ramp + Memory Fade, a self-taught learner can literally progress from their very first C-major 5-finger steps (`t1_01`) all the way through the final blazing G-minor octaves of Bar 264 inside one unified, zero-latency offline trainer.

---

## 2. Planned Tier 10B Module Breakdown (mm. 126–264)

| ID | Module Title | Measures | Key / Meter | Target Focus |
| :--- | :--- | :--- | :--- | :--- |
| `t10_06` | **Op. 23 Part 6 — Scherzando Waltz (A Major)** | mm. 126–165 | `A Major`, `6/4` | Sparkling high-register waltz leaps, playful grace notes, light LH bass-to-chord bounce, and rapid 8th-note swirls. |
| `t10_07` | **Op. 23 Part 7 — Eb Return & Passion Build** | mm. 166–193 | `Eb Major`, `6/4` | Radiant soaring return of the 2nd theme (`ff`), sweeping LH arpeggios & harmonic drive toward the home dominant. |
| `t10_08` | **Op. 23 Part 8 — Dominant Pedal & Buildup to Coda** | mm. 194–207 | `G Minor`, `6/4` | Relentless `D` dominant pedal tension, chromatic rising surges, and electric launch into the Coda. |
| `t10_09` | **Op. 23 Part 9 — Presto con Fuoco Coda (Part 1)** | mm. 208–241 | `G Minor`, `2/2` (`alla breve`) | Ferocious `2/2` tempo, blind 2-octave LH leaps, interlocking syncopated chords, and fiery scalar torrents (built for Speed Lab + Tempo Ramp). |
| `t10_10` | **Op. 23 Part 10 — Presto Coda Finale & Double-Octave Cascade** | mm. 242–264 | `G Minor`, `2/2` | Chromatic octaves, dizzying 2-hand scale plunges, and final tragic `ff` G-minor cadence through Bar 264. |

---

## 3. Deliverables & Vertical Slices

### Slice 36.1 — Author Modules `t10_06.json`..`t10_10.json` (`assets/lessons/`)
- Author complete Urtext mm. 126–264 across 5 modules with verified rhythms, `6/4 -> 2/2` meter shift, `8va`, LH treble clefs, grace notes, tuplets, chromatic double-notes, ergonomic concert fingerings (`1–5`), authentic `Ped. … *` marks, full harmonic/Roman maps, and Speed Lab drill recipes.

### Slice 36.2 — Curriculum Completion & Seam-Stitching Flow (`assets/lessons/curriculum.json`, `src/core/lesson_engine.hpp/.cpp`, `src/ui/lesson_view.cpp`)
- Chain `t10_05 -> t10_06 -> ... -> t10_10` in `curriculum.json` and update Tier 10 Chapter Guide with Coda stamina & seam-practice guidance.
- Add a lightweight **"Chain Next Module"** / **Seam Practice** action when finishing a Tier 10 module (loading the last 4 bars of Part `N` + first 4 bars of Part `N+1`, or flowing smoothly into the next module) so learners master every section transition across all 264 bars.

### Slice 36.3 — Full-Curriculum Integrity Audit & End-to-End Browser Verification (`tests/lesson_library_tests.cpp`)
- Run full Catch2 musical-integrity verification across **every lesson in Tiers 1–10** (confirming contiguous chain from `t1_01` to `t10_10`, exact measure ticks, ergonomic fingerings, harmonic maps, and clean Demo playback).
- Inspect headless Light & Dark screenshots of `t10_06` (*Scherzando*), `t10_09` (*Presto Coda*), and `t10_10` (final bars) + live headless Chromium CDP verification on `build/web-release` confirming 0 console/runtime/HTTP errors and smooth WebGL performance on dense Coda bars.

---

## 4. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** and passes 100% of Catch2 tests across the complete Tiers 1–10 curriculum.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings** and bundles all Tier 1–10 lessons into `selfano.data`.
3. Live Chromium CDP verification on `build/web-release` passes with **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors** on `t10_09` and `t10_10`.
4. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
5. Headless Light & Dark screenshots verify crisp engraving on *Scherzando* leaps and *Presto con fuoco* Coda passages with zero collisions.

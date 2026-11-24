# Phase 9: Complete 28-Lesson Classical Curriculum (*Road to Chopin Ballade No. 1*)

**Status:** `[ ] Planned` (Depends on Phase 8)  
**Goal:** Expand the 6 starter lessons from Phase 8 into the complete **28-lesson, 6-tier progressive classical curriculum** in `assets/lessons/`, covering keys outward around the circle of fifths from `C / Am / G / F` through `Gm / Eb / Cm / F#m / Ab`, with musically verified notes, rhythms, ties, fingerings (`1–5`), chord & Roman-numeral harmonic maps, and tailored Theory Lens notes—all verified by automated Catch2 musical-integrity tests and audio/visual spot checks.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Is Its Own Phase

Authoring ~380+ bars of two-hand classical piano music with accurate harmonies, realistic fingerings, exact measure tick sums, clean phrase boundaries, and clear pedagogical commentary requires focused musical craftsmanship. Isolating curriculum authoring into Phase 9 guarantees zero rushed scores and thorough per-tier verification.

---

## 2. Full 6-Tier Curriculum Roster (28 Lessons)

*(Items marked `[Starter — Phase 8]` are already in place and audited here for consistency alongside the 22 newly authored lessons.)*

### Tier 1 — Reading Foundations & Hand Independence (5 Lessons | 0–1 Sharp/Flat)
*Eliminate single-note counting; anchor landmarks (`G4`, `C4`, `F3`), 5-finger shapes, and steady two-hand alignment.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t1_01` | **Five-Finger Steps & Tonic Drone** (*Beyer Op. 101 style*) `[Starter]` | `C Major` | `4/4` | `76` | 8 | Stepwise RH over whole-note LH `I–V–I` anchors; reading by 2nds & 3rds. |
| `t1_02` | **Parallel & Contrary 5-Finger Motion** (*Czerny Op. 599, No. 11*) | `C Major` | `4/4` | `84` | 8 | `I–IV–V7–I` block harmony; keeping hands relaxed and synchronized. |
| `t1_03` | **Natural Minor Lament** (*Beginner Russian School / Beyer minor study*) | `A Minor` | `4/4` | `76` | 8 | Minor tonic `i–iv–V–i` with `G#` leading tone; cantabile legato. |
| `t1_04` | **First Sharp & Waltz Pulse** (*Czerny Op. 599, No. 16*) | `G Major` | `3/4` | `88` | 12 | `F#` in key signature; `3/4` pulse (`1–2–3`); `I–V6/5–I` shapes. |
| `t1_05` | **First Flat & Singing Quarter Melody** (*Czerny Op. 599, No. 19*) | `F Major` | `4/4` | `84` | 12 | `Bb` in key signature; dotted half notes + LH blocked triads (`I–IV–ii–V–I`). |

### Tier 2 — Accompaniment Patterns & 8th-Note Flow (5 Lessons | Up to 1 Sharp/Flat)
*Automate classic left-hand accompaniment patterns so eyes read contour and harmonic grips instead of isolated dots.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t2_01` | **Intro to Alberti Bass** (*Czerny Op. 599, No. 22*) | `C Major` | `4/4` | `88` | 12 | `low–high–mid–high` 8th-note LH; chunking `I–V6/5–I–IV` as 1 grip/bar. |
| `t2_02` | **Waltz Accompaniment & Hand Shifts** (*Czerny Op. 599, No. 29*) | `G Major` | `3/4` | `96` | 16 | Bass–chord–chord LH waltz; clean thumb-under RH position shift. |
| `t2_03` | **Melancholy Broken Chords** (*Gurlitt / Czerny Op. 599 minor study*) | `E Minor` | `4/4` | `80` | 12 | `i–VI–III–VII–i` progression (classical & anime staple); flowing 8th arpeggios. |
| `t2_04` | **Dotted Rhythms & Ties** (*Czerny Op. 599, No. 38*) `[Starter]` | `F Major` | `4/4` | `84` | 12 | Dotted-quarter + 8th rhythm and barline ties; subdividing cleanly. |
| `t2_05` | **Dramatic D-Minor Cadences** (*Czerny Op. 599, No. 45*) | `D Minor` | `4/4` | `88` | 16 | `i–iv–i6/4–V7–i` with `C#` leading tone; staccato bass vs. legato melody. |

### Tier 3 — Two-Voice Independence & Romantic Articulation (5 Lessons | Up to 2 Sharps/Flats)
*Read genuinely independent Baroque voices and shape Romantic articulation & rapid chord-tone figuration.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t3_01` | **Minuet in G Major (BWV Anh. 114 — Part A)** (*Petzold / Bach Anna Magdalena*) `[Starter]` | `G Major` | `3/4` | `104` | 16 | Independent two-voice counterpoint; scale fragments vs. broken triad outlines. |
| `t3_02` | **Musette in D Major (BWV Anh. 126 — Main Theme)** (*Bach Anna Magdalena*) | `D Major` | `2/4` | `96` | 16 | Two sharps (`F#`, `C#`); octave drone bass vs. crisp 8th-note RH jumps; `2/4` meter. |
| `t3_03` | **La Candeur (Op. 100, No. 1 — Theme)** (*F. Burgmüller*) | `C Major` | `4/4` | `100` | 16 | Romantic phrasing, scalar runs, and tonic/dominant cadence clarity. |
| `t3_04` | **Arabesque (Op. 100, No. 2 — Main Section)** (*F. Burgmüller*) | `A Minor` | `2/4` | `112` | 16 | 16th-note 5-finger bursts over crisp LH chords; modulation to relative `III` (C major). |
| `t3_05` | **Minuet in G Minor (BWV Anh. 115 — Part A)** (*Petzold / Bach Anna Magdalena*) | `G Minor` | `3/4` | `96` | 16 | First deep dive into **G minor** (home key of Ballade No. 1): `Bb`, `Eb`, `F#`, melodic minor. |

### Tier 4 — Harmonic Chunking & Classical Form (5 Lessons | 2–3 Sharps/Flats)
*Accelerate piece learning through Classical modulations, secondary dominants, inversions, and 16th-note independence.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t4_01` | **Sonatina in C Major (Op. 36, No. 1 — Mvt. 1 Exposition)** (*M. Clementi*) | `C Major` | `4/4` | `108` | 16 | Tonic-to-Dominant modulation (`I → V/V → V`); broken-chord RH + Alberti LH. |
| `t4_02` | **Two-Part Invention No. 1 (BWV 772 — Opening 8 Bars)** (*J.S. Bach*) | `C Major` | `4/4` | `76` | 8 | Imitative 16th-note motif passed between hands; premier independence builder. |
| `t4_03` | **B-Minor Expressive Study** (*Köhler / Heller Op. 47 style*) `[Starter]` | `B Minor` | `6/8` | `66` (♩.) | 16 | Compound `6/8` meter (`1-2-3, 4-5-6` felt in 2); secondary dominants (`V7/iv`, `V7/V`). |
| `t4_04` | **Bb-Major Singing Arpeggios** (*Burgmüller / Classical arpeggio study*) | `Bb Major` | `4/4` | `92` | 16 | Relative major of G minor (`Bb`, `Eb`); rolling 16th-note accompaniment + lyrical RH. |
| `t4_05` | **Ballade (Op. 100, No. 15 — Opening Dramatic Theme)** (*F. Burgmüller*) | `C Minor` | `6/8` | `72` (♩.) | 16 | Three flats (`Bb`, `Eb`, `Ab`); stormy LH octave/chord pulse + dramatic storytelling. |

### Tier 5 — Early Chopin & Romantic Voicing (4 Lessons | 3–4 Sharps/Flats)
*Enter Chopin's musical language: cantabile top-note voicing, wide LH stride leaps, chromatic inner voices, and rich flat/sharp keys.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t5_01` | **Prelude in A Major (Op. 28, No. 7 — Full Piece)** (*F. Chopin*) | `A Major` | `3/4` | `84` | 16 | Complete authentic Chopin work! Three sharps (`F#`, `C#`, `G#`); wide LH reach + `V7/ii` (`F#7`). |
| `t5_02` | **Prelude in C Minor (Op. 28, No. 20 — Full Piece)** (*F. Chopin*) `[Starter]` | `C Minor` | `4/4` | `56` | 13 | Complete authentic Chopin work! Thick 4-note chord voicing, diminished 7ths, chromatic bass. |
| `t5_03` | **Prelude in E Minor (Op. 28, No. 4 — Opening 12 Bars)** (*F. Chopin*) | `E Minor` | `4/4` | `60` | 12 | Masterclass in chromatic voice-leading: LH triads shifting 1 half-step at a time. |
| `t5_04` | **Waltz in A Minor (B. 150 — Main Theme)** (*F. Chopin*) | `A Minor` | `3/4` | `116` | 16 | Classic Chopin LH bass-to-chord jump + singing legato RH (`i–iv–VII–III–VI–ii°–V7–i`). |

### Tier 6 — Bridge to Chopin Ballade No. 1 (4 Lessons | Advanced Keys & Op. 23 Idioms)
*Directly train the exact harmonic, rhythmic, and technical idioms required to begin studying Ballade No. 1 in G minor, Op. 23.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t6_01` | **Eb-Major Cantabile & Wide LH Leaps** (*Chopin Nocturne Op. 9 No. 2 idiom study*) | `Eb Major` | `6/8` | `54` (♩.) | 12 | Secondary key area of Ballade No. 1; singing RH over wide compound-meter LH leaps. |
| `t6_02` | **Sharp-Key Chromatic Voicing** (*Romantic Prelude study in F# Minor*) | `F# Minor` | `4/4` | `68` | 12 | 3–4 sharps reading fluency (`F#`, `C#`, `G#`, `E#`); diminished-7th pivots & inner voices. |
| `t6_03` | **Ballade No. 1 — Opening Largo & Neapolitan Study** (*F. Chopin, Op. 23 mm. 1–7*) | `G Minor` | `4/4` | `52` | 8 | The real opening of Ballade No. 1! Unison octave arpeggio on **Neapolitan 6th (`Ab/C = bII6`)** → `V7 → i`. |
| `t6_04` | **Ballade No. 1 — Moderato Theme Study (6/4 Lilt)** (*F. Chopin, Op. 23 mm. 8–23*) `[Starter]` | `G Minor` | `6/4` | `96` | 16 | Iconic `6/4` waltz-barcarolle main theme: LH `bass–chord–rest` lilt + singing RH over `i–iv6–V7/V–V7`. |

---

## 3. Deliverables & Vertical Slices

### Slice 9.1 — Tiers 1–2 Lessons (`t1_01`..`t2_05`, 10 Lessons Total)
- Author remaining 8 JSON lesson files in `assets/lessons/`, update `curriculum.json`, and run Catch2 validator.

### Slice 9.2 — Tiers 3–4 Lessons (`t3_01`..`t4_05`, 10 Lessons Total)
- Author remaining 8 JSON lesson files with accurate Baroque/Classical counterpoint, 16th-note beaming, and `2/4 · 3/4 · 4/4 · 6/8` meters.

### Slice 9.3 — Tiers 5–6 Lessons (`t5_01`..`t6_04`, 8 Lessons Total)
- Author remaining 6 Romantic & Ballade No. 1 Bridge lessons with authentic Chopin voicings, chromatic accidentals, `bII6` Neapolitan harmony, and playable fingerings.

### Slice 9.4 — Automated Musical Integrity Suite & Spot Checks
- Expand `tests/lesson_library_tests.cpp` to enforce across **all 28 lessons**:
  1. Unique IDs, contiguous tier ordering, non-empty `TheoryLens` & `composer_source`.
  2. Every measure's primary voice sums strictly to `TimeSignature::measure_ticks()`.
  3. Every note's `midi == spelled.to_midi()`, within `[21, 108]`, and fingerings in `0..5`.
  4. Every `tie_start` has a valid matching `tie_stop` (no dangling ties).
  5. Every `HarmonicAnnotation::chord_symbol` parses cleanly via `parse_chord_symbol`, and `ChordSkeleton` builds `>= 1` target per lesson.
  6. `build_demo_playback` produces monotonically ordered, non-zero-duration MIDI events with zero stuck notes.
  7. Key coverage spans at least 13 distinct major/minor keys progressing outward around the circle of fifths.
- Visual spot checks (screenshots) on representative newly added lessons from every tier (`t1_05`, `t2_03`, `t3_04`, `t4_02`, `t5_01`, `t6_03`) + verify Emscripten `web-release` bundles all 28 lessons cleanly into `selfano.data`.

---

## 4. Exit Gate (Verification Criteria)

1. All 28 lessons pass 100% of Catch2 musical-integrity tests (`ctest --preset native-debug --output-on-failure`).
2. Zero compiler warnings on both `native-debug` and `web-release`.
3. Visual screenshot spot checks confirm crisp notation, readable chord/Roman labels, non-overlapping fingerings, and clean ties across all 6 tiers.
4. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.

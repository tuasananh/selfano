# Phase 7: Stage 3 Replacement — Guided Lessons Curriculum, Classical Notation & MusicXML Importer (*Road to Chopin Ballade No. 1*)

**Status:** Planned (Design Locked — Ready for Implementation)  
**Goal:** Replace Stage 3 (Companion Mode) in the 30-minute Autopilot session with a book-anchored, theory-integrated **Guided Lessons & Curriculum** engine featuring a 6-tier, 28-lesson progressive classical syllabus toward *Chopin Ballade No. 1 (Op. 23)*, full classical Grand Staff engraving, a Learn + Revise consistency loop, and a cross-platform `.musicxml` / `.mxl` score importer. Tuck Companion Mode neatly under Free Practice / Tools without removing it.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `midi-emscripten-interop`, `verification-and-review`

---

## 1. Motivation & Locked Design Decisions

### Why This Change Exists
- **Companion Mode's Limitation:** Stage 3 occupies `10:00` (one-third of the daily 30-minute Autopilot). Companion Mode is a passive pitch-class chord tracker designed for external paper/PDF scores—it does not teach reading, fingering, rhythm, hand independence, or musical phrasing, and its loose matching does not correct rhythmic or melodic mistakes.
- **Target Outcome:** Build fluent sight-reading, applied keyboard harmony, and progressive classical technique on a structured multi-year trajectory toward *Chopin's Ballade No. 1 in G minor, Op. 23*.

### Locked Decisions (Design Specification)
1. **Stage 3 Role & Companion Mode Retention:**
   - Autopilot **Stage 3 (`10:00`)** becomes **Guided Lessons & Curriculum**.
   - **Companion Mode** is preserved intact (zero code/test deletion, existing `profile.json` data preserved) and moved out of Autopilot into **Free Practice / Tools** (secondary sidebar section or Tools view).
2. **Content Sourcing:**
   - **Built-in Curated Classical Curriculum:** 28 hand-verified, public-domain-anchored lessons across **6 progressive tiers** stored as data files in `assets/lessons/` (automatically copied on desktop and preloaded into Emscripten VFS).
   - **External Book Import (`My Books` Track):** Cross-platform `.musicxml`, `.xml`, and compressed `.mxl` importer (native file picker, Web file picker, and drag-and-drop), automatically slicing longer imported pieces into selectable 4-to-8 bar phrase chunks inside a single lesson with editable title, target BPM, and optional chord labels.
3. **Embedded Applied Theory (Every Lesson):**
   - Each lesson includes a collapsible **"Theory & Pattern Lens"** card (key, meter, harmonic structure, fingering/technique tip), **chord & Roman-numeral labels rendered directly above the staff**, and an optional **Step 1: Play the Chord Skeleton** warm-up drill (1 chord per bar) before playing the full score.
4. **Authentic Keys + Sync Weekly Key by Default:**
   - Every lesson stays in its **authentic original key**, sequenced to progress outward around the circle of fifths from easy (`C`, `Am`, `G`, `F`, `Em`, `Dm`) to advanced (`D`, `Bm`, `Bb`, `Gm`, `A`, `F#m`, `Eb`, `Cm`, `E`, `Ab`).
   - **`sync_weekly_key_to_lesson`** is **ON by default** so Stage 1 (Warm-Up Circuit), Stage 2B (Continuous Etudes), and Stage 4B (Ear Trainer) automatically reinforce the exact key of the active Stage 3 lesson.
5. **Learn + Revise Loop (Consistency Engine):**
   - When mastered lessons are due for guided revision (SM-2 spaced intervals), Autopilot Stage 3 auto-splits: **`~03:00` Guided Score Revision** of a due lesson + **`~07:00` Active Lesson** (or full `10:00` on the active lesson when nothing is due).
   - Mastering a lesson offers a **one-click promotion to Stage 4A SRS** ("Play from Memory — No Sheet Music") pre-filled with the piece name, section, and harmonic roadmap.
6. **Guided Mastery with Free Override:**
   - Every lesson provides **Listen Demo** (`MIDI OUT` / `AudioSynth`), **Chord Skeleton**, **RH Only / LH Only / Hands Together** isolation, **4–8 bar Phrase Looping**, **Wait-for-Note** ("Learn") mode, and **Steady-Pulse Tempo Ladder (`60% → 80% → 100%` target BPM)**.
   - A lesson auto-graduates to **`Mastered`** upon completing a full Hands-Together run at `≥90%` accuracy at `100%` target BPM (auto-advancing the recommended active lesson and scheduling its first SM-2 guided revision).
   - Users can freely jump to, unlock, or manually mark/unmark any lesson at any time so they are never artificially blocked.
7. **Classical Reading Essentials in Grand Staff Engraving:**
   - Extend `staff_renderer` to support **8th & 16th notes/rests** with automatic beaming and SMuFL flags, **dotted durations** (dotted half, dotted quarter, dotted 8th), **ties**, **time signatures (`2/4`, `3/4`, `4/4`, `6/8`, `6/4`)**, **fingering numbers (`1–5`)**, and **chord/Roman-numeral symbols above the staff**.

---

## 2. Architecture & Dependency Boundaries

Strict compliance with `AGENTS.md`:
- **`piano_core` (`src/core/`)** MUST remain 100% hardware-, UI-, and clock-agnostic (zero `#include <SDL3/...>`, `<imgui.h>`, `<libremidi/...>`, `<emscripten.h>`, `steady_clock::now`, or `random_device`).
- **New `FetchContent` Dependencies (Pinned, Identical on Native + Emscripten):**
  - **`pugixml`** (e.g., `v1.15`, static library, `PUGIXML_BUILD_TESTS OFF`): lightweight, zero-dependency, warning-clean C++ XML DOM parser used privately inside `piano_core` for `.musicxml` parsing.
  - **`miniz`** (or minimal pinned single-file inflate/zip library, e.g., `miniz` `3.0.2`, static library): pure C ZIP/DEFLATE reader used privately inside `piano_core` to unpack `.mxl` archives (`META-INF/container.xml` → root `.musicxml` entry).
  - *Alternative check:* Both `pugixml` and `miniz` compile cleanly under Emscripten WASM and GCC/Clang/MSVC without OS libraries (no system `zlib` required).
- **Asset & Persistence Paths:**
  - **Built-in curriculum:** `assets/lessons/curriculum.json` (index/manifest) + `assets/lessons/t1_*.json` .. `assets/lessons/t6_*.json` (or `.musicxml`). Automatically copied on desktop via existing `POST_BUILD` step and preloaded into Emscripten VFS via existing `--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets`.
  - **User-imported scores:** Stored as sanitized `.json` (normalized `LessonScore`) or `.musicxml` inside `<pref_dir>/lessons/<id>.json` on desktop (`SDL_GetPrefPath("selfano", "selfano")`, or sibling directory when `--profile <path>` is used) and `/persist/lessons/<id>.json` on Emscripten (`IDBFS`, flushed via existing `FS.syncfs(false)`).

---

## 3. Core Data Models (`src/core/`)

### 3.1 Musical Score Representation (`src/core/lesson_score.hpp/.cpp`)

Use an exact rational tick grid so `4/4`, `3/4`, `2/4`, `6/8`, `6/4`, dotted rhythms, 16th notes, and ties never suffer floating-point drift:
- **`kTicksPerQuarter = 480`** (divisible cleanly by 2, 3, 4, 6, 8, 12, 16, 24, 32 — supports quarter `480`, dotted quarter `720`, 8th `240`, dotted 8th `360`, 16th `120`, triplet 8th `160`, half `960`, dotted half `1440`, whole `1920`).

```cpp
struct TimeSignature {
  std::uint8_t numerator{4};    // 2, 3, 4, 6
  std::uint8_t denominator{4};  // 4 or 8
  [[nodiscard]] constexpr int measure_ticks() const noexcept; // numerator * (480 * 4 / denominator)
  [[nodiscard]] constexpr int beat_ticks() const noexcept;    // compound 6/8 -> 720 (dotted quarter); simple -> 480 * 4 / denominator
  [[nodiscard]] constexpr int beats_per_measure() const noexcept; // 6/8 -> 2 dotted-quarter beats (or 6 eighth pulses), 6/4 -> 2 dotted-half or 6 quarter pulses
};

enum class NoteBaseDuration : std::uint8_t {
  Sixteenth = 0, // 120 ticks
  Eighth = 1,    // 240 ticks
  Quarter = 2,   // 480 ticks
  Half = 3,      // 960 ticks
  Whole = 4,     // 1920 ticks
};

struct RhythmicValue {
  NoteBaseDuration base{NoteBaseDuration::Quarter};
  bool dotted{false}; // adds 50% (e.g. dotted quarter = 720 ticks)
  [[nodiscard]] constexpr int ticks() const noexcept;
};

struct LessonNote {
  SpelledNote spelled{};
  MidiPitch midi{60};
  std::uint8_t finger{0};   // 0 = unspecified, 1..5 = printed fingering
  bool tie_start{false};    // tied forward to next matching pitch in same staff
  bool tie_stop{false};     // continuation of previous note (not re-attacked on MIDI!)
};

struct LessonEvent {
  int start_tick{0};        // offset from start of measure [0, measure_ticks)
  RhythmicValue duration{};
  bool is_rest{false};
  std::uint8_t voice{0};    // 0 = primary voice, 1 = optional secondary voice in same staff
  std::vector<LessonNote> notes;
};

struct HarmonicAnnotation {
  int start_tick{0};
  std::string chord_symbol;  // e.g. "Dm", "G7/B", "Eb/G" (parsed via parse_chord_symbol)
  std::string roman_numeral; // e.g. "i", "V6/5", "bII6"
};

struct LessonMeasure {
  std::uint32_t bar_number{1};
  std::optional<KeySignature> key_change{};    // mid-piece modulation if any
  std::optional<TimeSignature> meter_change{}; // mid-piece meter change if any
  std::vector<LessonEvent> treble_events;      // RH
  std::vector<LessonEvent> bass_events;        // LH
  std::vector<HarmonicAnnotation> harmony;     // rendered above staff + powers Chord Skeleton!
};

struct LessonPhrase {
  std::string label;        // e.g. "Phrase A (Bars 1-4)"
  std::size_t start_bar{0}; // 0-based inclusive
  std::size_t end_bar{3};   // 0-based inclusive
};

struct TheoryLens {
  std::string summary;         // 1-2 sentences: musical goal & why this study matters
  std::string pattern_tip;     // What visual/harmonic shape to chunk instead of single dots
  std::string technique_tip;   // Hand posture, fingering shift, voicing, or articulation cue
  std::string ballade_link;    // Optional 1-line link to how this builds toward Ballade No. 1
};

struct LessonMetadata {
  std::string id;              // stable slug, e.g. "t1_01_beyer_five_finger_c" or "imp_<hash>"
  std::uint8_t tier{1};        // 1..6 for built-in, 0 for My Books / Imported
  std::uint8_t order_in_tier{1};
  std::string title;           // e.g. "No. 1 — Five-Finger Melody & Drone"
  std::string composer_source; // e.g. "F. Beyer, Op. 101 (adapted)" / "J.S. Bach — Anna Magdalena"
  KeySignature key{0, Mode::Major};
  TimeSignature time_signature{4, 4};
  double target_bpm{84.0};     // 100% mastery tempo (quarter-note BPM, or dotted-quarter in 6/8)
  TheoryLens theory{};
  bool is_imported{false};
  std::string source_filename{}; // for imported .musicxml/.mxl
};

struct LessonScore {
  LessonMetadata meta{};
  std::vector<LessonMeasure> measures;
  std::vector<LessonPhrase> phrases; // auto-computed 4-8 bar chunks if omitted
};
```

**Critical MIDI Grading Rule for Ties:**
- Notes with `tie_stop == true` are **sustained continuations**, NOT new MIDI `NoteOn` attacks. They are rendered with a curved tie arc from the preceding matching notehead, and the `LessonEngine` excludes `tie_stop` notes from required fresh `NoteOn` targets!

### 3.2 Lesson Runner Engine (`src/core/lesson_engine.hpp/.cpp`)

```cpp
enum class LessonPracticeMode : std::uint8_t {
  ChordSkeleton = 0, // 1 chord per bar (harmonic annotations -> playable voicing on Grand Staff)
  WaitForNote = 1,   // Untimed note-by-note / chord-by-chord learning
  SteadyPulse = 2,   // Metronome + beat-window grading (Tempo Ladder: 60% / 80% / 100%)
};

enum class LessonHandMode : std::uint8_t {
  RightHand = 0,
  LeftHand = 1,
  HandsTogether = 2,
};

enum class TempoLadderStep : std::uint8_t {
  Slow60 = 0,   // 60% of target_bpm (clamped >= 40 BPM)
  Medium80 = 1, // 80% of target_bpm
  Target100 = 2,// 100% of target_bpm
  Custom = 3,   // User-adjusted BPM via +/- controls
};

enum class LessonMasteryStatus : std::uint8_t {
  New = 0,
  Learning = 1,
  Mastered = 2,
  NeedsReview = 3, // derived when Mastered && today_epoch_day >= due_epoch_day
};

struct LessonProgress {
  std::string lesson_id;
  LessonMasteryStatus status{LessonMasteryStatus::New};
  bool skeleton_completed{false};
  bool rh_clean{false};
  bool lh_clean{false};
  bool ladder_60_clean{false};
  bool ladder_80_clean{false};
  double best_accuracy{0.0};     // 0.0..1.0 on full Hands-Together SteadyPulse
  double best_clean_bpm{0.0};    // highest BPM achieved at >= 90% accuracy (full piece, HT)
  std::uint32_t total_runs{0};
  std::uint32_t practice_seconds{0};
  bool manual_override{false};   // true if user manually marked mastered
  bool promoted_to_srs{false};
  // Guided Revision SM-2 fields (reuses review_card math from srs_deck.hpp)
  int review_interval_days{0};
  double review_ease_factor{kDefaultEaseFactor};
  int review_repetitions{0};
  std::int64_t review_due_epoch_day{0};
};
```

**Core Engine Behaviors:**
1. **Target Extraction (`build_targets`):**
   - Given `(score, hand_mode, practice_mode, phrase_range)`:
     - In `ChordSkeleton` mode: extracts one target per `HarmonicAnnotation` (voicing the chord in root position or slash bass in LH + close triad/7th in RH, or accepting any clean pitch-class realization in either octave!). Wait — for Chord Skeleton, displaying a clean playable LH bass + RH chord voicing on the staff and accepting matching pitch-classes (octave-flexible) feels musical and frustration-free.
     - In `WaitForNote` / `SteadyPulse` mode: merges simultaneous attack ticks across selected hand(s) (excluding `is_rest` and `tie_stop` notes) into ordered `LessonTarget`s with exact `tick_offset`, `bar_index`, `expected_midi`, `hit/missed/pending` status, and `played_midi`.
2. **Listen Demo Generator (`build_demo_playback`):**
   - Produces `std::vector<MidiPlaybackNote>` for the active phrase (or full piece) at the currently selected BPM (and selected hand(s), so you can listen to LH-only or Hands-Together!), handling tied durations by summing contiguous tied note lengths into a single sustained MIDI note.
3. **Evaluation & Timing Window:**
   - **Wait-for-Note:** Advances when all expected notes at the active tick are played (using `PolyphonicChordGrouper` window); highlights wrong notes in Red with target vs. played badge without advancing until corrected.
   - **Steady-Pulse:** 1-bar count-in (`4` beats in `4/4`, `3` beats in `3/4`, `2` dotted-quarter beats or `6` clicks in `6/8`) → smooth playhead advancing by `ticks_per_sec = (bpm / 60.0) * 480.0` → hit tolerance window `±max(140ms, 0.38 * beat_ms)` so fast 8th/16th passages remain playable without phantom misses.
   - **Phrase Loop:** When looping a selected phrase (`[start_bar, end_bar]`), reaching the end plays a subtle chime, updates phrase stats, and loops cleanly after a short 1-bar breath (or immediately in Wait-for-Note).
4. **Mastery & SM-2 Guided Revision Graduation:**
   - Completing a **Full Piece** + **Hands Together** + **Steady Pulse** run with `accuracy >= 0.90`:
     - Updates `best_accuracy` and `best_clean_bpm`.
     - Checks off `ladder_60_clean` (`bpm >= 0.60 * target_bpm - 0.5`), `ladder_80_clean` (`bpm >= 0.80 * target_bpm - 0.5`).
     - If `bpm >= target_bpm - 0.5`: graduates `status = Mastered` (if not already), schedules initial guided revision via SM-2 (`Good` → 1 day, then 6 days, then `interval * ease`), and raises a `just_mastered` event so the UI displays a congratulatory pill + **"Add to Stage 4A Memory SRS"** one-click action.
   - Completing a **Guided Revision** run on a due mastered lesson:
     - `accuracy >= 0.90` at `>= 0.90 * target_bpm` → auto-rates SM-2 `Good` (advances interval cleanly).
     - `0.75 <= accuracy < 0.90` → suggests/rates SM-2 `Hard`.
     - `< 0.75` → suggests/rates SM-2 `Again` (re-queues tomorrow) while letting the user confirm or override.
5. **Autopilot Stage 3 Auto-Split Planner (`plan_stage3_session`):**
   - Inputs: `curriculum`, `progress_map`, `active_lesson_id`, `today_epoch_day`, `total_stage3_sec = 600.0`.
   - Finds the most overdue mastered lesson (`review_due_epoch_day <= today_epoch_day`).
   - If a revision is due: schedules **Phase A: Guided Revision (`180s` / 3:00)** followed by **Phase B: Active Lesson (`420s` / 7:00)**, transitioning cleanly at a bar/run boundary (never mid-measure).
   - If no revision is due: schedules full **`600s` (10:00)** on the active lesson.
6. **Safe Boundary Helper (`is_mid_run` / `is_mid_bar`):**
   - Prevents Autopilot from cutting off the user mid-measure during Steady-Pulse playback.

---

## 4. Complete 6-Tier Classical Curriculum Syllabus (28 Built-In Lessons)

Designed specifically to bridge from **slow note-by-note decoding & muscle-memory forgetting** to **fluent score reading, applied harmonic chunking, and Chopin Ballade No. 1 (Op. 23 in G minor)**.

Every built-in lesson is **8–24 bars**, musically authentic (public-domain compositions or carefully graded pedagogical studies modeled on classical works), verified for exact measure tick sums, playable fingerings (`1–5`), chord + Roman-numeral labels, and progressive key exposure around the circle of fifths.

### Tier 1 — Reading Foundations & Hand Independence (5 Lessons | 0–1 Sharp/Flat)
*Goal: Eliminate single-note counting; anchor landmarks (`G4`, `C4`, `F3`), 5-finger shapes, and basic two-hand alignment.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t1_01` | **Five-Finger Steps & Tonic Drone** (*Beyer Op. 101, Nos. 12–14 style*) | `C Major` | `4/4` | `76` | 8 | Stepwise RH over whole-note LH `I–V–I` bass anchors; reading by 2nds & 3rds. |
| `t1_02` | **Parallel & Contrary 5-Finger Motion** (*Czerny Op. 599, No. 11*) | `C Major` | `4/4` | `84` | 8 | `I–IV–V7–I` block harmony; keeping hands relaxed and synchronized. |
| `t1_03` | **Natural Minor Lament** (*Beyer / Russian School beginner study*) | `A Minor` | `4/4` | `76` | 8 | Minor tonic `i–iv–V–i` (introducing leading tone `G#` accidental); expressive legato. |
| `t1_04` | **First Sharp & Treble-Bass Handover** (*Czerny Op. 599, No. 16*) | `G Major` | `3/4` | `88` | 12 | Reading `F#` in key signature; `3/4` waltz pulse (`1–2–3`); `I–V6/5–I` shapes. |
| `t1_05` | **First Flat & Singing Quarter Melody** (*Czerny Op. 599, No. 19*) | `F Major` | `4/4` | `84` | 12 | Reading `Bb` in key signature; dotted half notes + LH blocked triads (`I–IV–ii–V–I`). |

### Tier 2 — Accompaniment Patterns & 8th-Note Flow (5 Lessons | Up to 1 Sharp/Flat)
*Goal: Automate left-hand accompaniment patterns (Alberti bass, waltz, broken chords) so eyes focus on melodic contour and harmonic chunks.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t2_01` | **Intro to Alberti Bass** (*Czerny Op. 599, No. 22*) | `C Major` | `4/4` | `88` | 12 | Classic `low–high–mid–high` 8th-note LH pattern; chunking `I–V6/5–I–IV` as 1 grip per bar. |
| `t2_02` | **Waltz Accompaniment & Hand Shifts** (*Diabelli / Czerny Op. 599, No. 29*) | `G Major` | `3/4` | `96` | 16 | Bass-chord-chord LH pattern (`1–2–3`); clean thumb-under shift in RH melody. |
| `t2_03` | **Melancholy Broken Chords** (*Gurlitt / Czerny Op. 599 minor study*) | `E Minor` | `4/4` | `80` | 12 | `i–VI–III–VII–i` progression (ubiquitous in classical & anime!); flowing 8th-note arpeggios. |
| `t2_04` | **Dotted Rhythms & Ties** (*Czerny Op. 599, No. 38*) | `F Major` | `4/4` | `84` | 12 | Dotted-quarter + 8th rhythm and barline ties; subdividing cleanly without rushing. |
| `t2_05` | **Dramatic D-Minor Cadences** (*Czerny Op. 599, No. 45 / Harmonic Minor*) | `D Minor` | `4/4` | `88` | 16 | `i–iv–i6/4–V7–i` with `C#` leading tone; contrasting staccato bass & legato melody. |

### Tier 3 — Two-Voice Independence & Romantic Articulation (5 Lessons | Up to 2 Sharps/Flats)
*Goal: Read two genuinely independent melodic lines (Baroque) and shape expressive phrasing & rapid chord-tone figuration (Early Romantic).*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t3_01` | **Minuet in G Major (BWV Anh. 114 — Part A)** (*C. Petzold / Bach Anna Magdalena*) | `G Major` | `3/4` | `104` | 16 | True two-voice counterpoint; recognizing scale fragments vs. broken triad outlines (`I–IV–V–I`). |
| `t3_02` | **Musette in D Major (BWV Anh. 126 — Main Theme)** (*Bach Anna Magdalena*) | `D Major` | `2/4` | `96` | 16 | Two sharps (`F#`, `C#`); octave drone bass vs. crisp 8th-note RH jumps; `2/4` march pulse. |
| `t3_03` | **La Candeur (Op. 100, No. 1 — Theme)** (*F. Burgmüller*) | `C Major` | `4/4` | `100` | 16 | Classic Romantic phrasing, scalar runs, and tonic/dominant cadence clarity. |
| `t3_04` | **Arabesque (Op. 100, No. 2 — Main Section)** (*F. Burgmüller*) | `A Minor` | `2/4` | `112` | 16 | 16th-note 5-finger bursts over crisp LH chord pulses (`i–V7–i`, modulation to relative `III` C major!). |
| `t3_05` | **Minuet in G Minor (BWV Anh. 115 — Part A)** (*C. Petzold / Bach Anna Magdalena*) | `G Minor` | `3/4` | `96` | 16 | First deep dive into **G minor** (the home key of Ballade No. 1!): `Bb`, `Eb`, `F#`, melodic minor lifts. |

### Tier 4 — Harmonic Chunking & Classical Form (5 Lessons | 2–3 Sharps/Flats)
*Goal: Think in harmonic progressions, inversions, secondary dominants, and 16th-note Classical patterns to accelerate piece learning.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t4_01` | **Sonatina in C Major (Op. 36, No. 1 — Mvt. 1 Exposition)** (*M. Clementi*) | `C Major` | `4/4` | `108` | 16 | Tonic-to-Dominant modulation (`I → V/V → V`); broken-chord RH shapes + clean Alberti LH. |
| `t4_02` | **Two-Part Invention No. 1 (BWV 772 — Opening 8 Bars)** (*J.S. Bach*) | `C Major` | `4/4` | `76` | 8 | Imitative 16th-note motif passed between RH and LH; ultimate hand-independence builder. |
| `t4_03` | **B-Minor Expressive Study** (*L. Köhler / Heller Op. 47 style*) | `B Minor` | `6/8` | `66` (♩.) | 16 | First **compound `6/8` meter** (`1-2-3, 4-5-6` felt in 2); secondary dominants (`V7/iv`, `V7/V`). |
| `t4_04` | **Bb-Major Singing Arpeggios** (*Burgmüller Op. 100, No. 5 Innocence / Classical study*) | `Bb Major` | `4/4` | `92` | 16 | Relative major of G minor (`Bb`, `Eb`); rolling 16th-note accompaniment + balanced RH phrasing. |
| `t4_05` | **Ballade (Op. 100, No. 15 — Opening Dramatic Theme)** (*F. Burgmüller*) | `C Minor` | `3/8`→`6/8` | `72` (♩.) | 16 | Three flats (`Bb`, `Eb`, `Ab`); stormy LH octave/chord pulse + dramatic Romantic storytelling. |

### Tier 5 — Early Chopin & Romantic Voicing (4 Lessons | 3–4 Sharps/Flats)
*Goal: Enter Chopin's actual musical language—cantabile top-note voicing, wide LH stride leaps, chromatic inner voices, and rich flat/sharp keys.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t5_01` | **Prelude in A Major (Op. 28, No. 7 — Full Piece)** (*F. Chopin*) | `A Major` | `3/4` | `84` | 16 | Complete authentic Chopin work! Three sharps (`F#`, `C#`, `G#`); wide LH chord reaches + `V7/ii` (`F#7`). |
| `t5_02` | **Prelude in C Minor (Op. 28, No. 20 — Full Piece)** (*F. Chopin*) | `C Minor` | `4/4` | `56` | 13 | Complete authentic Chopin work! Thick 4-note chord voicing, diminished 7ths, Neapolitan/chromatic bass descent. |
| `t5_03` | **Prelude in E Minor (Op. 28, No. 4 — Opening 12 Bars)** (*F. Chopin*) | `E Minor` | `4/4` | `60` | 12 | Masterclass in chromatic voice-leading: slow-shifting LH triads where 1 voice drops by a half-step at a time. |
| `t5_04` | **Waltz in A Minor (B. 150 — Main Theme)** (*F. Chopin*) | `A Minor` | `3/4` | `116` | 16 | Classic Chopin LH bass-to-chord jump (`A2 → E3-A3-C4`) + singing legato RH melody (`i–iv–VII–III–VI–ii°–V7–i`). |

### Tier 6 — Bridge to Chopin Ballade No. 1 (4 Lessons | Advanced Keys & Op. 23 Idioms)
*Goal: Directly target the exact harmonic, rhythmic, and technical idioms required to begin studying Ballade No. 1 in G minor, Op. 23.*

| ID | Title & Book Anchor | Key | Meter | Target BPM | Bars | Theory & Technique Focus |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `t6_01` | **Eb-Major Cantabile & Wide LH Leaps** (*Chopin Nocturne Op. 9 No. 2 idiom study*) | `Eb Major` | `6/8` | `54` (♩.) | 12 | The secondary key area of Ballade No. 1! Singing RH melody over wide compound-meter LH jumps. |
| `t6_02` | **Sharp-Key Chromatic Voicing** (*Romantic Prelude study in F# Minor / E Major*) | `F# Minor` | `4/4` | `68` | 12 | 3–4 sharps reading fluency (`F#`, `C#`, `G#`, `E#`); diminished-7th pivot chords & inner-voice control. |
| `t6_03` | **Ballade No. 1 — Opening Largo & Neapolitan Study** (*F. Chopin, Op. 23 mm. 1–7*) | `G Minor` | `4/4` | `52` | 8 | The real opening of Ballade No. 1! Unison octave arpeggio on **Neapolitan 6th (`Ab/C = bII6`)** resolving into `V7 → i` in G minor. |
| `t6_04` | **Ballade No. 1 — Moderato Theme Study (6/4 Lilt)** (*F. Chopin, Op. 23 mm. 8–23*) | `G Minor` | `6/4` | `96` (♩) | 16 | The iconic `6/4` lilting waltz-barcarolle main theme: LH `bass–chord–rest` lilt + singing RH phrasing over `i–iv6–V7/V–V7`. |

*(Note: Every single built-in lesson JSON will be validated by an automated Catch2 test suite checking exact measure tick counts, key signature spelling consistency, playable piano range `[21, 108]`, valid fingerings `0..5`, and parseable chord symbols!)*

---

## 5. Grand Staff Engraving Upgrades (`src/ui/staff_renderer.*`, `src/ui/smufl_glyphs.hpp`)

To render Tiers 1–6 and imported `.musicxml` scores crisply at `≥60 FPS` on both Desktop and Emscripten:

1. **New SMuFL Glyphs (`src/ui/smufl_glyphs.hpp`, all verified in bundled `Bravura.otf` v1.482):**
   - Time signature digits `0–9` (`U+E080`..`U+E089`: supports `2/4`, `3/4`, `4/4`, `6/8`, `6/4`).
   - Flags: `flag8thUp` (`U+E240`), `flag8thDown` (`U+E241`), `flag16thUp` (`U+E242`), `flag16thDown` (`U+E243`).
   - Rests: `rest8th` (`U+E4E6`), `rest16th` (`U+E4E7`).
   - Augmentation dot: `augmentationDot` (`U+E1E7`) — placed in the space above the notehead if the note sits on a staff line.
2. **Procedural Engraving Additions (`src/ui/staff_renderer.cpp` + `src/core/staff_layout.*`):**
   - **Automatic Beaming:** Pure layout helper in `src/core/staff_layout.*` grouping contiguous 8th/16th notes within meter-appropriate beat groups (`4/4` beams in half-bar or quarter-beat groups; `3/4` per beat or 3-eighth pairs; `6/8` in 3-eighth dotted-quarter groups; `6/4` in 3-quarter / half-bar groups). Draws crisp sloped beam quads (`beamThickness = 0.50` staff spaces, `beamSpacing = 0.25` spaces for secondary 16th beams) and adjusts stem lengths cleanly. Unbeamed single 8th/16th notes draw SMuFL flags.
   - **Dotted Notes & Rests:** Draws augmentation dot `0.35` staff spaces right of the rightmost notehead (or rest).
   - **Tie Curves:** Quadratic Bezier arc (`ImDrawList::AddBezierQuadratic` or tapered double-arc for authentic engraved look) connecting `tie_start` to `tie_stop` of matching pitch (handling same-system ties and system-break half-ties).
   - **Fingering Numbers (`1–5`):** Small crisp Inter SemiBold numerals positioned cleanly above the treble stem/notehead (or below bass) without colliding with ledger lines or beams.
   - **Harmonic Annotations Above Staff:** Chord symbol (bold primary tint, e.g., `Gm`, `Ab/C`) + Roman numeral (muted secondary tint, e.g., `i`, `bII6`) aligned to the exact tick `x` position above the Treble staff.
   - **Multi-System Scrolling / Paging:** Render 2 visible systems (8 bars — active 4-bar system + dimmed 4-bar lookahead system, matching the familiar Stage 2B visual rhythm) with smooth auto-advancing page turns and a compact bar-scrubber / phrase bar below.

---

## 6. `.musicxml` & `.mxl` Importer (`src/core/musicxml_importer.*` + Desktop/Web File Picker)

### 6.1 Pure Core Parser (`src/core/musicxml_importer.hpp/.cpp`)
- **APIs:**
  - `std::expected<LessonScore, std::string> import_musicxml_string(std::string_view xml_text, std::string_view fallback_title = {})`
  - `std::expected<LessonScore, std::string> import_mxl_bytes(std::span<const std::uint8_t> zip_bytes, std::string_view fallback_title = {})`
  - `std::expected<LessonScore, std::string> import_musicxml_file(const std::filesystem::path& path)` (dispatches `.mxl` vs `.musicxml`/`.xml`).
- **Supported MusicXML Features:**
  - `<score-partwise>` (selects piano / first 2-staff part, or merges first 2 single-staff parts if split).
  - `<attributes>`: `<divisions>`, `<key><fifths>/<mode>`, `<time><beats>/<beat-type>`, `<clef>` (mapping `<staff>1` → Treble, `<staff>2` → Bass; fallback split at Middle C4 if single staff).
  - `<note>`: `<pitch>` (`<step>`, `<alter>`, `<octave>`), `<rest/>`, `<chord/>`, `<duration>`, `<type>`, `<dot/>`, `<tie type="start|stop"/>`, `<voice>`, `<staff>`, `<notations><technical><fingering>`.
  - `<backup>` and `<forward>` cursor movement within a measure (critical for multi-staff piano MusicXML!).
  - `<harmony>`: imports `<root>` + `<kind>` + `<bass>` directly into `HarmonicAnnotation` so imported scores with chord symbols get **Chord Skeleton** mode automatically!
  - `<direction><sound tempo="..."/>` or `<metronome>`: reads default `target_bpm` (fallback `84 BPM`).
  - **Graceful Quantization & Safety:** Converts arbitrary `<divisions>` to `kTicksPerQuarter = 480`; ignores grace notes (`<grace/>`) cleanly for v1 rather than crashing; clamps/warns on unplayable out-of-range pitches; never throws uncaught exceptions.
  - **Automatic Phrase Slicing:** Auto-partitions measures into balanced **4-to-8 bar `LessonPhrase`** chunks (preferring double barlines / rehearsal marks if present, otherwise 4-bar chunks for `<= 24` bars or 8-bar chunks for longer works).

### 6.2 Cross-Platform File Picker & Drag-and-Drop (`src/ui/` + `src/main.cpp`)
- **Native Desktop (Linux/macOS/Windows):**
  - **"Import .musicxml / .mxl"** button opens native OS dialog via **`SDL_ShowOpenFileDialog`** (SDL3 built-in async dialog with `{"MusicXML Scores", "musicxml;xml;mxl"}` filter!).
  - Drag-and-drop onto the Selfano window handled via **`SDL_EVENT_DROP_FILE`**.
  - CLI convenience flag: `--import-lesson <path>` for quick testing/automation.
- **Emscripten WebGUI:**
  - **"Import .musicxml / .mxl"** button triggers an offscreen HTML `<input type="file" accept=".musicxml,.xml,.mxl">` via a tiny `EM_JS` bridge that passes the `Uint8Array` bytes into a C++ `EMSCRIPTEN_KEEPALIVE` callback → `import_mxl_bytes` / `import_musicxml_string` → saves normalized JSON to `/persist/lessons/<id>.json` → `FS.syncfs(false)`.
  - Browser canvas drag-and-drop (`dragover` / `drop` on `#canvas`) routes into the exact same callback.
- **Post-Import Metadata Editor:** Compact sheet/modal after import allowing the user to tweak Title, Composer, Target BPM, and Phrase boundaries (or edit/add chord labels per bar).

---

## 7. Persistence & Autopilot Integration (`src/core/profile_store.*`, `src/core/autopilot.*`)

### 7.1 Backward-Compatible `profile.json` (`schema_version: 1`)
All new fields have safe defaults so existing `profile.json` files load seamlessly with zero data loss:
- `UserPreferences`:
  - `bool sync_weekly_key_to_lesson{true};`
  - `bool lesson_show_fingerings{true};`
  - `bool lesson_show_harmony{true};`
- `UserProfile`:
  - `std::string active_lesson_id{"t1_01"};`
  - `std::map<std::string, LessonProgress> lesson_progress{};`
  - `std::vector<std::string> imported_lesson_ids{};` (actual imported score payloads stored in `<pref_dir>/lessons/<id>.json` so `profile.json` stays compact and fast!)
- `PracticeHistoryEntry` & `SessionSummary`:
  - Add `std::uint32_t lesson_runs_completed{0};`, `std::uint32_t lesson_revisions_completed{0};`, and `std::string active_lesson_title{};` (keep `companion_reps` for backward compatibility).

### 7.2 Autopilot & Navigation Updates
- **`AutopilotStage` (`src/core/autopilot.hpp`):**
  - Stage 3 block in `kAutopilotBlocks` becomes **`AutopilotStage::Stage3Lessons`** (`"Stage 3: Guided Lessons"`, `"3/4"`, `600.0s`).
  - Keep `CompanionMode` as a standalone Free Practice / Tools view (not in the 6 Autopilot blocks).
- **Sidebar & Home Dashboard:**
  - Sidebar **Practice** group: `1. Warm-Up`, `2A. Flashcards`, `2B. Etudes`, `3. Guided Lessons`, `4A. Retention SRS`, `4B. Ear Trainer`.
  - Sidebar **Tools** group: `Companion Mode` (tucked cleanly right above `MIDI Monitor` & `Dev Tools`).
  - Home Dashboard: Stage 3 card displays the active lesson title, Tier badge, current Tempo Ladder progress (`60% / 80% / 100%`), and due revision badge (e.g., *"1 due revision + Active: Minuet in G"*).
- **Piano Remote Bindings (Stage 3 Guided Lessons):**
  - `E7` (`ReplayOrHear`): Play / Stop **Listen Demo** of active phrase.
  - `F7` (`PlayPauseTransport`): Start / Pause Steady-Pulse playback.
  - `G7` (`ResetOrRepeatPass`): Reset to start of current phrase.
  - `C#7 / D#7`: Step Tempo Ladder / Custom BPM down or up (`-5 / +5 BPM`).
  - `A7` (`NextPromptOrPassage`): Cycle active phrase (`Phrase A → B → Full Piece`).
  - `B7` (`ContextToggle`): Cycle practice mode (`Chord Skeleton → Wait-for-Note → Steady Pulse`).
  - `F#7`: Toggle Metronome.
  - `C8` (`ConfirmOrGood`): Confirm / Advance to next phrase or promote mastered lesson.
  - *Outer-key safety:* If the current lesson score actually uses any MIDI note `< 24` or `>= 96`, automatically enable the Soft Pedal (`CC 67`) guard just like Companion Mode!

---

## 8. Vertical Implementation Slices & TDD Plan

Execute sequentially via `cpp23-tdd` and `verification-and-review`:

### Slice 7.1 — Core `LessonScore` Model & Engraving Layout (`piano_core` + `piano_ui`)
- **Files:** `src/core/lesson_score.hpp/.cpp`, `src/core/staff_layout.hpp/.cpp` (beaming & tie layout), `src/ui/smufl_glyphs.hpp`, `src/ui/staff_renderer.hpp/.cpp`.
- **Tests:** `tests/lesson_score_tests.cpp` + expanded `tests/staff_layout_tests.cpp` (tick math for `2/4`, `3/4`, `4/4`, `6/8`, `6/4`; beaming groups; dotted durations; tie-stop exclusion; JSON serialize/deserialize round-trip).
- **Visual Check:** Add a classical engraving specimen to `Dev Tools → Staff Showcase` and verify crisp beams, flags, dots, ties, `3/4` & `6/8` meters, fingerings, and chord labels via headless `--screenshot`.

### Slice 7.2 — `LessonEngine`, Mastery, SM-2 Revision & Persistence (`piano_core`)
- **Files:** `src/core/lesson_engine.hpp/.cpp`, `src/core/profile_store.hpp/.cpp`, `src/core/autopilot.hpp/.cpp`, `src/core/piano_remote.hpp/.cpp`.
- **Tests:** `tests/lesson_engine_tests.cpp` + expanded `tests/profile_store_tests.cpp` & `tests/autopilot_tests.cpp`:
  - Target extraction (RH/LH/HT, Chord Skeleton, tie_stop skipped, phrase range slicing).
  - Demo MIDI event generation (tied note duration merging).
  - Wait-for-Note & Steady-Pulse grading (`±hit window`), phrase looping, Tempo Ladder (`60% → 80% → 100%`), `≥90%` auto-mastery graduation, manual override.
  - Stage 3 auto-split planner (`3:00` due revision + `7:00` active lesson vs. `10:00` active lesson).
  - `profile.json` backward compatibility (old v1 files load cleanly; new lesson fields round-trip).

### Slice 7.3 — Curated 6-Tier Classical Curriculum (`assets/lessons/`)
- **Files:** `assets/lessons/curriculum.json` + 28 verified lesson score files (`t1_01`..`t6_04`) + `src/core/lesson_library.hpp/.cpp` (pure loader from JSON strings / filesystem paths).
- **Tests:** `tests/lesson_library_tests.cpp`:
  - Loads all 28 built-in lessons from `assets/lessons/`.
  - Verifies **every single measure** sums cleanly to its `TimeSignature::measure_ticks()`.
  - Verifies every chord symbol parses cleanly via `parse_chord_symbol`.
  - Verifies all MIDI pitches match `spelled.to_midi()`, fall within `[21, 108]`, and all fingerings are `0..5`.
  - Verifies key progression covers the planned keys cleanly.

### Slice 7.4 — `.musicxml` & `.mxl` Importer (`piano_core` + `CMakeLists.txt`)
- **Files:** `CMakeLists.txt` (add pinned `FetchContent` for `pugixml` & `miniz`), `src/core/musicxml_importer.hpp/.cpp`.
- **Tests:** `tests/musicxml_importer_tests.cpp`:
  - Uncompressed `.musicxml` fixtures: 2-staff Grand Staff with `<backup>`, key/time signatures, chords (`<chord/>`), ties (`start`/`stop`), dots, 8th/16th notes, fingerings, `<harmony>` chord symbols, multi-measure auto phrase slicing.
  - Compressed `.mxl` archive fixture (`META-INF/container.xml` + embedded XML).
  - Malformed XML / corrupt ZIP returns clean `std::unexpected` error string (no crashes).

### Slice 7.5 — Guided Lessons UI, File Import & App Shell Integration (`piano_ui` + `src/main.cpp`)
- **Files:** `src/ui/lesson_view.hpp/.cpp` (Apple HIG Curriculum Drawer/Browser + Lesson Player + Theory Lens Card + Tempo Ladder + Import/Edit modal), `src/ui/home_dashboard.cpp`, `src/ui/autopilot_chrome.cpp`, `src/ui/piano_remote_overlay.cpp`, `src/main.cpp`.
- **Capabilities:**
  - Clean split or toggleable **Curriculum Browser** (Tiers 1–6 + My Books with mastery rings, key pills, due-review badges) and distraction-free **Lesson Player** (Grand Staff 2-system view, Theory Lens toggle, Listen Demo, Chord Skeleton / Wait-for-Note / Steady-Pulse segmented control, RH/LH/HT selector, Phrase pills, `60% / 80% / 100%` Tempo Ladder pills, and one-click "Add to Memory SRS" on mastery).
  - Sync Weekly Key to Active Lesson wired into `AppContext` (when switching active lesson, updates Weekly Key if enabled).
  - Companion Mode relocated to Tools navigation group (still accessible via `--tab companion`).
  - Desktop `SDL_ShowOpenFileDialog` + `SDL_EVENT_DROP_FILE` and Emscripten `<input type="file">` + canvas drop + `/persist/lessons/` IDBFS sync.

### Slice 7.6 — Cross-Platform Verification & Polish
- Native Debug build warning-free (`-Wall -Wextra -Wpedantic`), 100% passing `ctest`.
- Emscripten `web-release` build warning-free (`selfano.html`, `selfano.js`, `selfano.wasm`, `selfano.data`).
- `src/core/` purity check (`0` matches for UI/SDL/MIDI/Emscripten/clock/random).
- `clang-format --dry-run --Werror` clean on all modified/created files.
- Headless Light & Dark visual screenshot verification across `--tab lessons` (Curriculum Browser, Theory Lens, 8th/16th beaming, `6/8` & `3/4` meters, Chord Skeleton, Tempo Ladder), `--tab home`, `--tab autopilot`, and `--tab companion` (under Tools).
- Browser verification (Emscripten) testing lesson loading from preloaded `/assets/lessons/`, `.musicxml`/`.mxl` upload, and `IDBFS` persistence across reload.

---

## 9. Exit Gate (Verification Criteria)

1. Zero compiler warnings on both `native-debug` and `web-release` (`emcmake cmake --preset web-release`).
2. 100% passing `ctest --preset native-debug --output-on-failure` including full validation of all 28 built-in lessons, engraving layout, lesson engine mastery/revision math, and `.musicxml`/`.mxl` importer tests.
3. `src/core/` purity check returns 0 matches.
4. Autopilot Stage 3 seamlessly runs Guided Lessons (with 3 min revision + 7 min active auto-split when due) and updates Weekly Key automatically when `sync_weekly_key_to_lesson` is enabled.
5. Companion Mode remains fully functional under Tools without regressions.
6. Importing an external `.musicxml` or `.mxl` file works cleanly and persists across restarts on both Desktop and Web.

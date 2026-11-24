# Phase 8: Guided Lessons Engine, Autopilot Stage 3 Integration & Apple HIG UI (6 Starter Lessons)

**Status:** `[ ] Planned` (Depends on Phase 7)  
**Goal:** Build the interactive `LessonEngine`, replace Companion Mode in Autopilot Stage 3 with **Guided Lessons & Curriculum** (auto-splitting `~3:00` guided revision + `~7:00` active lesson when due, syncing Weekly Key by default, and promoting mastered lessons to Stage 4A SRS), deliver the Apple HIG Lesson Browser & Player UI, tuck Companion Mode safely under **Tools**, and ship **6 musically verified starter lessons (1 per Tier 1–6)** exercising every meter and feature end-to-end.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `music-notation-imgui`, `verification-and-review`

---

## 1. Design Defaults & Open Confirmations

- **Ornaments / Grace Notes (v1):** Ignored for v1 MIDI attack grading so decorative notes never stall playback.
- **Default Starting Lesson:** Fresh profiles default `active_lesson_id` to `t1_01`, with **Guided Mastery + Free Override** allowing immediate manual jumping/unlocking of any tier at any time.

---

## 2. Deliverables & Vertical Slices

### Slice 8.1 — `LessonEngine`, Mastery & SM-2 Guided Revision (`src/core/lesson_engine.*` via `cpp23-tdd`)
- **Pure C++23 (`piano_core`):** All timestamps (`now_ms`, `dt_ms`, `today_epoch_day`) injected as parameters.
- **Practice Modes & State:**
  ```cpp
  enum class LessonPracticeMode : std::uint8_t {
    ChordSkeleton = 0, // 1 chord per bar from HarmonicAnnotation (octave-flexible pitch-class match!)
    WaitForNote = 1,   // Untimed note-by-note / chord-by-chord learning
    SteadyPulse = 2,   // 1-bar count-in + metronome + beat-window grading (60% / 80% / 100% Tempo Ladder)
  };

  enum class LessonHandMode : std::uint8_t {
    RightHand = 0,
    LeftHand = 1,
    HandsTogether = 2,
  };

  enum class TempoLadderStep : std::uint8_t {
    Slow60 = 0,    // max(40.0, round(0.60 * target_bpm))
    Medium80 = 1,  // max(44.0, round(0.80 * target_bpm))
    Target100 = 2, // target_bpm
    Custom = 3,
  };

  enum class LessonMasteryStatus : std::uint8_t {
    New = 0,
    Learning = 1,
    Mastered = 2,
    NeedsReview = 3, // derived when Mastered && today_epoch_day >= review_due_epoch_day
  };
  ```
- **Target Extraction (`build_targets`):**
  - Slices active `LessonPhrase` (e.g., Bars 1–4) or Full Piece.
  - Filters by `LessonHandMode` (`RightHand`, `LeftHand`, `HandsTogether`).
  - **Tie Handling:** Excludes `tie_stop == true` notes from fresh `NoteOn` targets.
  - **Chord Skeleton Mode:** Converts each `HarmonicAnnotation` via `parse_chord_symbol` into a clean playable LH bass + RH close-position triad/7th display voicing on the Grand Staff, and grades flexibly by pitch-class coverage (so learners internalize the harmonic shape without fighting a specific octave).
- **Listen Demo Generator (`build_demo_playback`):**
  - Produces `std::vector<MidiPlaybackNote>` for the active phrase/piece and selected hand(s) at the currently selected BPM, merging contiguous tied notes (`tie_start` → `tie_stop`) into single sustained MIDI durations.
- **Grading, Looping & Mastery Graduation:**
  - `WaitForNote`: advances on exact chord match; highlights wrong notes Red with target vs. played badge.
  - `SteadyPulse`: 1-bar count-in (meter-aware: 4 clicks in `4/4`, 3 in `3/4`, 2 or 6 in `6/8`, 6 in `6/4`) → smooth tick playhead → hit window `±max(140 ms, 0.38 * beat_ms)` → tracks `hits`, `misses`, `accuracy`.
  - Phrase Loop mode: subtle chime on clean phrase completion + optional seamless repeat.
  - **Mastery Graduation (Full Piece + Hands Together + Steady Pulse + `accuracy >= 0.90`):**
    - Updates `best_accuracy`, `best_clean_bpm`, and checks off `ladder_60_clean` / `ladder_80_clean`.
    - When `bpm >= target_bpm - 0.5`: graduates `status = Mastered`, schedules first SM-2 guided revision (reusing `review_card` math from `src/core/srs_deck.hpp`), and raises `just_mastered` event (UI offers one-click **"Add to Stage 4A Memory SRS"**).
  - **Guided Revision Rating:** Completing a due revision run computes suggested SM-2 rating (`>= 90%` at `>= 90%` target BPM → `Good`; `75–89%` → `Hard`; `< 75%` → `Again`) and reschedules `review_due_epoch_day`.
- **Autopilot Stage 3 Auto-Split Planner (`plan_stage3_session`):**
  - Checks due mastered lessons (`review_due_epoch_day <= today_epoch_day`, picking most overdue first).
  - If due: schedules **`Phase A: Guided Revision (180s)` → `Phase B: Active Lesson (420s)`** (transitioning cleanly at a bar/run boundary via `is_mid_bar()`).
  - If none due: schedules full **`600s` Active Lesson**.

### Slice 8.2 — `profile.json`, `Autopilot` & `PianoRemote` Updates (`src/core/` via `cpp23-tdd`)
- **Backward-Compatible `profile_store.hpp/.cpp` (`schema_version: 1`):**
  - `UserPreferences`: add `sync_weekly_key_to_lesson{true}`, `lesson_show_fingerings{true}`, `lesson_show_harmony{true}`.
  - `UserProfile`: add `active_lesson_id{"t1_01"}`, `std::map<std::string, LessonProgress> lesson_progress{}`, `std::vector<std::string> imported_lesson_ids{}`.
  - `PracticeHistoryEntry` & `SessionSummary`: add `lesson_runs_completed`, `lesson_revisions_completed`, `active_lesson_title` (retain `companion_reps` for backward compatibility).
  - Verify old Phase 6 `profile.json` files deserialize cleanly with sensible defaults.
- **`autopilot.hpp/.cpp`:**
  - Replace Stage 3 block in `kAutopilotBlocks` with `AutopilotStage::Stage3Lessons` (`"Stage 3: Guided Lessons"`, `"3/4"`, `600.0`).
  - Retain Companion Mode as a standalone tool view (separate from the 6 Autopilot stages, zero deleted tests or logic).
- **`piano_remote.hpp/.cpp`:**
  - Bind outer keys for Stage 3 Guided Lessons:
    - `E7`: Play/Stop Listen Demo
    - `F7`: Start/Pause Steady Pulse
    - `G7`: Reset current phrase
    - `C#7 / D#7`: Decrease/Increase BPM (`-5 / +5`)
    - `A7`: Cycle phrase (`Phrase A → B → Full Piece`)
    - `B7`: Cycle mode (`Chord Skeleton → Wait-for-Note → Steady Pulse`)
    - `F#7`: Toggle Metronome
    - `C8`: Confirm / Advance / Promote
  - Automatically enable Soft Pedal (`CC 67`) guard whenever the active lesson score uses any note `< 24` or `>= 96`.

### Slice 8.3 — 6 Representative Starter Lessons & `LessonLibrary` Loader (`assets/lessons/`, `src/core/lesson_library.*`)
- Create `src/core/lesson_library.hpp/.cpp` loading `assets/lessons/curriculum.json` + individual lesson JSON files (pure C++23 loader taking directory path or JSON strings).
- Ship **6 hand-verified starter lessons (1 per Tier 1–6)** covering all 5 engraving meters and core features:
  1. `t1_01` — *Five-Finger Steps & Tonic Drone* (`C Major`, `4/4`, 8 bars — landmarks, quarter/whole notes)
  2. `t2_04` — *Dotted Rhythms & Ties* (`F Major`, `4/4`, 12 bars — 1 flat, dotted quarters, barline ties, 8th beams)
  3. `t3_01` — *Minuet in G Major — Part A* (`G Major`, `3/4`, 16 bars — 1 sharp, `3/4` counterpoint, fingerings)
  4. `t4_03` — *B-Minor Expressive Study* (`B Minor`, `6/8`, 16 bars — 2 sharps, compound `6/8` beaming, secondary dominants)
  5. `t5_02` — *Chopin Prelude in C Minor, Op. 28 No. 20* (`C Minor`, `4/4`, 13 bars — 3 flats, thick 4-note Romantic chords, chromatic bass)
  6. `t6_04` — *Ballade No. 1 — Moderato Theme Study* (`G Minor`, `6/4`, 16 bars — 2 flats, `6/4` lilt, Chopin voicing)
- Full Catch2 integrity test validating all loaded starter lessons via `validate_lesson_score`.

### Slice 8.4 — Apple HIG Guided Lessons UI & App Shell Wiring (`src/ui/lesson_view.*`, `src/ui/home_dashboard.cpp`, `src/main.cpp`)
- **`LessonView` (`src/ui/lesson_view.hpp/.cpp`):**
  - Follows `.agents/skills/apple-hig-imgui/SKILL.md` (Light Paper & Dark Studio tokens, progressive disclosure, zero clutter):
    - **Top Control Strip:** Active lesson title + Tier/Key/Meter pills + **"Curriculum"** drawer toggle + **Listen Demo** button + **Options** popover (`Show Fingerings`, `Show Harmony`, `Sync Weekly Key`, `Manual Mark Mastered`).
    - **Collapsible "Theory & Pattern Lens" Card:** Concise summary, pattern chunking tip, technique tip, and Ballade No. 1 link (collapsed automatically during active Steady-Pulse playback for focus).
    - **Practice Scaffold Bar:** Segmented mode control (`Chord Skeleton` | `Wait` | `Pulse`), Hand filter (`RH` | `LH` | `Both`), Phrase chips (`Phrase A`, `Phrase B`, ..., `Full`), and **Tempo Ladder pills** (`60%` | `80%` | `100%` with green checkmarks on cleared steps).
    - **2-System Elevated Grand Staff Canvas:** Active 4-bar system + dimmed 4-bar lookahead system with moving playhead, Neutral/Green/Red note feedback, chord/Roman-numeral headers, and fingerings.
    - **Mastery Celebration Banner:** Appears on graduation with one-click **"Add to Memory SRS (Stage 4A)"** and **"Next Lesson →"** buttons.
    - **Slide-Over / Modal Curriculum Browser:** Grouped by Tiers 1–6 (plus placeholder `My Books` section ready for Phase 10), showing mastery rings, due-revision badges, key pills, and best clean BPM.
- **Navigation, Home Dashboard & Sync Weekly Key (`src/main.cpp`, `src/ui/home_dashboard.cpp`):**
  - Sidebar **Practice**: `1. Warm-Up`, `2A. Flashcards`, `2B. Etudes`, `3. Guided Lessons`, `4A. Retention SRS`, `4B. Ear Trainer`.
  - Sidebar **Tools**: `Companion Mode`, `MIDI Monitor`, `Dev Tools`.
  - CLI smoke routing: `--tab lessons` (and `--tab companion` continues working).
  - **Sync Weekly Key:** Whenever `sync_weekly_key_to_lesson` is ON and active lesson changes (or on startup), synchronizes `weekly_key` to the active lesson's authentic key so Stages 1, 2B, and 4B reinforce the same key immediately.
  - Home Dashboard Stage 3 card previews the active lesson, Tier/Key badge, Tempo Ladder progress, and any due revision count.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug` compiles cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
2. `ctest --preset native-debug --output-on-failure` passes 100%, covering `lesson_engine_tests`, `lesson_library_tests`, backward-compatible `profile_store_tests`, updated `autopilot_tests`, and existing `companion_tests`.
3. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with zero warnings (`assets/lessons/` preloaded into `selfano.data`).
4. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
5. Headless Light & Dark visual verification (`--tab lessons`, `--tab home`, `--tab autopilot`, `--tab companion`) confirms crisp Apple HIG layout, zero clipping, active Sync Weekly Key, and working Chord Skeleton / Wait-for-Note / Tempo Ladder controls.
6. CLI `--profile /tmp/selfano_phase8.json` round-trip verifies lesson progress, mastery, SM-2 revision scheduling, and legacy Companion phrases survive restart.

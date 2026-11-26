# Phase 30: Tiered Multi-Meter Sight-Reading & Hybrid Micro-Corpus (Stage 2A/2B)

**Status:** `[ ] Planned` (Depends on Phase 20 — Track C: Daily Drills & MIDI Technique Diagnostics)  
**Goal:** Break the Grade 1–2 sight-reading ceiling in Stage 2 by upgrading **Flashcards (`Stage 2A`)** with 7th chords, accidentals, and ledger-line extremes, and upgrading **Continuous Etudes (`Stage 2B`)** to **5 progressive difficulty tiers** (`3/4`, `6/8`, 8th notes, dotted rhythms, Alberti/waltz bass, accidentals) plus the planned **Hybrid Micro-Corpus** of unseen 4-bar classical excerpts.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Right now Autopilot spends **10 minutes/day (33% of practice)** on Stage 2A + 2B, yet `EtudeEngine` is hardcoded to `4/4` quarter/half/whole notes with static bass blocks and zero accidentals. Past Tier 2, learners stop growing during Stage 2B. Upgrading Stage 2 to scale seamlessly from 5-finger reading up to compound `6/8`, 16th-note subdivisions, Alberti/waltz accompaniments, 7th-chord recognition, and real classical sight-reading excerpts keeps sight-reading velocity climbing all the way toward Chopin.

---

## 2. Deliverables & Vertical Slices

### Slice 30.1 — Advanced Flashcard Shapes (`src/core/flashcard_engine.hpp/.cpp`, `tests/flashcard_tests.cpp`)
- Add an optional `FlashcardLevel` (`Foundation` = existing 40 shapes; `Romantic` = adds 24+ new shapes):
  - **7th Chords (Root & Inversions):** `V7`, `vii°7`, `iiø7` in Treble and Bass.
  - **High/Low Ledger-Line Landmarks:** `A5–E6` (Treble) and `E2–A1` (Bass).
  - **Chromatic Alterations:** Reading raised leading tones, `bII` Neapolitan notes, and double-sharps/naturals inside active key signatures.
  - **Two-Hand Vertical Grabs:** Simultaneous bass root + treble chord shape across the Grand Staff.
- Maintain full backward compatibility with existing `profile.json` `shape_telemetry` keys.

### Slice 30.2 — 5-Tier Multi-Meter `EtudeEngine` + Hybrid Excerpt Pool (`src/core/etude_engine.hpp/.cpp`, `assets/sight_reading/`, `tests/etude_tests.cpp`)
- Add `enum class EtudeDifficultyLevel : std::uint8_t { Level1Foundations = 1, Level2EighthsAndWaltz = 2, Level3CompoundAndDotted = 3, Level4AlbertiAndAccidentals = 4, Level5RomanticTextures = 5 };` to `EtudeConfig` (persisted in `profile.json` and auto-suggested from the learner's current curriculum tier):
  - **Level 1:** Current `4/4` quarters/halves/wholes + whole/half bass.
  - **Level 2:** Adds `3/4`, 8th-note pairs, and waltz bass (`bass–chord–chord`).
  - **Level 3:** Adds `6/8` compound meter, dotted rhythms, and barline ties.
  - **Level 4:** Adds Alberti bass, leading-tone & secondary-dominant accidentals (`V7/V`), and `16th` bursts.
  - **Level 5:** Adds `6/4` barcarolle lilt, 2-hand chord voicings, wider LH leaps, and chromatic passing tones.
- Implement the **Hybrid Micro-Corpus** (`assets/sight_reading/corpus.json`): a curated bank of 30+ authentic 4-bar public-domain micro-excerpts (Czerny Op. 599/299, Bach Chorales/Notebook, Gurlitt, Burgmüller, Schumann *Album for the Young*) tagged by level and key, which `EtudeEngine` can interleave cleanly alongside procedural systems (transposed or filtered to the weekly key) so the learner reads real musical phrasing at sight.

### Slice 30.3 — Stage 2 UI Level Picker & Staff Rendering (`src/ui/flashcard_view.cpp`, `src/ui/etude_view.cpp`, `CMakeLists.txt`)
- Surface clean Level/Mode controls in `FlashcardView` and `EtudeView` (including a `Procedural | Hybrid Classical` toggle in Etudes) and update `EtudeView` to render beaming, `3/4`/`6/8`/`6/4` meters, dots, and ties using `StaffRenderer`'s Phase 7 primitives.
- Bundle `assets/sight_reading/` in both native `selfano_sync_assets` and Emscripten `--preload-file`.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests (including all 5 Etude tiers, Hybrid corpus integrity, and new Flashcard shapes).
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings** and bundles `assets/sight_reading/` into `selfano.data`.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots of `--tab flashcards` (7th chord / ledger line) and `--tab etude` (Level 3 `6/8` and Level 4 Alberti) confirm crisp notation and zero clipping.

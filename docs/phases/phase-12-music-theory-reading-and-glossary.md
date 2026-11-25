# Phase 12: Music Theory Reading Layer, Tier Guides & Clickable Glossary

**Status:** `[ ] Planned` (Depends on Phase 11)  
**Goal:** Give self-taught learners genuine, plain-English music theory text *before* they touch the keys—structured Lesson Briefs with playable "Hear & Compare" snippets, 6 Tier Chapter Guides, and clickable Roman-numeral Theory Popovers directly on the Grand Staff.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Is Needed

Currently, instructional text is limited to 4 one-line strings inside `TheoryLens`, hidden by default (`theory_expanded_ = false`), auto-hidden during playback, and using conservatory jargon (`V6/5`, `I6/4`, `V7/V`, `ii6/5`, `bII6`) without explaining any of it. Self-taught pianists learn exponentially faster when they understand the harmonic and formal *why* behind a score before drilling the notes.

---

## 2. Deliverables & Vertical Slices

### Slice 12.1 — Extended `TheoryLens` Schema & Pure Harmonic Glossary (`src/core/lesson_score.*`, `src/core/theory_glossary.*`)
- Extend `TheoryLens` (100% optional, backward-compatible JSON):
  - `std::string concept_title` (the single core concept taught by this lesson)
  - `std::vector<std::string> concept_paragraphs` (2–3 crisp, jargon-free paragraphs)
  - `std::vector<PhraseFormNote> form_map` (`phrase_label` + structural `role`, e.g., *"Question phrase ending on Half Cadence (V)"*)
  - `std::vector<std::string> practice_steps` (piece-specific 3–4 step recipe)
  - `std::vector<TheoryAudioExample> audio_examples` (`label`, `caption`, `chords[]` playable via voice-led synthesis)
- Create pure C++23 `src/core/theory_glossary.hpp/.cpp`:
  - `explain_harmony(std::string_view roman, std::string_view chord_symbol, KeySignature key)`
  - Explains diatonic triads/7ths, figured bass inversions (`6`, `6/4`, `6/5`, `4/3`, `4/2`), cadential `I6/4`, secondary dominants (`V7/V`, `V7/iv`, `V7/ii`, `V7/vi`, `V7/III`), diminished pivots (`viio7/V`), and the Neapolitan 6th (`bII6`).
  - Returns plain-English title, harmonic function badge (*Tonic / Predominant / Dominant / Chromatic*), 2-sentence explanation, spelled note names, and a voice-led playable preview (chord → natural resolution).

### Slice 12.2 — Tier Chapter Guides & 28 Rich Lesson Briefs (`assets/lessons/tier_guides.json`, `assets/lessons/t*.json`, `src/core/lesson_library.*`)
- Author `assets/lessons/tier_guides.json` with 6 concise 2-minute Chapter Guides:
  - **Tier 1:** Grand Staff Landmarks (`G4`, `C4`, `F3`), Line-vs-Space Intervals, Rhythm Values, Hand Frame, `I–IV–V–I`.
  - **Tier 2:** Chord Inversions & Figured Bass (`6`, `6/4`), Chunking Alberti & Waltz Accompaniments, Dotted Rhythms & Ties.
  - **Tier 3:** Half vs. Authentic Cadences, Two-Voice Counterpoint, Articulation, Syncopated Pedaling.
  - **Tier 4:** Secondary Dominants (`V7/V`), Classical Modulation & Sonata Exposition, Compound `6/8` Meter.
  - **Tier 5:** Cantabile Top-Note Voicing, Chromatic Voice-Leading, Wide LH Stride & Wrist Arcs.
  - **Tier 6:** Mode Mixture, The Neapolitan 6th (`bII6`), `6/4` Barcarolle Lilt, and Form Map of Ballade No. 1.
- Populate rich `TheoryLens` Lesson Brief fields across all 28 built-in lessons (`assets/lessons/t*.json`), each teaching 1 focused theory concept plus 1–2 interactive *"Hear & Compare"* chord snippets.
- Add `brief_read` (`bool`, default `false`) to `LessonProgress` in `src/core/lesson_engine.hpp` and backward-compatible `profile_store.cpp`.

### Slice 12.3 — `Learn | Practice` View, Chapter Guide Sheet & Clickable Staff Popovers (`src/ui/lesson_brief_view.*`, `lesson_view.*`, `curriculum_modal.*`, `staff_renderer.*`)
- Create `src/ui/lesson_brief_view.hpp/.cpp` following Apple HIG typography and card hierarchy:
  - Concept card + inline **"Hear & Compare"** playback buttons routed through `AppController`'s `MidiPlaybackScheduler`.
  - Visual Form & Phrase Map list, Technique Cue card, Practice Recipe checklist, and primary **"Start Practicing →"** button.
- Add a clean **`Learn | Practice`** segmented toggle in `LessonView`:
  - Auto-opens `Learn` once on a `New` lesson (`!prog.brief_read`), marking `brief_read = true` when switching to `Practice`.
- Add a **"Chapter Guide"** button on each Tier header in `CurriculumModal` opening a readable sheet for that tier.
- Return hit-test rectangles for rendered `StaffHarmonicLabel` badges from `render_staff(...)` so clicking any Roman numeral (`V6/5`, `V7/V`, `bII6`) above the staff opens a compact **Theory Popover** powered by `explain_harmony(...)` with a **"Play Chord & Resolution"** button.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` passes 100% with 0 warnings, including `theory_glossary_tests` and integrity assertions that all 28 lessons and 6 tier guides have non-empty `concept_title`, `concept_paragraphs`, `form_map`, `practice_steps`, and parseable `audio_examples`.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly and bundles `tier_guides.json` into `selfano.data`.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots verify crisp `Learn` Lesson Brief layout, Tier Chapter Guide modal, and clickable Roman-numeral Theory Popover (`t4_01` `V7/V` and `t6_03` `bII6`) with zero text clipping or overlap.

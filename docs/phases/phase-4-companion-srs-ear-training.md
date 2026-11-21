# Phase 4: Stage 3 (Companion Mode) & Stage 4 (SRS Retention Deck + `MIDI OUT` Ear Trainer)

**Goal:** Implement the second 15 minutes of the daily routine—distraction-free repertoire harmonic mapping (Companion Mode), SM-2 spaced-repetition retention deck, and procedural `MIDI OUT` (or SDL synth) ear training.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `midi-emscripten-interop`, `music-notation-imgui`, `verification-and-review`

---

## Deliverables & Vertical Slices

### Slice 4.1 — Chord Symbol Parser & Companion Mode Matcher (`src/core/` via `cpp23-tdd`)
- `chord_symbol.hpp/.cpp`: Parse practical classical/anime chord symbols (`C`, `Dm`, `Bb`, `F#m`, `G7`, `Cmaj7`, `Bdim`, `Asus4`, slash chords like `C/E`, `Dm/F`) via `std::expected<ChordSymbol, ParseError>` into root pitch class + chord tone pitch-class bitmask (`uint16_t`).
- `companion_matcher.hpp/.cpp`: Flexible rolling-window harmonic verifier for 4-bar (or N-bar) phrases (e.g., *Gate of Steiner — Phrase A*: `Dm -> Bb -> F -> C`):
  - Ignores octaves and chord inversions.
  - Tolerates melodic passing tones outside the chord.
  - Advances to the next target bar once core chord coverage is detected (e.g., bass/root + >= 1 other chord tone, or >= 2 distinct chord pitch classes within the rolling window).
  - Emits a `phrase_completed` event (triggering the subtle confirmation chime) and increments clean rep counter.

### Slice 4.2 — Stage 4A 3-Button SM-2 SRS Engine (`src/core/` via `cpp23-tdd`)
- `srs_deck.hpp/.cpp`:
  - `SrsCard` storing `id`, `piece_name`, `section_name` (e.g., *Gate of Steiner — Intro*, *Jane Doe — Chorus*), optional chord roadmap hint, `interval_days`, `ease_factor` (default `2.5`, min `1.3`), `repetitions`, and `due_epoch_day`.
  - Pure `review_card(card, SrsRating::Again | Hard | Good, today_epoch_day)` implementing 3-button SM-2 transitions deterministically.
  - Due queue query (`due_cards(today_epoch_day)`).

### Slice 4.3 — Stage 4B Procedural Ear Trainer (`src/core/` via `cpp23-tdd`)
- `ear_trainer.hpp/.cpp`:
  - Generates tonic anchor chord (e.g., `D–F–A` in D minor) + 3-to-5 note procedural melody in the weekly key (scaling difficulty from stepwise motion -> 3rds -> 5ths).
  - Emits a scheduled playback list (`MidiPlaybackNote { note, velocity, start_offset_ms, duration_ms }`) consumed by `MidiEngine` (`MIDI OUT`) or `AudioSynthFallback`.
  - Evaluates user playback note-by-note:
    - On wrong note: returns directional hint string via `std::format` (e.g., `"Target is 2 half-steps higher"` or `"Target is 1 half-step lower"`).
    - On `Spacebar` or Soft Pedal (`CC 67`): triggers instant melody replay.
    - On full match: transitions state to `Revealed` so the staff displays the exact melody the user just played by ear.

### Slice 4.4 — Stage 3 & Stage 4 UI Views (`src/ui/`)
- **Stage 3 Companion Mode UI:** Active piece/phrase picker, inline chord-roadmap editor (`Dm -> Bb -> F -> C`), large low-glare bar indicator + 10-minute chunk timer + rep counter + subtle chime on clean rep (designed for zero screen-switching while eyes stay on printed/PDF sheet music).
- **Stage 4A SRS Deck UI:** Add/edit repertoire sections, show due card prompt ("Play from memory without sheet music"), optional toggle to peek at chord map, and `Again` / `Hard` / `Good` buttons showing next interval preview.
- **Stage 4B Ear Trainer UI:** Hidden staff during listening/guessing -> directional hint feedback -> replay button (`Space` / Soft Pedal `CC 67`) -> revealed Grand Staff notation on match.

---

## Exit Gate (Verification Criteria)

1. Warning-free build and 100% passing `ctest` covering:
   - Chord symbol parser (valid triads, 7ths, flats/sharps, slash chords, invalid input `std::expected` errors).
   - Companion Mode matcher handling broken arpeggios, inversions, and passing melody tones without false stalls.
   - SM-2 `Again` / `Hard` / `Good` scheduling math and ease-factor floor.
   - Ear trainer note-by-note evaluation, directional half-step hints, replay, and reveal state transitions.
2. `src/core/` purity check passes.
3. Manual verification confirms Companion Mode chime triggers on clean 4-bar chord reps, SRS cards reschedule cleanly, and Ear Trainer plays tonic + melody via `MIDI OUT` or SDL3 synth and reveals the staff on match.

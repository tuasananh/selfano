# Phase 7: Core `LessonScore` Model & Classical Grand Staff Engraving Upgrade

**Status:** `[ ] Planned` (Design Locked — Ready for Implementation)  
**Goal:** Build the pure C++23 rational-tick `LessonScore` data model and upgrade Selfano's hybrid `ImDrawList` + SMuFL Grand Staff renderer to handle authentic classical notation (**8th/16th notes & rests, automatic beaming, SMuFL flags, dotted durations, Bezier ties, `2/4 · 3/4 · 4/4 · 6/8 · 6/4` time signatures, fingerings `1–5`, and chord/Roman-numeral harmonic labels**) with **zero visual or behavioral regressions** across existing Stages 1, 2A, 2B, and 4B.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`

---

## 1. Why This Phase Comes First

Upgrading `staff_renderer` and introducing a rich multi-meter score model touches Selfano's most visually intricate layer (`src/core/staff_layout.*`, `src/ui/staff_renderer.*`, `src/ui/smufl_glyphs.hpp`). Isolating this into Phase 7 guarantees:
- All existing screens (`Warm-Up`, `Flashcards`, `Etudes`, `Ear Trainer`, `Sandbox`, `Staff Showcase`) continue compiling and rendering identically.
- Every engraving feature needed by classical books and `.musicxml` scores is proven via unit tests and headless Light/Dark screenshot verification before Phase 8 wires up the Lesson Engine and Autopilot.

---

## 2. Deliverables & Vertical Slices

### Slice 7.1 — Rational-Tick `LessonScore` Data Model & JSON Codec (`src/core/lesson_score.*` via `cpp23-tdd`)
- **Zero hardware/UI/clock/random dependencies** (`piano_core` purity preserved).
- **Exact Rational Tick Grid:** `inline constexpr int kTicksPerQuarter = 480;` (cleanly divisible by 2, 3, 4, 6, 8, 12, 16, 24, 32 — prevents floating-point drift across `4/4`, `3/4`, `2/4`, `6/8`, `6/4`, dotted values, 16th notes, and future triplets).
- **Core Structs & Enums:**
  ```cpp
  struct TimeSignature {
    std::uint8_t numerator{4};    // 2, 3, 4, 6
    std::uint8_t denominator{4};  // 4 or 8
    [[nodiscard]] constexpr int measure_ticks() const noexcept;
    [[nodiscard]] constexpr int beat_ticks() const noexcept;       // 6/8 -> 720 (dotted quarter); simple -> 480 * 4 / denominator
    [[nodiscard]] constexpr int pulse_count() const noexcept;      // 6/8 -> 2 main beats (or 6 eighth subdivisions)
    [[nodiscard]] constexpr bool is_compound() const noexcept;     // numerator == 6 && denominator == 8
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
    bool dotted{false}; // adds 50% (e.g. dotted quarter = 720, dotted half = 1440, dotted 8th = 360)
    [[nodiscard]] constexpr int ticks() const noexcept;
  };

  struct LessonNote {
    SpelledNote spelled{};
    MidiPitch midi{60};
    std::uint8_t finger{0};   // 0 = none, 1..5 = printed fingering
    bool tie_start{false};    // tied forward to matching pitch in same staff
    bool tie_stop{false};     // sustained continuation (excluded from fresh MIDI attacks)
  };

  struct LessonEvent {
    int start_tick{0};        // [0, measure_ticks)
    RhythmicValue duration{};
    bool is_rest{false};
    std::uint8_t voice{0};    // 0 = primary, 1 = optional secondary voice
    std::vector<LessonNote> notes;
  };

  struct HarmonicAnnotation {
    int start_tick{0};
    std::string chord_symbol;  // e.g. "Gm", "Ab/C", "D7"
    std::string roman_numeral; // e.g. "i", "bII6", "V7"
  };

  struct LessonMeasure {
    std::uint32_t bar_number{1};
    std::optional<KeySignature> key_change{};
    std::optional<TimeSignature> meter_change{};
    std::vector<LessonEvent> treble_events;
    std::vector<LessonEvent> bass_events;
    std::vector<HarmonicAnnotation> harmony;
  };

  struct LessonPhrase {
    std::string label;        // e.g. "Phrase A (Bars 1-4)"
    std::size_t start_bar{0}; // 0-based inclusive
    std::size_t end_bar{3};   // 0-based inclusive
  };

  struct TheoryLens {
    std::string summary;
    std::string pattern_tip;
    std::string technique_tip;
    std::string ballade_link;
  };

  struct LessonMetadata {
    std::string id;
    std::uint8_t tier{1};          // 1..6 built-in, 0 imported
    std::uint8_t order_in_tier{1};
    std::string title;
    std::string composer_source;
    KeySignature key{0, Mode::Major};
    TimeSignature time_signature{4, 4};
    double target_bpm{84.0};
    TheoryLens theory{};
    bool is_imported{false};
    std::string source_filename{};
  };

  struct LessonScore {
    LessonMetadata meta{};
    std::vector<LessonMeasure> measures;
    std::vector<LessonPhrase> phrases;
  };
  ```
- **Pure Helpers & Validation:**
  - `validate_lesson_score(const LessonScore&) -> std::expected<void, std::vector<std::string>>`: verifies non-empty measures, exact voice tick coverage per measure (`<= measure_ticks`, primary voice exact sum), valid MIDI range `[21, 108]`, `note.midi == note.spelled.to_midi()`, valid fingerings `0..5`, valid phrase bounds, and parseable chord symbols via `parse_chord_symbol`.
  - `auto_partition_phrases(std::size_t bar_count) -> std::vector<LessonPhrase>`: deterministic 4-bar chunks (`<= 24` bars) or 8-bar chunks (`> 24` bars) when explicit phrases are omitted.
  - Pure JSON codec (`lesson_score_to_json` / `lesson_score_from_json` returning `std::expected<LessonScore, std::string>`) via isolated `nlohmann/json` in `.cpp`.

### Slice 7.2 — Pure Beaming & Tie Layout Geometry (`src/core/staff_layout.*` via `cpp23-tdd`)
- Add pure, deterministic engraving layout helpers to `src/core/staff_layout.hpp/.cpp`:
  - **Meter-Aware Beam Grouping (`compute_beam_groups`):**
    - Groups contiguous beamable non-rest events (`Eighth`, `Sixteenth`, including dotted-8th + 16th pairs) within beat/sub-bar boundaries:
      - `4/4`: beam within half-bar boundaries `[0, 960)` and `[960, 1920)` (or per-quarter beat when mixing 16ths for clarity).
      - `3/4` & `2/4`: beam per quarter-note beat `[0, 480)`, `[480, 960)`, `[960, 1440)` (or 2-eighth pairs).
      - `6/8`: beam in compound dotted-quarter groups `[0, 720)` and `[720, 1440)` (3 eighths per beam!).
      - `6/4`: beam in half-bar / 3-quarter groups `[0, 1440)` and `[1440, 2880)` or per beat.
    - Computes unified group `StemDirection` (majority / farthest-from-middle-line rule), sloped beam line endpoints `(start_y, end_y)` clamped to a tasteful maximum engraving slope (`<= 0.5` staff spaces per beat so beams never look jagged), and per-note stem tip heights.
    - Handles **secondary 16th beams** (full span between adjacent 16ths, or short fractional hook beam for lone 16th in a dotted-8th + 16th pair!).
  - **Tie Span Resolution (`resolve_tie_spans`):**
    - Matches `(measure_idx, event_idx, note_idx)` with `tie_start == true` to the next matching MIDI pitch with `tie_stop == true` in the same staff.
    - Flags whether the tie stays within the same 4-bar system (`Complete`) or crosses a system break (`OutgoingHalf` / `IncomingHalf`).
    - Selects tie arch orientation (above or below notehead, opposite stem direction for single notes; outer-curve direction for chords).

### Slice 7.3 — SMuFL Glyphs & Backward-Compatible `staff_renderer` Upgrade (`src/ui/smufl_glyphs.hpp`, `src/ui/staff_renderer.*`)
- **New SMuFL Codepoints (verified in bundled `assets/fonts/Bravura.otf` v1.482):**
  - Time signature digits `0–9` (`U+E080`..`U+E089`) with helper `time_sig_digit_glyph(std::uint8_t digit)`.
  - Flags: `kGlyphFlag8thUp` (`U+E240`), `kGlyphFlag8thDown` (`U+E241`), `kGlyphFlag16thUp` (`U+E242`), `kGlyphFlag16thDown` (`U+E243`).
  - Rests: `kGlyphRest8th` (`U+E4E6`), `kGlyphRest16th` (`U+E4E7`).
  - Augmentation dot: `kGlyphAugmentationDot` (`U+E1E7`).
- **Backward-Compatible Extensions to `src/ui/staff_renderer.hpp`:**
  - Extend `NoteDuration` and `RestDuration` with `Eighth` and `Sixteenth`.
  - Add optional fields (defaulted so zero existing callers break):
    - `StaffNote`: `bool dotted{false}`, `std::uint8_t finger{0}`, `bool tie_start{false}`, `bool tie_stop{false}`.
    - `StaffChord`: `bool dotted{false}`, `bool treble_rest_dotted{false}`, `bool bass_rest_dotted{false}`, `std::optional<int> tick_offset{}` (or float beat offset for exact proportional spacing).
    - `StaffMeasure`: `std::vector<StaffHarmonicLabel> harmony_labels{}` (`beat_offset`, `chord_symbol`, `roman_numeral`), `std::optional<core::TimeSignature> time_signature{}` (for mid-piece meter changes).
    - `StaffRenderOptions`: `std::optional<core::TimeSignature> time_signature{}` (supersedes `show_time_signature_4_4` while keeping `show_time_signature_4_4` working), `bool show_fingerings{true}`, `bool show_harmony_labels{true}`.
- **Rendering Additions (`src/ui/staff_renderer.cpp`):**
  - Arbitrary time signatures (`2/4`, `3/4`, `4/4`, `6/8`, `6/4`) cleanly centered vertically on both Treble and Bass staves.
  - Sloped primary (8th) and secondary (16th + hook) beam quads (`AddQuadFilled`) and SMuFL flags for unbeamed 8th/16th notes.
  - Augmentation dots positioned in the staff space above when notehead sits on a staff line (classic engraving rule).
  - Smooth tapered Bezier tie curves (same-system and system-break half-ties).
  - Crisp fingering numerals (`1–5`) in Inter SemiBold, avoiding collision with ledger lines, stems, and beams.
  - Chord symbols (primary ink) + Roman numerals (muted secondary ink) neatly aligned above the Treble staff, adjusting `compute_staff_canvas_height` top margin when `show_harmony_labels` is active.
- **Adapter Helper (`src/ui/lesson_staff_adapter.hpp/.cpp` or inside `staff_renderer`):**
  - Pure mapping from a slice of `core::LessonMeasure` (4-bar system) + per-target feedback state → `std::vector<StaffMeasure>`.

### Slice 7.4 — Visual Engraving Showcase & Regression Verification (`src/ui/staff_showcase.cpp`)
- Add a **"Classical Notation Specimen"** toggle/section in `Dev Tools → Staff Showcase` rendering a 4-bar specimen exercising:
  - `6/8` compound beaming (3-eighth groups), dotted quarter + 16th beams/hooks, unbeamed 8th/16th flags, barline ties, fingerings `1–5`, and chord + Roman-numeral labels (`Gm / i`, `Ab/C / bII6`, `D7 / V7`).
- Verify zero visual regressions on existing Showcase, Warm-Up, Flashcards, and Etudes views.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug` compiles cleanly with **zero warnings** under `-Wall -Wextra -Wpedantic`.
2. `ctest --preset native-debug --output-on-failure` passes 100%, including new `tests/lesson_score_tests.cpp` and expanded `tests/staff_layout_tests.cpp` (tick math for all 5 meters, beaming groups, secondary 16th hooks, tie span matching across bars & system breaks, JSON round-trip, validation diagnostics).
3. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with zero warnings.
4. `src/core/` purity check returns 0 matches (zero UI/SDL/MIDI/Emscripten/clock/random includes or calls).
5. `clang-format --dry-run --Werror` clean on all modified/created files.
6. Headless Light & Dark screenshot checks (`--tab showcase`, `--tab warmup`, `--tab etude`) verify crisp beams, flags, dots, ties, time signatures, fingerings, chord labels, and zero regressions on existing screens.

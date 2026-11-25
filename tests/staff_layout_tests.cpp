#include "core/staff_layout.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

using namespace selfano::core;
using Catch::Matchers::WithinAbs;

TEST_CASE("step_to_y maps diatonic steps linearly from staff bottom line", "[staff_layout]") {
  const float bottom_y = 200.0f;
  const float space = 10.0f; // half_space = 5.0f

  // Treble: E4(30) at bottom_y, G4(32) -10, B4(34) -20, F5(38) -40, C4(28) +10
  REQUIRE_THAT(step_to_y(30, Clef::Treble, bottom_y, space), WithinAbs(200.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(32, Clef::Treble, bottom_y, space), WithinAbs(190.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(34, Clef::Treble, bottom_y, space), WithinAbs(180.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(38, Clef::Treble, bottom_y, space), WithinAbs(160.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(28, Clef::Treble, bottom_y, space), WithinAbs(210.0f, 1e-4f));

  // Bass: G2(18) at bottom_y, D3(22) -20, F3(24) -30, A3(26) -40, E2(16) +10, C4(28) -50
  REQUIRE_THAT(step_to_y(18, Clef::Bass, bottom_y, space), WithinAbs(200.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(22, Clef::Bass, bottom_y, space), WithinAbs(180.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(24, Clef::Bass, bottom_y, space), WithinAbs(170.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(26, Clef::Bass, bottom_y, space), WithinAbs(160.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(16, Clef::Bass, bottom_y, space), WithinAbs(210.0f, 1e-4f));
  REQUIRE_THAT(step_to_y(28, Clef::Bass, bottom_y, space), WithinAbs(150.0f, 1e-4f));
}

TEST_CASE("compute_ledger_line_steps detects exact ledger lines above and below staff",
          "[staff_layout]") {
  // On-staff and adjacent space notes need zero ledger lines
  REQUIRE(compute_ledger_line_steps(30, Clef::Treble).empty()); // E4
  REQUIRE(compute_ledger_line_steps(38, Clef::Treble).empty()); // F5
  REQUIRE(compute_ledger_line_steps(29, Clef::Treble).empty()); // D4 space below
  REQUIRE(compute_ledger_line_steps(39, Clef::Treble).empty()); // G5 space above

  REQUIRE(compute_ledger_line_steps(18, Clef::Bass).empty()); // G2
  REQUIRE(compute_ledger_line_steps(26, Clef::Bass).empty()); // A3
  REQUIRE(compute_ledger_line_steps(17, Clef::Bass).empty()); // F2 space below
  REQUIRE(compute_ledger_line_steps(27, Clef::Bass).empty()); // B3 space above

  // Treble ledger lines
  REQUIRE(compute_ledger_line_steps(28, Clef::Treble) == std::vector<int>{28});     // C4
  REQUIRE(compute_ledger_line_steps(26, Clef::Treble) == std::vector<int>{28, 26}); // A3
  REQUIRE(compute_ledger_line_steps(40, Clef::Treble) == std::vector<int>{40});     // A5
  REQUIRE(compute_ledger_line_steps(42, Clef::Treble) == std::vector<int>{40, 42}); // C6

  // Bass ledger lines
  REQUIRE(compute_ledger_line_steps(16, Clef::Bass) == std::vector<int>{16});     // E2
  REQUIRE(compute_ledger_line_steps(14, Clef::Bass) == std::vector<int>{16, 14}); // C2
  REQUIRE(compute_ledger_line_steps(28, Clef::Bass) == std::vector<int>{28});     // C4
}

TEST_CASE("MeasureAccidentalState tracks courtesy and cancellation naturals per bar",
          "[staff_layout]") {
  const auto g_maj = make_key_signature(1, Mode::Major).value();
  MeasureAccidentalState state{g_maj};

  const auto fs4 =
      SpelledNote{.letter = NoteLetter::F, .accidental = Accidental::Sharp, .octave = 4};
  const auto f_nat4 =
      SpelledNote{.letter = NoteLetter::F, .accidental = Accidental::Natural, .octave = 4};

  REQUIRE_FALSE(state.resolve_and_record(fs4).has_value());
  REQUIRE(state.resolve_and_record(f_nat4) == Accidental::Natural);
  REQUIRE_FALSE(state.resolve_and_record(f_nat4).has_value());
  REQUIRE(state.resolve_and_record(fs4) == Accidental::Sharp);

  state.reset_measure();
  REQUIRE_FALSE(state.resolve_and_record(fs4).has_value());
}

TEST_CASE("layout_chord computes stem direction, 2nd-interval offsets, and accidental columns",
          "[staff_layout]") {
  const float w = kNoteheadBlackWidthSpaces;
  const float expected_offset = w * kSecondOffsetRatio;

  SECTION("Stem directions relative to staff middle line") {
    const std::array g4{
        ChordNoteLayoutInput{.source_index = 0, .step = 32, .displayed_accidental = std::nullopt}};
    REQUIRE(layout_chord(g4, Clef::Treble).stem_direction == StemDirection::Up);

    const std::array b4{
        ChordNoteLayoutInput{.source_index = 0, .step = 34, .displayed_accidental = std::nullopt}};
    REQUIRE(layout_chord(b4, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array d5{
        ChordNoteLayoutInput{.source_index = 0, .step = 36, .displayed_accidental = std::nullopt}};
    REQUIRE(layout_chord(d5, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array c_triad_low{
        ChordNoteLayoutInput{.source_index = 0, .step = 28, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 30, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 2, .step = 32, .displayed_accidental = std::nullopt},
    };
    REQUIRE(layout_chord(c_triad_low, Clef::Treble).stem_direction == StemDirection::Up);

    const std::array a_triad_high{
        ChordNoteLayoutInput{.source_index = 0, .step = 33, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 35, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 2, .step = 37, .displayed_accidental = std::nullopt},
    };
    REQUIRE(layout_chord(a_triad_high, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array bass_b2{
        ChordNoteLayoutInput{.source_index = 0, .step = 20, .displayed_accidental = std::nullopt}};
    REQUIRE(layout_chord(bass_b2, Clef::Bass).stem_direction == StemDirection::Up);

    const std::array bass_d3{
        ChordNoteLayoutInput{.source_index = 0, .step = 22, .displayed_accidental = std::nullopt}};
    REQUIRE(layout_chord(bass_d3, Clef::Bass).stem_direction == StemDirection::Down);
  }

  SECTION("2nd-interval offsets for Stem Up and Stem Down") {
    // Stem-up dyad F4-G4
    const std::array fg_up{
        ChordNoteLayoutInput{.source_index = 0, .step = 31, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 32, .displayed_accidental = std::nullopt},
    };
    const auto fg_layout = layout_chord(fg_up, Clef::Treble, w);
    REQUIRE(fg_layout.stem_direction == StemDirection::Up);
    REQUIRE_THAT(fg_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(fg_layout.noteheads[1].x_offset_spaces, WithinAbs(expected_offset, 1e-4f));
    REQUIRE_THAT(fg_layout.min_notehead_x_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(fg_layout.max_notehead_x_spaces, WithinAbs(expected_offset + w, 1e-4f));

    // Stem-up 3-note cluster C4-D4-E4
    const std::array cde_up{
        ChordNoteLayoutInput{.source_index = 0, .step = 28, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 29, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 2, .step = 30, .displayed_accidental = std::nullopt},
    };
    const auto cde_layout = layout_chord(cde_up, Clef::Treble, w);
    REQUIRE(cde_layout.stem_direction == StemDirection::Up);
    REQUIRE_THAT(cde_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cde_layout.noteheads[1].x_offset_spaces, WithinAbs(expected_offset, 1e-4f));
    REQUIRE_THAT(cde_layout.noteheads[2].x_offset_spaces, WithinAbs(0.0f, 1e-4f));

    // Stem-down dyad C5-D5
    const std::array cd_down{
        ChordNoteLayoutInput{.source_index = 0, .step = 35, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 36, .displayed_accidental = std::nullopt},
    };
    const auto cd_layout = layout_chord(cd_down, Clef::Treble, w);
    REQUIRE(cd_layout.stem_direction == StemDirection::Down);
    REQUIRE_THAT(cd_layout.noteheads[0].x_offset_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cd_layout.noteheads[1].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cd_layout.min_notehead_x_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cd_layout.max_notehead_x_spaces, WithinAbs(w, 1e-4f));

    // Stem-down 3-note cluster C5-D5-E5
    const std::array cde_down{
        ChordNoteLayoutInput{.source_index = 0, .step = 35, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 36, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 2, .step = 37, .displayed_accidental = std::nullopt},
    };
    const auto cde_down_layout = layout_chord(cde_down, Clef::Treble, w);
    REQUIRE(cde_down_layout.stem_direction == StemDirection::Down);
    REQUIRE_THAT(cde_down_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cde_down_layout.noteheads[1].x_offset_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cde_down_layout.noteheads[2].x_offset_spaces, WithinAbs(0.0f, 1e-4f));

    // Non-adjacent C4-E4-G4 triad has zero offsets
    const std::array c_triad{
        ChordNoteLayoutInput{.source_index = 0, .step = 28, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 1, .step = 30, .displayed_accidental = std::nullopt},
        ChordNoteLayoutInput{.source_index = 2, .step = 32, .displayed_accidental = std::nullopt},
    };
    const auto c_layout = layout_chord(c_triad, Clef::Treble, w);
    for (const auto& nh : c_layout.noteheads) {
      REQUIRE_THAT(nh.x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    }
  }

  SECTION("Accidental column stagger avoids vertical overlap within 5 diatonic steps") {
    // F#4(31) + A#4(33): 2 steps apart -> distinct columns 0 and 1
    const std::array close_two{
        ChordNoteLayoutInput{
            .source_index = 0, .step = 31, .displayed_accidental = Accidental::Sharp},
        ChordNoteLayoutInput{
            .source_index = 1, .step = 33, .displayed_accidental = Accidental::Sharp},
    };
    const auto close_layout = layout_chord(close_two, Clef::Treble, w);
    std::set<int> cols_two{close_layout.noteheads[0].accidental_column,
                           close_layout.noteheads[1].accidental_column};
    REQUIRE(cols_two == std::set<int>{0, 1});

    // F#4(31) + D#5(36): 5 steps apart -> both fit in column 0
    const std::array far_two{
        ChordNoteLayoutInput{
            .source_index = 0, .step = 31, .displayed_accidental = Accidental::Sharp},
        ChordNoteLayoutInput{
            .source_index = 1, .step = 36, .displayed_accidental = Accidental::Sharp},
    };
    const auto far_layout = layout_chord(far_two, Clef::Treble, w);
    REQUIRE(far_layout.noteheads[0].accidental_column == 0);
    REQUIRE(far_layout.noteheads[1].accidental_column == 0);

    // 3 close accidentals C#4(28), D#4(29), E#4(30) -> columns 0, 1, 2
    const std::array close_three{
        ChordNoteLayoutInput{
            .source_index = 0, .step = 28, .displayed_accidental = Accidental::Sharp},
        ChordNoteLayoutInput{
            .source_index = 1, .step = 29, .displayed_accidental = Accidental::Sharp},
        ChordNoteLayoutInput{
            .source_index = 2, .step = 30, .displayed_accidental = Accidental::Sharp},
    };
    const auto three_layout = layout_chord(close_three, Clef::Treble, w);
    std::set<int> cols_three{
        three_layout.noteheads[0].accidental_column,
        three_layout.noteheads[1].accidental_column,
        three_layout.noteheads[2].accidental_column,
    };
    REQUIRE(cols_three == std::set<int>{0, 1, 2});
  }

  SECTION("Ledger lines union across multi-note chord") {
    const std::array low_dyad{
        ChordNoteLayoutInput{
            .source_index = 0, .step = 26, .displayed_accidental = std::nullopt}, // A3 (28, 26)
        ChordNoteLayoutInput{
            .source_index = 1, .step = 28, .displayed_accidental = std::nullopt}, // C4 (28)
    };
    const auto low_layout = layout_chord(low_dyad, Clef::Treble, w);
    REQUIRE(low_layout.ledger_line_steps == std::vector<int>{26, 28});
  }
}

TEST_CASE("compute_beam_groups groups 8th and 16th notes cleanly per meter", "[staff_layout]") {
  SECTION("4/4 beams in half-bar groups for 8ths and splits on rests") {
    std::vector<BeamEventInput> evs{
        {.event_index = 0,
         .start_tick = 0,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 2.0f,
         .min_step = 30,
         .max_step = 30},
        {.event_index = 1,
         .start_tick = 240,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 5.0f,
         .min_step = 31,
         .max_step = 31},
        {.event_index = 2,
         .start_tick = 480,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 8.0f,
         .min_step = 32,
         .max_step = 32},
        {.event_index = 3,
         .start_tick = 720,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 11.0f,
         .min_step = 33,
         .max_step = 33},
        {.event_index = 4,
         .start_tick = 960,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = true,
         .x_spaces = 14.0f,
         .min_step = 34,
         .max_step = 34}, // rest breaks group
        {.event_index = 5,
         .start_tick = 1200,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 17.0f,
         .min_step = 36,
         .max_step = 36},
        {.event_index = 6,
         .start_tick = 1440,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 20.0f,
         .min_step = 35,
         .max_step = 35},
        {.event_index = 7,
         .start_tick = 1680,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 23.0f,
         .min_step = 34,
         .max_step = 34},
    };
    const auto groups =
        compute_beam_groups(evs, TimeSignature{.numerator = 4, .denominator = 4}, Clef::Treble);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].event_indices == std::vector<std::size_t>{0, 1, 2, 3});
    REQUIRE(groups[0].stem_direction == StemDirection::Up);
    REQUIRE(groups[1].event_indices == std::vector<std::size_t>{5, 6, 7});
    REQUIRE(groups[1].stem_direction == StemDirection::Down);
    // Beam slope is tastefully clamped
    REQUIRE(std::abs(groups[0].end_y_spaces - groups[0].start_y_spaces) <= 2.5f);
  }

  SECTION("6/8 beams in compound dotted-quarter groups of 3 eighths") {
    std::vector<BeamEventInput> evs{
        {.event_index = 0,
         .start_tick = 0,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 2.0f,
         .min_step = 31,
         .max_step = 31},
        {.event_index = 1,
         .start_tick = 240,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 5.0f,
         .min_step = 32,
         .max_step = 32},
        {.event_index = 2,
         .start_tick = 480,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 8.0f,
         .min_step = 33,
         .max_step = 33},
        {.event_index = 3,
         .start_tick = 720,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 11.0f,
         .min_step = 34,
         .max_step = 34},
        {.event_index = 4,
         .start_tick = 960,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 14.0f,
         .min_step = 35,
         .max_step = 35},
        {.event_index = 5,
         .start_tick = 1200,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 17.0f,
         .min_step = 36,
         .max_step = 36},
    };
    const auto groups =
        compute_beam_groups(evs, TimeSignature{.numerator = 6, .denominator = 8}, Clef::Treble);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].event_indices == std::vector<std::size_t>{0, 1, 2});
    REQUIRE(groups[1].event_indices == std::vector<std::size_t>{3, 4, 5});
  }

  SECTION("3/4 beams within quarter beats when mixing or on beat boundaries") {
    std::vector<BeamEventInput> evs{
        {.event_index = 0,
         .start_tick = 0,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 2.0f,
         .min_step = 32,
         .max_step = 32},
        {.event_index = 1,
         .start_tick = 240,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 5.0f,
         .min_step = 33,
         .max_step = 33},
        {.event_index = 2,
         .start_tick = 480,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 8.0f,
         .min_step = 32,
         .max_step = 32},
        {.event_index = 3,
         .start_tick = 720,
         .duration_ticks = 240,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 11.0f,
         .min_step = 31,
         .max_step = 31},
        {.event_index = 4,
         .start_tick = 960,
         .duration_ticks = 480,
         .base_duration = NoteBaseDuration::Quarter,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 14.0f,
         .min_step = 30,
         .max_step = 30},
    };
    const auto groups =
        compute_beam_groups(evs, TimeSignature{.numerator = 3, .denominator = 4}, Clef::Treble);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].event_indices == std::vector<std::size_t>{0, 1});
    REQUIRE(groups[1].event_indices == std::vector<std::size_t>{2, 3});
  }

  SECTION("Secondary 16th beams and dotted-8th + 16th fractional hooks") {
    std::vector<BeamEventInput> evs{
        // Beat 1: dotted 8th + lone 16th -> hook
        {.event_index = 0,
         .start_tick = 0,
         .duration_ticks = 360,
         .base_duration = NoteBaseDuration::Eighth,
         .dotted = true,
         .is_rest = false,
         .x_spaces = 2.0f,
         .min_step = 31,
         .max_step = 31},
        {.event_index = 1,
         .start_tick = 360,
         .duration_ticks = 120,
         .base_duration = NoteBaseDuration::Sixteenth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 6.0f,
         .min_step = 32,
         .max_step = 32},
        // Beat 2: four 16ths -> full secondary beam
        {.event_index = 2,
         .start_tick = 480,
         .duration_ticks = 120,
         .base_duration = NoteBaseDuration::Sixteenth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 9.0f,
         .min_step = 32,
         .max_step = 32},
        {.event_index = 3,
         .start_tick = 600,
         .duration_ticks = 120,
         .base_duration = NoteBaseDuration::Sixteenth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 11.5f,
         .min_step = 33,
         .max_step = 33},
        {.event_index = 4,
         .start_tick = 720,
         .duration_ticks = 120,
         .base_duration = NoteBaseDuration::Sixteenth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 14.0f,
         .min_step = 34,
         .max_step = 34},
        {.event_index = 5,
         .start_tick = 840,
         .duration_ticks = 120,
         .base_duration = NoteBaseDuration::Sixteenth,
         .dotted = false,
         .is_rest = false,
         .x_spaces = 16.5f,
         .min_step = 33,
         .max_step = 33},
    };
    const auto groups =
        compute_beam_groups(evs, TimeSignature{.numerator = 2, .denominator = 4}, Clef::Treble);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].secondary_beams.size() == 1);
    REQUIRE(groups[0].secondary_beams[0].is_hook);
    REQUIRE(groups[1].secondary_beams.size() == 1);
    REQUIRE_FALSE(groups[1].secondary_beams[0].is_hook);
  }
}

TEST_CASE("resolve_tie_spans handles complete, outgoing, incoming ties and orientation",
          "[staff_layout]") {
  const std::vector<TieEndpointRef> refs{
      // Incoming tie from previous system at bar 0
      TieEndpointRef{.measure_index = 0,
                     .event_index = 0,
                     .note_index = 0,
                     .midi = 62,
                     .step = 29,
                     .x_spaces = 2.0f,
                     .stem_direction = StemDirection::Up,
                     .chord_note_count = 1,
                     .index_in_chord = 0,
                     .tie_start = false,
                     .tie_stop = true},
      // Complete tie from bar 0 to bar 1
      TieEndpointRef{.measure_index = 0,
                     .event_index = 1,
                     .note_index = 0,
                     .midi = 67,
                     .step = 32,
                     .x_spaces = 10.0f,
                     .stem_direction = StemDirection::Up,
                     .chord_note_count = 1,
                     .index_in_chord = 0,
                     .tie_start = true,
                     .tie_stop = false},
      TieEndpointRef{.measure_index = 1,
                     .event_index = 0,
                     .note_index = 0,
                     .midi = 67,
                     .step = 32,
                     .x_spaces = 15.0f,
                     .stem_direction = StemDirection::Up,
                     .chord_note_count = 1,
                     .index_in_chord = 0,
                     .tie_start = false,
                     .tie_stop = true},
      // Outgoing tie at bar 1 crossing system break
      TieEndpointRef{.measure_index = 1,
                     .event_index = 1,
                     .note_index = 0,
                     .midi = 72,
                     .step = 35,
                     .x_spaces = 22.0f,
                     .stem_direction = StemDirection::Down,
                     .chord_note_count = 1,
                     .index_in_chord = 0,
                     .tie_start = true,
                     .tie_stop = false},
  };

  const auto spans = resolve_tie_spans(refs);
  REQUIRE(spans.size() == 3);
  REQUIRE(spans[0].kind == TieSpanKind::IncomingHalf);
  REQUIRE(spans[0].direction == TieDirection::Below); // Stem Up -> Tie Below
  REQUIRE(spans[1].kind == TieSpanKind::Complete);
  REQUIRE(spans[1].start_measure_index == 0);
  REQUIRE(spans[1].end_measure_index == 1);
  REQUIRE(spans[2].kind == TieSpanKind::OutgoingHalf);
  REQUIRE(spans[2].direction == TieDirection::Above); // Stem Down -> Tie Above
}

TEST_CASE("Two-voice forced stem directions and collision offset avoidance work cleanly",
          "[staff_layout]") {
  const float w = kNoteheadBlackWidthSpaces;
  const std::array high_c5{
      ChordNoteLayoutInput{.source_index = 0, .step = 35, .displayed_accidental = std::nullopt}};
  const std::array high_b4{
      ChordNoteLayoutInput{.source_index = 0, .step = 34, .displayed_accidental = std::nullopt}};
  const std::array low_f4{
      ChordNoteLayoutInput{.source_index = 0, .step = 31, .displayed_accidental = std::nullopt}};

  // Forced StemDirection::Up on C5 (even above middle line B4)
  const auto v0_c5 = layout_chord(high_c5, Clef::Treble, w, StemDirection::Up);
  REQUIRE(v0_c5.stem_direction == StemDirection::Up);

  // Forced StemDirection::Down on F4 (below middle line B4)
  const auto v1_f4 = layout_chord(low_f4, Clef::Treble, w, StemDirection::Down);
  REQUIRE(v1_f4.stem_direction == StemDirection::Down);

  // C5 vs F4 (5th apart) -> no horizontal shift
  REQUIRE_THAT(resolve_two_voice_collision(v0_c5, v1_f4, w), WithinAbs(0.0f, 1e-5f));

  // C5 vs B4 (2nd apart) -> shift voice 1 right
  const auto v1_b4 = layout_chord(high_b4, Clef::Treble, w, StemDirection::Down);
  REQUIRE(resolve_two_voice_collision(v0_c5, v1_b4, w) > w);

  // Voice crossing (voice0 F4 below voice1 C5) -> shift voice 1 right
  const auto v0_f4 = layout_chord(low_f4, Clef::Treble, w, StemDirection::Up);
  const auto v1_c5 = layout_chord(high_c5, Clef::Treble, w, StemDirection::Down);
  REQUIRE(resolve_two_voice_collision(v0_f4, v1_c5, w) > w);

  // Forced stem direction on compute_beam_groups
  std::vector<BeamEventInput> beam_in{
      {.event_index = 0,
       .start_tick = 0,
       .duration_ticks = 240,
       .base_duration = NoteBaseDuration::Eighth,
       .dotted = false,
       .is_rest = false,
       .x_spaces = 2.0f,
       .min_step = 35,
       .max_step = 36},
      {.event_index = 1,
       .start_tick = 240,
       .duration_ticks = 240,
       .base_duration = NoteBaseDuration::Eighth,
       .dotted = false,
       .is_rest = false,
       .x_spaces = 5.0f,
       .min_step = 35,
       .max_step = 37},
  };
  const auto forced_up =
      compute_beam_groups(beam_in, TimeSignature{4, 4}, Clef::Treble, w, StemDirection::Up);
  REQUIRE(forced_up.size() == 1U);
  REQUIRE(forced_up[0].stem_direction == StemDirection::Up);
}

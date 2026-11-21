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

  const auto fs4 = SpelledNote{NoteLetter::F, Accidental::Sharp, 4};
  const auto f_nat4 = SpelledNote{NoteLetter::F, Accidental::Natural, 4};

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
    const std::array g4{ChordNoteLayoutInput{0, 32, std::nullopt}};
    REQUIRE(layout_chord(g4, Clef::Treble).stem_direction == StemDirection::Up);

    const std::array b4{ChordNoteLayoutInput{0, 34, std::nullopt}};
    REQUIRE(layout_chord(b4, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array d5{ChordNoteLayoutInput{0, 36, std::nullopt}};
    REQUIRE(layout_chord(d5, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array c_triad_low{
        ChordNoteLayoutInput{0, 28, std::nullopt},
        ChordNoteLayoutInput{1, 30, std::nullopt},
        ChordNoteLayoutInput{2, 32, std::nullopt},
    };
    REQUIRE(layout_chord(c_triad_low, Clef::Treble).stem_direction == StemDirection::Up);

    const std::array a_triad_high{
        ChordNoteLayoutInput{0, 33, std::nullopt},
        ChordNoteLayoutInput{1, 35, std::nullopt},
        ChordNoteLayoutInput{2, 37, std::nullopt},
    };
    REQUIRE(layout_chord(a_triad_high, Clef::Treble).stem_direction == StemDirection::Down);

    const std::array bass_b2{ChordNoteLayoutInput{0, 20, std::nullopt}};
    REQUIRE(layout_chord(bass_b2, Clef::Bass).stem_direction == StemDirection::Up);

    const std::array bass_d3{ChordNoteLayoutInput{0, 22, std::nullopt}};
    REQUIRE(layout_chord(bass_d3, Clef::Bass).stem_direction == StemDirection::Down);
  }

  SECTION("2nd-interval offsets for Stem Up and Stem Down") {
    // Stem-up dyad F4-G4
    const std::array fg_up{
        ChordNoteLayoutInput{0, 31, std::nullopt},
        ChordNoteLayoutInput{1, 32, std::nullopt},
    };
    const auto fg_layout = layout_chord(fg_up, Clef::Treble, w);
    REQUIRE(fg_layout.stem_direction == StemDirection::Up);
    REQUIRE_THAT(fg_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(fg_layout.noteheads[1].x_offset_spaces, WithinAbs(expected_offset, 1e-4f));
    REQUIRE_THAT(fg_layout.min_notehead_x_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(fg_layout.max_notehead_x_spaces, WithinAbs(expected_offset + w, 1e-4f));

    // Stem-up 3-note cluster C4-D4-E4
    const std::array cde_up{
        ChordNoteLayoutInput{0, 28, std::nullopt},
        ChordNoteLayoutInput{1, 29, std::nullopt},
        ChordNoteLayoutInput{2, 30, std::nullopt},
    };
    const auto cde_layout = layout_chord(cde_up, Clef::Treble, w);
    REQUIRE(cde_layout.stem_direction == StemDirection::Up);
    REQUIRE_THAT(cde_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cde_layout.noteheads[1].x_offset_spaces, WithinAbs(expected_offset, 1e-4f));
    REQUIRE_THAT(cde_layout.noteheads[2].x_offset_spaces, WithinAbs(0.0f, 1e-4f));

    // Stem-down dyad C5-D5
    const std::array cd_down{
        ChordNoteLayoutInput{0, 35, std::nullopt},
        ChordNoteLayoutInput{1, 36, std::nullopt},
    };
    const auto cd_layout = layout_chord(cd_down, Clef::Treble, w);
    REQUIRE(cd_layout.stem_direction == StemDirection::Down);
    REQUIRE_THAT(cd_layout.noteheads[0].x_offset_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cd_layout.noteheads[1].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cd_layout.min_notehead_x_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cd_layout.max_notehead_x_spaces, WithinAbs(w, 1e-4f));

    // Stem-down 3-note cluster C5-D5-E5
    const std::array cde_down{
        ChordNoteLayoutInput{0, 35, std::nullopt},
        ChordNoteLayoutInput{1, 36, std::nullopt},
        ChordNoteLayoutInput{2, 37, std::nullopt},
    };
    const auto cde_down_layout = layout_chord(cde_down, Clef::Treble, w);
    REQUIRE(cde_down_layout.stem_direction == StemDirection::Down);
    REQUIRE_THAT(cde_down_layout.noteheads[0].x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(cde_down_layout.noteheads[1].x_offset_spaces, WithinAbs(-expected_offset, 1e-4f));
    REQUIRE_THAT(cde_down_layout.noteheads[2].x_offset_spaces, WithinAbs(0.0f, 1e-4f));

    // Non-adjacent C4-E4-G4 triad has zero offsets
    const std::array c_triad{
        ChordNoteLayoutInput{0, 28, std::nullopt},
        ChordNoteLayoutInput{1, 30, std::nullopt},
        ChordNoteLayoutInput{2, 32, std::nullopt},
    };
    const auto c_layout = layout_chord(c_triad, Clef::Treble, w);
    for (const auto& nh : c_layout.noteheads) {
      REQUIRE_THAT(nh.x_offset_spaces, WithinAbs(0.0f, 1e-4f));
    }
  }

  SECTION("Accidental column stagger avoids vertical overlap within 5 diatonic steps") {
    // F#4(31) + A#4(33): 2 steps apart -> distinct columns 0 and 1
    const std::array close_two{
        ChordNoteLayoutInput{0, 31, Accidental::Sharp},
        ChordNoteLayoutInput{1, 33, Accidental::Sharp},
    };
    const auto close_layout = layout_chord(close_two, Clef::Treble, w);
    std::set<int> cols_two{close_layout.noteheads[0].accidental_column,
                           close_layout.noteheads[1].accidental_column};
    REQUIRE(cols_two == std::set<int>{0, 1});

    // F#4(31) + D#5(36): 5 steps apart -> both fit in column 0
    const std::array far_two{
        ChordNoteLayoutInput{0, 31, Accidental::Sharp},
        ChordNoteLayoutInput{1, 36, Accidental::Sharp},
    };
    const auto far_layout = layout_chord(far_two, Clef::Treble, w);
    REQUIRE(far_layout.noteheads[0].accidental_column == 0);
    REQUIRE(far_layout.noteheads[1].accidental_column == 0);

    // 3 close accidentals C#4(28), D#4(29), E#4(30) -> columns 0, 1, 2
    const std::array close_three{
        ChordNoteLayoutInput{0, 28, Accidental::Sharp},
        ChordNoteLayoutInput{1, 29, Accidental::Sharp},
        ChordNoteLayoutInput{2, 30, Accidental::Sharp},
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
        ChordNoteLayoutInput{0, 26, std::nullopt}, // A3 (28, 26)
        ChordNoteLayoutInput{1, 28, std::nullopt}, // C4 (28)
    };
    const auto low_layout = layout_chord(low_dyad, Clef::Treble, w);
    REQUIRE(low_layout.ledger_line_steps == std::vector<int>{26, 28});
  }
}

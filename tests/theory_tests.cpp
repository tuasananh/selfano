#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace selfano::core;

TEST_CASE("Interval metadata and diatonic transposition preserve enharmonic spelling", "[theory]") {
  REQUIRE(interval_semitones(Interval::Unison) == 0);
  REQUIRE(interval_semitones(Interval::MajorThird) == 4);
  REQUIRE(interval_semitones(Interval::PerfectFifth) == 7);
  REQUIRE(interval_semitones(Interval::Octave) == 12);

  REQUIRE(interval_diatonic_span(Interval::MajorThird) == 2);
  REQUIRE(interval_diatonic_span(Interval::DiminishedFifth) == 4);
  REQUIRE(interval_name(Interval::AugmentedFourth) == "A4");
  REQUIRE(interval_full_name(Interval::MinorThird) == "Minor 3rd");

  SECTION("Exact enharmonic transpositions") {
    const auto fs4 =
        SpelledNote{.letter = NoteLetter::F, .accidental = Accidental::Sharp, .octave = 4};
    REQUIRE(transpose_diatonic(fs4, Interval::MajorThird).value() ==
            SpelledNote{NoteLetter::A, Accidental::Sharp, 4});

    const auto db4 =
        SpelledNote{.letter = NoteLetter::D, .accidental = Accidental::Flat, .octave = 4};
    REQUIRE(transpose_diatonic(db4, Interval::MinorThird).value() ==
            SpelledNote{NoteLetter::F, Accidental::Flat, 4});

    const auto cs4 =
        SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Sharp, .octave = 4};
    REQUIRE(transpose_diatonic(cs4, Interval::PerfectFifth).value() ==
            SpelledNote{NoteLetter::G, Accidental::Sharp, 4});

    const auto c4 =
        SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Natural, .octave = 4};
    REQUIRE(transpose_diatonic(c4, Interval::MinorSecond, -1).value() ==
            SpelledNote{NoteLetter::B, Accidental::Natural, 3});
  }

  SECTION("Rejects triple accidentals and out-of-range MIDI") {
    const auto gss4 =
        SpelledNote{.letter = NoteLetter::G, .accidental = Accidental::DoubleSharp, .octave = 4};
    REQUIRE_FALSE(transpose_diatonic(gss4, Interval::AugmentedFourth).has_value());

    const auto g9 =
        SpelledNote{.letter = NoteLetter::G, .accidental = Accidental::Natural, .octave = 9};
    REQUIRE_FALSE(transpose_diatonic(g9, Interval::MinorSecond).has_value());
  }
}

TEST_CASE("build_scale generates 1- and 2-octave major, natural minor, and harmonic minor scales",
          "[theory]") {
  SECTION("C major 1 and 2 octaves") {
    const auto c_maj = make_key_signature(0, Mode::Major).value();
    const auto s1 = build_scale(c_maj, 4, 1);
    REQUIRE(s1.has_value());
    REQUIRE(s1->size() == 8);
    REQUIRE(format_note((*s1)[0]) == "C4");
    REQUIRE(format_note((*s1)[7]) == "C5");

    const auto s2 = build_scale(c_maj, 4, 2);
    REQUIRE(s2.has_value());
    REQUIRE(s2->size() == 15);
    REQUIRE(format_note((*s2)[14]) == "C6");

    for (std::size_t i = 1; i < s2->size(); ++i) {
      REQUIRE(to_midi((*s2)[i]).value() > to_midi((*s2)[i - 1]).value());
    }
  }

  SECTION("G major, Bb major, and F# major (with E#)") {
    const auto g_maj = build_scale(KeySignature{.fifths = 1, .mode = Mode::Major}, 4, 1).value();
    REQUIRE(format_note(g_maj[6]) == "F#5");

    const auto bb_maj = build_scale(KeySignature{.fifths = -2, .mode = Mode::Major}, 3, 1).value();
    REQUIRE(format_note(bb_maj[0]) == "Bb3");
    REQUIRE(format_note(bb_maj[3]) == "Eb4");

    const auto fs_maj = build_scale(KeySignature{.fifths = 6, .mode = Mode::Major}, 4, 1).value();
    REQUIRE(format_note(fs_maj[0]) == "F#4");
    REQUIRE(format_note(fs_maj[6]) == "E#5");
    REQUIRE(to_midi(fs_maj[6]).value() == 77);
  }

  SECTION("D natural minor vs D harmonic minor and A harmonic minor") {
    const auto d_min = KeySignature{.fifths = -1, .mode = Mode::Minor};
    const auto d_nat = build_scale(d_min, ScaleType::NaturalMinor, 4, 1).value();
    REQUIRE(format_note(d_nat[5]) == "Bb4");
    REQUIRE(format_note(d_nat[6]) == "C5");

    const auto d_harm = build_scale(d_min, ScaleType::HarmonicMinor, 4, 1).value();
    REQUIRE(format_note(d_harm[5]) == "Bb4");
    REQUIRE(format_note(d_harm[6]) == "C#5");

    const auto a_harm =
        build_scale(KeySignature{.fifths = 0, .mode = Mode::Minor}, ScaleType::HarmonicMinor, 3, 1)
            .value();
    REQUIRE(format_note(a_harm[6]) == "G#4");
  }
}

TEST_CASE("build_triad and build_diatonic_triad produce accurate spellings and inversions",
          "[theory]") {
  SECTION("C major triad inversions") {
    const auto c4 =
        SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Natural, .octave = 4};
    const auto root = build_triad(c4, TriadQuality::Major, Inversion::Root).value();
    REQUIRE(root.notes[0] == SpelledNote{NoteLetter::C, Accidental::Natural, 4});
    REQUIRE(root.notes[1] == SpelledNote{NoteLetter::E, Accidental::Natural, 4});
    REQUIRE(root.notes[2] == SpelledNote{NoteLetter::G, Accidental::Natural, 4});
    REQUIRE(format_chord_symbol(root) == "C");

    const auto first = build_triad(c4, TriadQuality::Major, Inversion::First).value();
    REQUIRE(first.notes[0] == SpelledNote{NoteLetter::E, Accidental::Natural, 4});
    REQUIRE(first.notes[1] == SpelledNote{NoteLetter::G, Accidental::Natural, 4});
    REQUIRE(first.notes[2] == SpelledNote{NoteLetter::C, Accidental::Natural, 5});

    const auto second = build_triad(c4, TriadQuality::Major, Inversion::Second).value();
    REQUIRE(second.notes[0] == SpelledNote{NoteLetter::G, Accidental::Natural, 4});
    REQUIRE(second.notes[1] == SpelledNote{NoteLetter::C, Accidental::Natural, 5});
    REQUIRE(second.notes[2] == SpelledNote{NoteLetter::E, Accidental::Natural, 5});
  }

  SECTION("F# major root triad uses A#4 rather than Bb4") {
    const auto fs_key = KeySignature{.fifths = 6, .mode = Mode::Major};
    const auto fs_i = build_diatonic_triad(fs_key, 1, 4, Inversion::Root).value();
    REQUIRE(format_note(fs_i.notes[0]) == "F#4");
    REQUIRE(format_note(fs_i.notes[1]) == "A#4");
    REQUIRE(format_note(fs_i.notes[2]) == "C#5");
    REQUIRE(format_chord_symbol(fs_i) == "F#");
  }

  SECTION("D minor primary cadence i - iv - V - vii°") {
    const auto d_min = KeySignature{.fifths = -1, .mode = Mode::Minor};
    const auto i_chord = build_diatonic_triad(d_min, 1, 4, Inversion::Root, true).value();
    REQUIRE(format_chord_symbol(i_chord) == "Dm");
    REQUIRE(format_note(i_chord.notes[0]) == "D4");
    REQUIRE(format_note(i_chord.notes[1]) == "F4");
    REQUIRE(format_note(i_chord.notes[2]) == "A4");

    const auto iv_chord = build_diatonic_triad(d_min, 4, 4, Inversion::Root, true).value();
    REQUIRE(format_chord_symbol(iv_chord) == "Gm");
    REQUIRE(format_note(iv_chord.notes[1]) == "Bb4");

    const auto v_chord = build_diatonic_triad(d_min, 5, 3, Inversion::Root, true).value();
    REQUIRE(format_chord_symbol(v_chord) == "A");
    REQUIRE(format_note(v_chord.notes[0]) == "A3");
    REQUIRE(format_note(v_chord.notes[1]) == "C#4");
    REQUIRE(format_note(v_chord.notes[2]) == "E4");

    const auto vii_chord = build_diatonic_triad(d_min, 7, 4, Inversion::Root, true).value();
    REQUIRE(format_chord_symbol(vii_chord) == "C#dim");
    REQUIRE(format_note(vii_chord.notes[0]) == "C#4");
    REQUIRE(format_note(vii_chord.notes[1]) == "E4");
    REQUIRE(format_note(vii_chord.notes[2]) == "G4");

    const auto b_dim =
        build_diatonic_triad(KeySignature{.fifths = 0, .mode = Mode::Major}, 7, 4).value();
    REQUIRE(format_chord_symbol(b_dim) == "Bdim");
  }
}

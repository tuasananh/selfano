#include "core/pitch.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace selfano::core;

TEST_CASE("to_midi converts standard and enharmonic spelled notes accurately", "[pitch]") {
  SECTION("Standard piano boundaries and middle C") {
    REQUIRE(to_midi(SpelledNote{NoteLetter::C, Accidental::Natural, 4}) == 60);
    REQUIRE(to_midi(SpelledNote{NoteLetter::A, Accidental::Natural, 0}) == 21);
    REQUIRE(to_midi(SpelledNote{NoteLetter::C, Accidental::Natural, 8}) == 108);
    REQUIRE(to_midi(SpelledNote{NoteLetter::C, Accidental::Natural, -1}) == 0);
    REQUIRE(to_midi(SpelledNote{NoteLetter::G, Accidental::Natural, 9}) == 127);
  }

  SECTION("Enharmonic and octave-crossing spellings") {
    REQUIRE(to_midi(SpelledNote{NoteLetter::C, Accidental::Flat, 4}) == 59);
    REQUIRE(to_midi(SpelledNote{NoteLetter::B, Accidental::Sharp, 3}) == 60);
    REQUIRE(to_midi(SpelledNote{NoteLetter::E, Accidental::Sharp, 4}) == 65);
    REQUIRE(to_midi(SpelledNote{NoteLetter::F, Accidental::Flat, 4}) == 64);
    REQUIRE(to_midi(SpelledNote{NoteLetter::F, Accidental::DoubleSharp, 4}) == 67);
    REQUIRE(to_midi(SpelledNote{NoteLetter::B, Accidental::DoubleFlat, 3}) == 57);
  }

  SECTION("Out-of-range notes return unexpected error") {
    const auto too_low =
        to_midi(SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Flat, .octave = -1});
    REQUIRE_FALSE(too_low.has_value());

    const auto too_high =
        to_midi(SpelledNote{.letter = NoteLetter::G, .accidental = Accidental::Sharp, .octave = 9});
    REQUIRE_FALSE(too_high.has_value());
  }
}

TEST_CASE("diatonic_step maps note letters and octaves consistently", "[pitch]") {
  REQUIRE(diatonic_step(NoteLetter::C, 4) == 28);
  REQUIRE(diatonic_step(NoteLetter::E, 4) == 30);
  REQUIRE(diatonic_step(NoteLetter::G, 4) == 32);
  REQUIRE(diatonic_step(NoteLetter::F, 3) == 24);
  REQUIRE(diatonic_step(NoteLetter::G, 2) == 18);
  REQUIRE(diatonic_step(NoteLetter::A, 0) == 5);
  REQUIRE(diatonic_step(NoteLetter::C, 8) == 56);

  REQUIRE(diatonic_step(SpelledNote{NoteLetter::F, Accidental::Sharp, 4}) == 31);
  REQUIRE(diatonic_step(SpelledNote{NoteLetter::G, Accidental::Flat, 4}) == 32);
}

TEST_CASE("pitch_class wraps negatives and handles double accidentals", "[pitch]") {
  REQUIRE(pitch_class(MidiPitch{60}) == 0);
  REQUIRE(pitch_class(MidiPitch{69}) == 9);
  REQUIRE(pitch_class(MidiPitch{127}) == 7);

  REQUIRE(pitch_class(SpelledNote{NoteLetter::C, Accidental::Flat, 4}) == 11);
  REQUIRE(pitch_class(SpelledNote{NoteLetter::C, Accidental::DoubleFlat, 4}) == 10);
  REQUIRE(pitch_class(SpelledNote{NoteLetter::B, Accidental::Sharp, 3}) == 0);
  REQUIRE(pitch_class(SpelledNote{NoteLetter::B, Accidental::DoubleSharp, 3}) == 1);
}

TEST_CASE("format_note and format_pitch_class produce expected ASCII strings", "[pitch]") {
  REQUIRE(format_note(SpelledNote{NoteLetter::C, Accidental::Natural, 4}) == "C4");
  REQUIRE(format_note(SpelledNote{NoteLetter::F, Accidental::Sharp, 4}) == "F#4");
  REQUIRE(format_note(SpelledNote{NoteLetter::B, Accidental::Flat, 3}) == "Bb3");
  REQUIRE(format_note(SpelledNote{NoteLetter::C, Accidental::Flat, 4}) == "Cb4");
  REQUIRE(format_note(SpelledNote{NoteLetter::F, Accidental::DoubleSharp, 4}) == "F##4");
  REQUIRE(format_note(SpelledNote{NoteLetter::E, Accidental::DoubleFlat, 5}) == "Ebb5");

  REQUIRE(format_pitch_class(NoteLetter::D, Accidental::Natural) == "D");
  REQUIRE(format_pitch_class(NoteLetter::A, Accidental::Flat) == "Ab");
}

TEST_CASE("normalize_midi_set sorts and deduplicates MIDI pitches", "[pitch]") {
  REQUIRE(normalize_midi_set(std::array<MidiPitch, 0>{}).empty());
  REQUIRE(normalize_midi_set(std::array<MidiPitch, 5>{67, 60, 64, 60, 67}) ==
          std::vector<MidiPitch>{60, 64, 67});
}

#include "core/key_signature.hpp"

#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace selfano::core;

TEST_CASE("make_key_signature validates fifths range -7..+7", "[key_signature]") {
  REQUIRE(make_key_signature(0, Mode::Major).has_value());
  REQUIRE(make_key_signature(-7, Mode::Minor).has_value());
  REQUIRE(make_key_signature(7, Mode::Major).has_value());
  REQUIRE_FALSE(make_key_signature(-8, Mode::Major).has_value());
  REQUIRE_FALSE(make_key_signature(8, Mode::Minor).has_value());
}

TEST_CASE("All 30 major and minor keys have accurate tonics, names, and accidentals",
          "[key_signature]") {
  const auto c_maj = make_key_signature(0, Mode::Major).value();
  REQUIRE(format_key_name(c_maj) == "C Major");
  REQUIRE(tonic_of(c_maj) == TonicPitchClass{NoteLetter::C, Accidental::Natural});
  REQUIRE(relative_key(c_maj) == KeySignature{0, Mode::Minor});
  REQUIRE(format_key_name(relative_key(c_maj)) == "A Minor");

  const auto g_maj = make_key_signature(1, Mode::Major).value();
  REQUIRE(format_key_name(g_maj) == "G Major");
  REQUIRE(key_accidental_for(g_maj, NoteLetter::F) == Accidental::Sharp);
  REQUIRE(key_accidental_for(g_maj, NoteLetter::C) == Accidental::Natural);

  const auto bb_maj = make_key_signature(-2, Mode::Major).value();
  REQUIRE(format_key_name(bb_maj) == "Bb Major");
  REQUIRE(key_accidental_for(bb_maj, NoteLetter::B) == Accidental::Flat);
  REQUIRE(key_accidental_for(bb_maj, NoteLetter::E) == Accidental::Flat);
  REQUIRE(key_accidental_for(bb_maj, NoteLetter::A) == Accidental::Natural);

  const auto fs_maj = make_key_signature(6, Mode::Major).value();
  REQUIRE(format_key_name(fs_maj) == "F# Major");
  REQUIRE(key_accidental_for(fs_maj, NoteLetter::E) == Accidental::Sharp);
  REQUIRE(key_accidental_for(fs_maj, NoteLetter::B) == Accidental::Natural);

  const auto cb_maj = make_key_signature(-7, Mode::Major).value();
  REQUIRE(format_key_name(cb_maj) == "Cb Major");
  for (int i = 0; i < 7; ++i) {
    REQUIRE(key_accidental_for(cb_maj, static_cast<NoteLetter>(i)) == Accidental::Flat);
  }

  const auto gs_min = make_key_signature(5, Mode::Minor).value();
  REQUIRE(format_key_name(gs_min) == "G# Minor");
  REQUIRE(tonic_of(gs_min) == TonicPitchClass{NoteLetter::G, Accidental::Sharp});

  // Verify all 30 names are unique
  std::set<std::string> seen_names;
  for (int f = -7; f <= 7; ++f) {
    seen_names.insert(format_key_name(KeySignature{.fifths = f, .mode = Mode::Major}));
    seen_names.insert(format_key_name(KeySignature{.fifths = f, .mode = Mode::Minor}));
  }
  REQUIRE(seen_names.size() == 30);
}

TEST_CASE("Key signature placement tables match standard engraving positions", "[key_signature]") {
  const auto seven_sharps = make_key_signature(7, Mode::Major).value();
  const auto seven_flats = make_key_signature(-7, Mode::Major).value();

  SECTION("Treble 7 sharps: F5 C5 G5 D5 A4 E5 B4") {
    const auto layout = key_signature_accidentals(seven_sharps, Clef::Treble);
    REQUIRE(layout.count == 7);
    const std::array<int, 7> expected{38, 35, 39, 36, 33, 37, 34};
    for (std::size_t i = 0; i < 7; ++i) {
      REQUIRE(layout.items[i].step == expected[i]);
      REQUIRE(layout.items[i].accidental == Accidental::Sharp);
    }
  }

  SECTION("Treble 7 flats: B4 E5 A4 D5 G4 C5 F4") {
    const auto layout = key_signature_accidentals(seven_flats, Clef::Treble);
    REQUIRE(layout.count == 7);
    const std::array<int, 7> expected{34, 37, 33, 36, 32, 35, 31};
    for (std::size_t i = 0; i < 7; ++i) {
      REQUIRE(layout.items[i].step == expected[i]);
      REQUIRE(layout.items[i].accidental == Accidental::Flat);
    }
  }

  SECTION("Bass 7 sharps: F3 C3 G3 D3 A2 E3 B2") {
    const auto layout = key_signature_accidentals(seven_sharps, Clef::Bass);
    REQUIRE(layout.count == 7);
    const std::array<int, 7> expected{24, 21, 25, 22, 19, 23, 20};
    for (std::size_t i = 0; i < 7; ++i) {
      REQUIRE(layout.items[i].step == expected[i]);
      REQUIRE(layout.items[i].accidental == Accidental::Sharp);
    }
  }

  SECTION("Bass 7 flats: B2 E3 A2 D3 G2 C3 F2") {
    const auto layout = key_signature_accidentals(seven_flats, Clef::Bass);
    REQUIRE(layout.count == 7);
    const std::array<int, 7> expected{20, 23, 19, 22, 18, 21, 17};
    for (std::size_t i = 0; i < 7; ++i) {
      REQUIRE(layout.items[i].step == expected[i]);
      REQUIRE(layout.items[i].accidental == Accidental::Flat);
    }
  }
}

TEST_CASE("spell_midi_in_key handles enharmonics, leading tones, and 128x30 round-trips",
          "[key_signature]") {
  SECTION("Exact round-trip for every MIDI note 0..127 across all 30 keys") {
    for (int f = -7; f <= 7; ++f) {
      for (const Mode mode : {Mode::Major, Mode::Minor}) {
        const KeySignature key{.fifths = f, .mode = mode};
        for (int m = 0; m <= 127; ++m) {
          const auto spelled = spell_midi_in_key(static_cast<MidiPitch>(m), key);
          REQUIRE(spelled.has_value());
          const auto round_trip = to_midi(*spelled);
          REQUIRE(round_trip.has_value());
          REQUIRE(*round_trip == m);
        }
      }
    }
  }

  SECTION("Key-specific enharmonic spellings") {
    REQUIRE(spell_midi_in_key(66, KeySignature{1, Mode::Major}).value() ==
            SpelledNote{NoteLetter::F, Accidental::Sharp, 4}); // G major
    REQUIRE(spell_midi_in_key(66, KeySignature{-5, Mode::Major}).value() ==
            SpelledNote{NoteLetter::G, Accidental::Flat, 4}); // Db major
    REQUIRE(spell_midi_in_key(66, KeySignature{-5, Mode::Minor}).value() ==
            SpelledNote{NoteLetter::G, Accidental::Flat, 4}); // Bb minor
    REQUIRE(spell_midi_in_key(59, KeySignature{-6, Mode::Major}).value() ==
            SpelledNote{NoteLetter::C, Accidental::Flat, 4}); // Gb major
    REQUIRE(spell_midi_in_key(60, KeySignature{7, Mode::Major}).value() ==
            SpelledNote{NoteLetter::B, Accidental::Sharp, 3}); // C# major
    REQUIRE(spell_midi_in_key(65, KeySignature{6, Mode::Major}).value() ==
            SpelledNote{NoteLetter::E, Accidental::Sharp, 4}); // F# major
  }

  SECTION("Harmonic minor leading tones") {
    REQUIRE(spell_midi_in_key(61, KeySignature{-1, Mode::Minor}).value() ==
            SpelledNote{NoteLetter::C, Accidental::Sharp, 4}); // D minor -> C#4
    REQUIRE(spell_midi_in_key(68, KeySignature{0, Mode::Minor}).value() ==
            SpelledNote{NoteLetter::G, Accidental::Sharp, 4}); // A minor -> G#4
    REQUIRE(spell_midi_in_key(66, KeySignature{-2, Mode::Minor}).value() ==
            SpelledNote{NoteLetter::F, Accidental::Sharp, 4}); // G minor -> F#4
    REQUIRE(spell_midi_in_key(67, KeySignature{5, Mode::Minor}).value() ==
            SpelledNote{NoteLetter::F, Accidental::DoubleSharp, 4}); // G# minor -> F##4
  }
}

TEST_CASE("format_midi_list spells and joins MIDI pitches in key", "[key_signature]") {
  const auto g_maj = make_key_signature(1, Mode::Major).value();
  const std::array<MidiPitch, 3> chord{62, 66, 69};
  REQUIRE(format_midi_list(chord, g_maj) == "D4 + F#4 + A4");
  REQUIRE(format_midi_list(chord, g_maj, "+") == "D4+F#4+A4");
  REQUIRE(format_midi_list(std::span<const MidiPitch>{}, g_maj).empty());
}

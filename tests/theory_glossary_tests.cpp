#include "core/theory_glossary.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace selfano::core;

TEST_CASE(
    "explain_harmony decodes diatonic triads, inversions, cadential 6/4, and secondary chords",
    "[theory_glossary]") {
  const KeySignature c_maj{.fifths = 0, .mode = Mode::Major};
  const KeySignature g_min{.fifths = -2, .mode = Mode::Minor};
  const KeySignature e_min{.fifths = 1, .mode = Mode::Minor};

  SECTION("Diatonic functions and figured-bass inversions") {
    const auto i = explain_harmony("I", "C", c_maj);
    REQUIRE(i.function == HarmonicFunction::Tonic);
    REQUIRE(i.function_label == "Tonic");
    REQUIRE(i.spelled_notes == "C - E - G");
    REQUIRE_FALSE(i.preview_notes.empty());

    const auto i6 = explain_harmony("I6", "C/E", c_maj);
    REQUIRE(i6.function == HarmonicFunction::Tonic);
    REQUIRE(i6.title.find("First-Inversion") != std::string::npos);
    REQUIRE(i6.spelled_notes.find("bass: E") != std::string::npos);

    const auto iv = explain_harmony("IV", "F", c_maj);
    REQUIRE(iv.function == HarmonicFunction::Predominant);
    REQUIRE(iv.function_label == "Predominant");

    const auto v65 = explain_harmony("V6/5", "G7/B", c_maj);
    REQUIRE(v65.function == HarmonicFunction::Dominant);
    REQUIRE(v65.title.find("First-Inversion") != std::string::npos);
    REQUIRE(v65.explanation.find("6/5") != std::string::npos);
    REQUIRE(v65.resolution_caption == "G7/B -> C");

    const auto v43 = explain_harmony("V4/3", "D7/A", g_min);
    REQUIRE(v43.function == HarmonicFunction::Dominant);
    REQUIRE(v43.title.find("Second-Inversion") != std::string::npos);

    const auto v42 = explain_harmony("V4/2", "D7/C", g_min);
    REQUIRE(v42.function == HarmonicFunction::Dominant);
    REQUIRE(v42.title.find("Third-Inversion") != std::string::npos);
  }

  SECTION("Cadential six-four and Pedal six-four") {
    const auto cad64 = explain_harmony("I6/4", "C/G", c_maj);
    REQUIRE(cad64.function == HarmonicFunction::Dominant);
    REQUIRE(cad64.title.find("Cadential Six-Four") != std::string::npos);
    REQUIRE(cad64.resolution_caption == "C/G -> G7 -> C");

    const auto ped64 = explain_harmony("IV6/4", "F/C", c_maj);
    REQUIRE(ped64.function == HarmonicFunction::Predominant);
    REQUIRE(ped64.title.find("Pedal Six-Four") != std::string::npos);
  }

  SECTION("Secondary dominants, diminished pivots, and double-slash inversions") {
    const auto v7v = explain_harmony("V7/V", "D7", c_maj);
    REQUIRE(v7v.function == HarmonicFunction::Chromatic);
    REQUIRE(v7v.title.find("Secondary Dominant of V") != std::string::npos);
    REQUIRE(v7v.spelled_notes == "D - F# - A - C");
    REQUIRE(v7v.resolution_caption == "D7 -> G -> C");

    const auto viio7v = explain_harmony("viio7/V", "F#dim7", g_min);
    REQUIRE(viio7v.function == HarmonicFunction::Chromatic);
    REQUIRE(viio7v.title.find("Leading-Tone of V") != std::string::npos);
    REQUIRE(viio7v.resolution_caption == "F#dim7 -> D -> Gm");

    const auto v42_iv = explain_harmony("V4/2/iv", "E7/D", e_min);
    REQUIRE(v42_iv.function == HarmonicFunction::Chromatic);
    REQUIRE(v42_iv.title.find("Secondary Dominant of iv") != std::string::npos);
    REQUIRE(v42_iv.explanation.find("4/2") != std::string::npos);
    REQUIRE(v42_iv.resolution_caption == "E7/D -> Am");

    const auto ii65_v = explain_harmony("ii6/5/V", "F#m7b5", e_min);
    REQUIRE(ii65_v.function == HarmonicFunction::Chromatic);
    REQUIRE(ii65_v.title.find("Applied Harmony to V") != std::string::npos);
    REQUIRE(ii65_v.resolution_caption == "F#m7b5 -> B -> Em");
  }

  SECTION("Neapolitan sixth bII6") {
    const auto b2_6 = explain_harmony("bII6", "Ab/C", g_min);
    REQUIRE(b2_6.function == HarmonicFunction::Chromatic);
    REQUIRE(b2_6.function_label == "Chromatic");
    REQUIRE(b2_6.title.find("Neapolitan Sixth") != std::string::npos);
    REQUIRE(b2_6.spelled_notes == "Ab - C - Eb (bass: C)");
    REQUIRE(b2_6.resolution_caption == "Ab/C -> D7 -> Gm");
    REQUIRE_FALSE(b2_6.preview_notes.empty());
  }
}

TEST_CASE("build_chord_sequence_preview builds playable voice-led audio notes",
          "[theory_glossary]") {
  const KeySignature c_maj{.fifths = 0, .mode = Mode::Major};
  const std::vector<std::string> prog{"C", "D7", "G7", "C"};
  const auto notes = build_chord_sequence_preview(prog, c_maj, 700.0);
  REQUIRE(notes.size() >= 12U);

  double prev_start = -1.0;
  for (const auto& n : notes) {
    REQUIRE(n.midi >= 36U);
    REQUIRE(n.midi <= 96U);
    REQUIRE(n.duration_ms > 0.0);
    REQUIRE(n.start_ms >= prev_start);
    prev_start = n.start_ms;
  }
}

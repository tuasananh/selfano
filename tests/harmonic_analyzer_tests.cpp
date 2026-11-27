#include "core/chord_symbol.hpp"
#include "core/harmonic_analyzer.hpp"
#include "core/lesson_score.hpp"
#include "core/theory_glossary.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace selfano::core;

LessonNote make_ln(NoteLetter l, Accidental a, int oct) {
  const SpelledNote sp{.letter = l, .accidental = a, .octave = oct};
  return LessonNote{.spelled = sp, .midi = to_midi(sp).value()};
}

LessonScore load_curriculum_lesson(std::string_view id) {
  const std::filesystem::path path =
      std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) / "lessons" / (std::string(id) + ".json");
  std::ifstream in(path);
  REQUIRE(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  const auto parsed = lesson_score_from_json(ss.str());
  REQUIRE(parsed.has_value());
  return *parsed;
}

} // namespace

TEST_CASE("analyze_score_harmony detects C major I-IV-V7-I despite passing tones",
          "[harmonic_analyzer][phase27]") {
  LessonScore score{};
  score.meta.id = "p27_cmaj";
  score.meta.title = "C Major";
  score.meta.key = KeySignature{.fifths = 0, .mode = Mode::Major};

  auto whole_bass = [](NoteLetter l, int oct) {
    return LessonEvent{.start_tick = 0,
                       .duration = {.base = NoteBaseDuration::Whole},
                       .notes = {make_ln(l, Accidental::Natural, oct)}};
  };

  // Bar 1: C major with a 16th passing D5
  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.bass_events = {whole_bass(NoteLetter::C, 3)};
  m1.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half},
       .notes = {make_ln(NoteLetter::E, Accidental::Natural, 4),
                 make_ln(NoteLetter::G, Accidental::Natural, 4)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Sixteenth},
       .notes = {make_ln(NoteLetter::D, Accidental::Natural, 5)}},
      {.start_tick = 1080,
       .duration = {.base = NoteBaseDuration::Quarter, .dotted = true},
       .notes = {make_ln(NoteLetter::C, Accidental::Natural, 5)}},
      {.start_tick = 1800,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_ln(NoteLetter::E, Accidental::Natural, 5)}},
  };

  // Bar 2: F major
  LessonMeasure m2{};
  m2.bar_number = 2;
  m2.bass_events = {whole_bass(NoteLetter::F, 3)};
  m2.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole},
       .notes = {make_ln(NoteLetter::A, Accidental::Natural, 4),
                 make_ln(NoteLetter::C, Accidental::Natural, 5)}},
  };

  // Bar 3: G7
  LessonMeasure m3{};
  m3.bar_number = 3;
  m3.bass_events = {whole_bass(NoteLetter::G, 2)};
  m3.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole},
       .notes = {make_ln(NoteLetter::B, Accidental::Natural, 4),
                 make_ln(NoteLetter::D, Accidental::Natural, 5),
                 make_ln(NoteLetter::F, Accidental::Natural, 5)}},
  };

  // Bar 4: C major
  LessonMeasure m4 = m1;
  m4.bar_number = 4;

  score.measures = {m1, m2, m3, m4};
  const auto out = analyze_score_harmony(score);
  REQUIRE(out.size() == 4U);
  REQUIRE_FALSE(out[0].empty());
  REQUIRE_FALSE(out[1].empty());
  REQUIRE_FALSE(out[2].empty());
  REQUIRE_FALSE(out[3].empty());
  CHECK(out[0][0].chord_symbol == "C");
  CHECK(out[0][0].roman_numeral == "I");
  CHECK(out[1][0].chord_symbol == "F");
  CHECK(out[1][0].roman_numeral == "IV");
  CHECK(out[2][0].chord_symbol == "G7");
  CHECK(out[2][0].roman_numeral == "V7");
  CHECK(out[3][0].chord_symbol == "C");
}

TEST_CASE("analyze_score_harmony detects secondary dominant V7/V and Neapolitan bII6",
          "[harmonic_analyzer][phase27]") {
  LessonScore score{};
  score.meta.id = "p27_neap";
  score.meta.title = "Neapolitan";
  score.meta.key = KeySignature{.fifths = -2, .mode = Mode::Minor};

  // Bar 1: Ab/C (bII6 in G minor) -> Bar 2: D7 (V7) -> Bar 3: Gm (i)
  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.bass_events = {{.start_tick = 0,
                     .duration = {.base = NoteBaseDuration::Whole},
                     .notes = {make_ln(NoteLetter::C, Accidental::Natural, 3)}}};
  m1.treble_events = {{.start_tick = 0,
                       .duration = {.base = NoteBaseDuration::Whole},
                       .notes = {make_ln(NoteLetter::E, Accidental::Flat, 4),
                                 make_ln(NoteLetter::A, Accidental::Flat, 4)}}};

  LessonMeasure m2{};
  m2.bar_number = 2;
  m2.bass_events = {{.start_tick = 0,
                     .duration = {.base = NoteBaseDuration::Whole},
                     .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3)}}};
  m2.treble_events = {{.start_tick = 0,
                       .duration = {.base = NoteBaseDuration::Whole},
                       .notes = {make_ln(NoteLetter::F, Accidental::Sharp, 4),
                                 make_ln(NoteLetter::A, Accidental::Natural, 4),
                                 make_ln(NoteLetter::C, Accidental::Natural, 5)}}};

  LessonMeasure m3{};
  m3.bar_number = 3;
  m3.bass_events = {{.start_tick = 0,
                     .duration = {.base = NoteBaseDuration::Whole},
                     .notes = {make_ln(NoteLetter::G, Accidental::Natural, 2)}}};
  m3.treble_events = {{.start_tick = 0,
                       .duration = {.base = NoteBaseDuration::Whole},
                       .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4),
                                 make_ln(NoteLetter::D, Accidental::Natural, 5)}}};

  score.measures = {m1, m2, m3};
  const auto out = analyze_score_harmony(score);
  REQUIRE(out.size() == 3U);
  REQUIRE_FALSE(out[0].empty());
  CHECK(out[0][0].chord_symbol == "Ab/C");
  CHECK(out[0][0].roman_numeral == "bII6");
  CHECK(out[1][0].chord_symbol == "D7");
  CHECK(out[1][0].roman_numeral == "V7");
  CHECK(out[2][0].chord_symbol == "Gm");
  CHECK(out[2][0].roman_numeral == "i");
}

TEST_CASE("analyze_score_harmony achieves >=80% root/quality accuracy on stripped curriculum "
          "scores and emits parseable, explainable symbols",
          "[harmonic_analyzer][phase27]") {
  static constexpr std::array<std::string_view, 7> kLessonIds{
      "t1_01", "t2_01", "t3_01", "t5_02", "t5_04", "t6_03", "t6_04",
  };

  int total_bars = 0;
  int matched_bars = 0;
  bool saw_t6_03_bii6 = false;

  for (const auto lid : kLessonIds) {
    LessonScore truth = load_curriculum_lesson(lid);
    LessonScore stripped = truth;
    for (auto& m : stripped.measures) {
      m.harmony.clear();
    }

    const auto pred = analyze_score_harmony(stripped);
    REQUIRE(pred.size() == truth.measures.size());

    KeySignature active_key = truth.meta.key;
    for (std::size_t bi = 0; bi < truth.measures.size(); ++bi) {
      if (truth.measures[bi].key_change.has_value()) {
        active_key = *truth.measures[bi].key_change;
      }
      for (const auto& h : pred[bi]) {
        const auto parsed_pred = parse_chord_symbol(h.chord_symbol);
        INFO("Lesson " << lid << " bar " << bi << " symbol: " << h.chord_symbol);
        REQUIRE(parsed_pred.has_value());
        const auto expl = explain_harmony(h.roman_numeral, h.chord_symbol, active_key);
        CHECK_FALSE(expl.explanation.empty());
      }

      if (truth.measures[bi].harmony.empty()) {
        continue;
      }
      ++total_bars;
      if (pred[bi].empty()) {
        continue;
      }
      const auto& truth_h = truth.measures[bi].harmony.front();
      const auto& pred_h = pred[bi].front();
      const auto tp = parse_chord_symbol(truth_h.chord_symbol);
      const auto pp = parse_chord_symbol(pred_h.chord_symbol);
      REQUIRE(tp.has_value());
      REQUIRE(pp.has_value());

      auto same_family = [](ChordQuality a, ChordQuality b) {
        auto fam = [](ChordQuality q) {
          switch (q) {
          case ChordQuality::Minor:
          case ChordQuality::Minor7:
          case ChordQuality::Minor6:
          case ChordQuality::MinorAdd9:
          case ChordQuality::Minor9:
          case ChordQuality::MinorMajor7:
            return 1;
          case ChordQuality::Diminished:
          case ChordQuality::Diminished7:
          case ChordQuality::HalfDiminished7:
            return 2;
          default:
            return 0;
          }
        };
        return fam(a) == fam(b);
      };

      if (tp->root_pc == pp->root_pc && same_family(tp->quality, pp->quality)) {
        ++matched_bars;
      }
      if (lid == "t6_03" && bi == 0U && pred_h.roman_numeral == "bII6") {
        saw_t6_03_bii6 = true;
      }
    }
  }

  REQUIRE(total_bars > 0);
  const double accuracy = static_cast<double>(matched_bars) / static_cast<double>(total_bars);
  INFO("Curriculum bar-level accuracy: " << matched_bars << "/" << total_bars << " ("
                                         << (accuracy * 100.0) << "%)");
  CHECK(accuracy >= 0.80);
  CHECK(saw_t6_03_bii6);
}

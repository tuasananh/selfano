#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using namespace selfano::core;

namespace {

[[nodiscard]] LessonNote make_ln(NoteLetter letter, Accidental acc, int octave,
                                 std::uint8_t finger = 0, bool tie_start = false,
                                 bool tie_stop = false) {
  const SpelledNote sp{.letter = letter, .accidental = acc, .octave = octave};
  return LessonNote{
      .spelled = sp,
      .midi = to_midi(sp).value(),
      .finger = finger,
      .tie_start = tie_start,
      .tie_stop = tie_stop,
  };
}

[[nodiscard]] LessonScore make_valid_two_bar_score() {
  LessonScore score{};
  score.meta = LessonMetadata{
      .id = "test_01",
      .tier = 1,
      .order_in_tier = 1,
      .title = "Test Two-Bar Study",
      .composer_source = "Selfano Test",
      .key = KeySignature{.fifths = -2, .mode = Mode::Minor},
      .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
      .target_bpm = 80.0,
      .theory = {},
  };
  score.meta.theory.summary = "G minor tonic pedal";
  score.meta.theory.pattern_tip = "Read fifths in LH";
  score.meta.theory.technique_tip = "Drop wrist on beat 1";
  score.meta.theory.ballade_link = "Shared G minor home key";

  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.treble_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = true},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 4, 1)}},
      LessonEvent{.start_tick = 1440,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4, 3, true, false)}},
  };
  m1.bass_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 3, 5)}},
  };
  m1.harmony = {
      HarmonicAnnotation{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"},
  };

  LessonMeasure m2{};
  m2.bar_number = 2;
  m2.treble_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4, 3, false, true)}},
      LessonEvent{.start_tick = 960,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::A, Accidental::Natural, 4, 2)}},
      LessonEvent{.start_tick = 1440,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                  .is_rest = true,
                  .voice = 0,
                  .notes = {}},
  };
  m2.bass_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3, 2)}},
      LessonEvent{.start_tick = 960,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3, 2)}},
  };
  m2.harmony = {
      HarmonicAnnotation{.start_tick = 0, .chord_symbol = "D7", .roman_numeral = "V7"},
  };

  score.measures = {m1, m2};
  score.phrases = {LessonPhrase{.label = "Phrase A (Bars 1-2)", .start_bar = 0, .end_bar = 1}};
  return score;
}

} // namespace

TEST_CASE("TimeSignature and RhythmicValue compute exact rational ticks across all 5 meters",
          "[lesson_score]") {
  const TimeSignature m44{.numerator = 4, .denominator = 4};
  REQUIRE(m44.measure_ticks() == 1920);
  REQUIRE(m44.beat_ticks() == 480);
  REQUIRE(m44.pulse_count() == 4);
  REQUIRE_FALSE(m44.is_compound());

  const TimeSignature m34{.numerator = 3, .denominator = 4};
  REQUIRE(m34.measure_ticks() == 1440);
  REQUIRE(m34.beat_ticks() == 480);
  REQUIRE(m34.pulse_count() == 3);
  REQUIRE_FALSE(m34.is_compound());

  const TimeSignature m24{.numerator = 2, .denominator = 4};
  REQUIRE(m24.measure_ticks() == 960);
  REQUIRE(m24.beat_ticks() == 480);
  REQUIRE(m24.pulse_count() == 2);
  REQUIRE_FALSE(m24.is_compound());

  const TimeSignature m68{.numerator = 6, .denominator = 8};
  REQUIRE(m68.measure_ticks() == 1440);
  REQUIRE(m68.beat_ticks() == 720);
  REQUIRE(m68.pulse_count() == 2);
  REQUIRE(m68.is_compound());

  const TimeSignature m64{.numerator = 6, .denominator = 4};
  REQUIRE(m64.measure_ticks() == 2880);
  REQUIRE(m64.beat_ticks() == 480);
  REQUIRE(m64.pulse_count() == 6);
  REQUIRE_FALSE(m64.is_compound());

  REQUIRE(RhythmicValue{NoteBaseDuration::Sixteenth, false}.ticks() == 120);
  REQUIRE(RhythmicValue{NoteBaseDuration::Sixteenth, true}.ticks() == 180);
  REQUIRE(RhythmicValue{NoteBaseDuration::Eighth, false}.ticks() == 240);
  REQUIRE(RhythmicValue{NoteBaseDuration::Eighth, true}.ticks() == 360);
  REQUIRE(RhythmicValue{NoteBaseDuration::Quarter, false}.ticks() == 480);
  REQUIRE(RhythmicValue{NoteBaseDuration::Quarter, true}.ticks() == 720);
  REQUIRE(RhythmicValue{NoteBaseDuration::Half, false}.ticks() == 960);
  REQUIRE(RhythmicValue{NoteBaseDuration::Half, true}.ticks() == 1440);
  REQUIRE(RhythmicValue{NoteBaseDuration::Whole, false}.ticks() == 1920);
  REQUIRE(RhythmicValue{NoteBaseDuration::Whole, true}.ticks() == 2880);

  REQUIRE(quantize_ticks_to_rhythmic_value(720) == RhythmicValue{NoteBaseDuration::Quarter, true});
  REQUIRE(quantize_ticks_to_rhythmic_value(360) == RhythmicValue{NoteBaseDuration::Eighth, true});
  REQUIRE_FALSE(quantize_ticks_to_rhythmic_value(133).has_value());
}

TEST_CASE("parse_spelled_note handles natural, sharp, flat, and double accidentals",
          "[lesson_score]") {
  const auto c4 = parse_spelled_note("C4");
  REQUIRE(c4.has_value());
  REQUIRE(*c4 == SpelledNote{NoteLetter::C, Accidental::Natural, 4});

  const auto fs5 = parse_spelled_note("F#5");
  REQUIRE(fs5.has_value());
  REQUIRE(*fs5 == SpelledNote{NoteLetter::F, Accidental::Sharp, 5});

  const auto bb3 = parse_spelled_note("Bb3");
  REQUIRE(bb3.has_value());
  REQUIRE(*bb3 == SpelledNote{NoteLetter::B, Accidental::Flat, 3});

  const auto cx4 = parse_spelled_note("C##4");
  REQUIRE(cx4.has_value());
  REQUIRE(*cx4 == SpelledNote{NoteLetter::C, Accidental::DoubleSharp, 4});

  const auto ebb3 = parse_spelled_note("Ebb3");
  REQUIRE(ebb3.has_value());
  REQUIRE(*ebb3 == SpelledNote{NoteLetter::E, Accidental::DoubleFlat, 3});

  REQUIRE_FALSE(parse_spelled_note("H4").has_value());
  REQUIRE_FALSE(parse_spelled_note("C").has_value());
  REQUIRE_FALSE(parse_spelled_note("").has_value());
}

TEST_CASE("validate_lesson_score checks tick sums, midi consistency, ties, fingers, and chords",
          "[lesson_score]") {
  const auto valid = make_valid_two_bar_score();
  REQUIRE(validate_lesson_score(valid).has_value());

  SECTION("Rejects incomplete primary voice bar ticks") {
    auto bad = valid;
    bad.measures[0].treble_events.pop_back();
    const auto res = validate_lesson_score(bad);
    REQUIRE_FALSE(res.has_value());
    REQUIRE_FALSE(res.error().empty());
  }

  SECTION("Rejects midi mismatch with spelled note") {
    auto bad = valid;
    bad.measures[0].treble_events[0].notes[0].midi = 60;
    REQUIRE_FALSE(validate_lesson_score(bad).has_value());
  }

  SECTION("Rejects out-of-range fingering") {
    auto bad = valid;
    bad.measures[0].treble_events[0].notes[0].finger = 6;
    REQUIRE_FALSE(validate_lesson_score(bad).has_value());
  }

  SECTION("Rejects dangling tie_start without matching tie_stop") {
    auto bad = valid;
    bad.measures[1].treble_events[0].notes[0].tie_stop = false;
    REQUIRE_FALSE(validate_lesson_score(bad).has_value());
  }

  SECTION("Rejects unparseable chord symbol") {
    auto bad = valid;
    bad.measures[0].harmony[0].chord_symbol = "Z##mega99";
    REQUIRE_FALSE(validate_lesson_score(bad).has_value());
  }

  SECTION("Respects mid-piece meter_change") {
    auto meter_shift = valid;
    meter_shift.measures[1].meter_change = TimeSignature{.numerator = 3, .denominator = 4};
    meter_shift.measures[1].treble_events = {
        LessonEvent{.start_tick = 0,
                    .duration = {.base = NoteBaseDuration::Half, .dotted = false},
                    .is_rest = false,
                    .voice = 0,
                    .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4, 3, false, true)}},
        LessonEvent{.start_tick = 960,
                    .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                    .is_rest = false,
                    .voice = 0,
                    .notes = {make_ln(NoteLetter::A, Accidental::Natural, 4, 2)}},
    };
    meter_shift.measures[1].bass_events = {
        LessonEvent{.start_tick = 0,
                    .duration = {.base = NoteBaseDuration::Half, .dotted = true},
                    .is_rest = false,
                    .voice = 0,
                    .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3, 2)}},
    };
    REQUIRE(validate_lesson_score(meter_shift).has_value());
  }
}

TEST_CASE("auto_partition_phrases slices 4-bar and 8-bar chunks cleanly", "[lesson_score]") {
  const auto p8 = auto_partition_phrases(8);
  REQUIRE(p8.size() == 2);
  REQUIRE(p8[0].start_bar == 0);
  REQUIRE(p8[0].end_bar == 3);
  REQUIRE(p8[1].start_bar == 4);
  REQUIRE(p8[1].end_bar == 7);

  // 13 bars -> fold single trailing bar into Phrase C (Bars 9-13)
  const auto p13 = auto_partition_phrases(13);
  REQUIRE(p13.size() == 3);
  REQUIRE(p13[2].start_bar == 8);
  REQUIRE(p13[2].end_bar == 12);

  // 32 bars (> 24) -> 8-bar chunks
  const auto p32 = auto_partition_phrases(32);
  REQUIRE(p32.size() == 4);
  REQUIRE(p32[0].start_bar == 0);
  REQUIRE(p32[0].end_bar == 7);
  REQUIRE(p32[3].start_bar == 24);
  REQUIRE(p32[3].end_bar == 31);
}

TEST_CASE("lesson_score_to_json and lesson_score_from_json round-trip cleanly", "[lesson_score]") {
  const auto original = make_valid_two_bar_score();
  const std::string json_str = lesson_score_to_json(original);
  const auto parsed = lesson_score_from_json(json_str);
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == original);

  REQUIRE_FALSE(lesson_score_from_json("{invalid json").has_value());

  SECTION("Extended TheoryLens fields round-trip and stay backward-compatible") {
    LessonScore rich = original;
    rich.meta.theory.concept_title = "Tonic Pedal vs. Dominant Tension";
    rich.meta.theory.concept_paragraphs = {
        "Paragraph one explains the pedal tone in simple language.",
        "Paragraph two connects the harmony to the left hand shape.",
    };
    rich.meta.theory.form_map = {
        PhraseFormNote{.phrase_label = "Phrase A", .role = "Opening question settling on V7"},
    };
    rich.meta.theory.practice_steps = {
        "1. Play RH melody alone.",
        "2. Add LH pedal on beat 1.",
    };
    rich.meta.theory.audio_examples = {
        TheoryAudioExample{.label = "Tonic vs. Dominant",
                           .caption = "Gm -> D7 -> Gm",
                           .chords = {"Gm", "D7", "Gm"}},
    };

    const std::string rich_json = lesson_score_to_json(rich);
    const auto reparsed = lesson_score_from_json(rich_json);
    REQUIRE(reparsed.has_value());
    REQUIRE(*reparsed == rich);
    REQUIRE(reparsed->meta.theory.concept_title == "Tonic Pedal vs. Dominant Tension");
    REQUIRE(reparsed->meta.theory.form_map.size() == 1U);
    REQUIRE(reparsed->meta.theory.audio_examples[0].chords.size() == 3U);
  }
}

TEST_CASE("split_ticks_to_values greedily decomposes durations into rhythmic values",
          "[lesson_score]") {
  const auto bar_4_4 = split_ticks_to_values(1920);
  REQUIRE(bar_4_4.size() == 1);
  REQUIRE(bar_4_4[0] == RhythmicValue{.base = NoteBaseDuration::Whole, .dotted = false});

  const auto bar_5_4 = split_ticks_to_values(2400);
  REQUIRE(bar_5_4.size() == 2);
  REQUIRE(bar_5_4[0] == RhythmicValue{.base = NoteBaseDuration::Whole, .dotted = false});
  REQUIRE(bar_5_4[1] == RhythmicValue{.base = NoteBaseDuration::Quarter, .dotted = false});

  REQUIRE(split_ticks_to_values(0).empty());
  REQUIRE(split_ticks_to_values(30).empty());
  const auto thirty_second = split_ticks_to_values(60);
  REQUIRE(thirty_second.size() == 1);
  REQUIRE(thirty_second[0] ==
          RhythmicValue{.base = NoteBaseDuration::ThirtySecond, .dotted = false});
}

TEST_CASE("LessonScore supports anacrusis pickup measures with backward-compatible JSON",
          "[lesson_score]") {
  LessonScore s = make_valid_two_bar_score();
  s.meta.time_signature = TimeSignature{.numerator = 3, .denominator = 4};
  s.measures.clear();

  LessonMeasure pickup{};
  pickup.bar_number = 0;
  pickup.pickup_ticks = 480;
  pickup.treble_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::E, Accidental::Natural, 4, 1)}},
  };
  pickup.bass_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
                  .is_rest = true,
                  .voice = 0,
                  .notes = {}},
  };

  LessonMeasure bar1{};
  bar1.bar_number = 1;
  bar1.treble_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = true},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 4, 3)}},
  };
  bar1.bass_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = true},
                  .is_rest = false,
                  .voice = 0,
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 3, 5)}},
  };
  bar1.harmony = {HarmonicAnnotation{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"}};

  s.measures = {pickup, bar1};
  s.phrases = {LessonPhrase{.label = "Phrase A", .start_bar = 0, .end_bar = 1}};

  REQUIRE(effective_measure_ticks(pickup, s.meta.time_signature) == 480);
  REQUIRE(effective_measure_ticks(bar1, s.meta.time_signature) == 1440);
  REQUIRE(validate_lesson_score(s).has_value());

  const std::string json_str = lesson_score_to_json(s);
  const auto parsed = lesson_score_from_json(json_str);
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == s);
  REQUIRE(parsed->measures[0].bar_number == 0U);
  REQUIRE(parsed->measures[0].pickup_ticks == 480);
  REQUIRE_FALSE(parsed->measures[1].pickup_ticks.has_value());

  SECTION("Rejects invalid pickup_ticks bounds or voice sums") {
    auto bad_zero = s;
    bad_zero.measures[0].pickup_ticks = 0;
    REQUIRE_FALSE(validate_lesson_score(bad_zero).has_value());

    auto bad_full = s;
    bad_full.measures[0].pickup_ticks = 1440;
    REQUIRE_FALSE(validate_lesson_score(bad_full).has_value());

    auto bad_sum = s;
    bad_sum.measures[0].pickup_ticks = 240;
    REQUIRE_FALSE(validate_lesson_score(bad_sum).has_value());
  }
}

TEST_CASE("Expressive LessonScore fields round-trip cleanly and validate bounds",
          "[lesson_score]") {
  auto score = make_valid_two_bar_score();
  score.meta.tempo_marking = "Moderato cantabile";
  score.measures[0].treble_events[0].articulations =
      articulation_bit(ArticulationFlag::Accent) | articulation_bit(ArticulationFlag::Staccato);
  score.measures[1].treble_events[1].articulations =
      articulation_bit(ArticulationFlag::Tenuto) | articulation_bit(ArticulationFlag::Arpeggiate);
  score.measures[0].dynamics = {{.start_tick = 0, .level = DynamicLevel::P}};
  score.measures[1].dynamics = {{.start_tick = 960, .level = DynamicLevel::FF}};
  score.measures[0].hairpins = {
      {.start_tick = 0, .end_tick = 1920, .kind = HairpinKind::Crescendo}};
  score.slurs = {{.start_bar = 0, .start_tick = 0, .end_bar = 1, .end_tick = 960, .treble = true}};
  score.pedals = {{.start_bar = 0, .start_tick = 0, .end_bar = 1, .end_tick = 1920}};

  REQUIRE(validate_lesson_score(score).has_value());
  const std::string json_text = lesson_score_to_json(score);
  const auto round = lesson_score_from_json(json_text);
  REQUIRE(round.has_value());
  REQUIRE(*round == score);
  REQUIRE(round->meta.tempo_marking == "Moderato cantabile");
  REQUIRE(has_articulation(round->measures[0].treble_events[0].articulations,
                           ArticulationFlag::Accent));
  REQUIRE(has_articulation(round->measures[1].treble_events[1].articulations,
                           ArticulationFlag::Arpeggiate));

  SECTION("Rejects out-of-range hairpins, slurs, and pedals") {
    auto bad_hp = score;
    bad_hp.measures[0].hairpins[0].end_tick = 4000;
    REQUIRE_FALSE(validate_lesson_score(bad_hp).has_value());

    auto bad_slur = score;
    bad_slur.slurs[0].end_bar = 5;
    REQUIRE_FALSE(validate_lesson_score(bad_slur).has_value());

    auto bad_pedal = score;
    bad_pedal.pedals[0].start_bar = 1;
    bad_pedal.pedals[0].end_bar = 0;
    REQUIRE_FALSE(validate_lesson_score(bad_pedal).has_value());
  }
}

TEST_CASE("LessonScore supports 32nd notes, double dots, and general tuplets with JSON round-trip",
          "[lesson_score][phase24]") {
  REQUIRE(RhythmicValue{NoteBaseDuration::ThirtySecond, false}.ticks() == 60);
  REQUIRE(RhythmicValue{NoteBaseDuration::ThirtySecond, true}.ticks() == 90);
  REQUIRE(RhythmicValue{.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 2}.ticks() ==
          840);
  REQUIRE(RhythmicValue{.base = NoteBaseDuration::Eighth, .dotted = true, .dots = 2}.ticks() ==
          420);
  REQUIRE(RhythmicValue{.base = NoteBaseDuration::Half, .dotted = true, .dots = 2}.ticks() == 1680);
  REQUIRE(RhythmicValue{.base = NoteBaseDuration::Sixteenth, .dotted = true, .dots = 2}.ticks() ==
          210);

  REQUIRE(quantize_ticks_to_rhythmic_value(60) ==
          RhythmicValue{.base = NoteBaseDuration::ThirtySecond, .dotted = false});
  REQUIRE(quantize_ticks_to_rhythmic_value(840) ==
          RhythmicValue{.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 2});

  LessonScore score = make_valid_two_bar_score();
  score.measures.clear();

  LessonMeasure m1{};
  m1.bar_number = 1;
  // Beat 1: 8th triplet (160 x 3 = 480)
  // Beat 2: 16th quintuplet (96 x 5 = 480)
  // Beat 3-4: double-dotted quarter (840) + two 32nds (60 + 60) = 960
  m1.treble_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Eighth},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 4, 1)},
                  .tuplet = TupletRatio{.actual = 3, .normal = 2, .group_id = 1}},
      LessonEvent{.start_tick = 160,
                  .duration = {.base = NoteBaseDuration::Eighth},
                  .notes = {make_ln(NoteLetter::A, Accidental::Natural, 4, 2)},
                  .tuplet = TupletRatio{.actual = 3, .normal = 2, .group_id = 1}},
      LessonEvent{.start_tick = 320,
                  .duration = {.base = NoteBaseDuration::Eighth},
                  .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4, 3)},
                  .tuplet = TupletRatio{.actual = 3, .normal = 2, .group_id = 1}},
      LessonEvent{.start_tick = 480,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::C, Accidental::Natural, 5, 1)},
                  .tuplet = TupletRatio{.actual = 5, .normal = 4, .group_id = 2}},
      LessonEvent{.start_tick = 576,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 5, 2)},
                  .tuplet = TupletRatio{.actual = 5, .normal = 4, .group_id = 2}},
      LessonEvent{.start_tick = 672,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::E, Accidental::Flat, 5, 3)},
                  .tuplet = TupletRatio{.actual = 5, .normal = 4, .group_id = 2}},
      LessonEvent{.start_tick = 768,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 5, 2)},
                  .tuplet = TupletRatio{.actual = 5, .normal = 4, .group_id = 2}},
      LessonEvent{.start_tick = 864,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::C, Accidental::Natural, 5, 1)},
                  .tuplet = TupletRatio{.actual = 5, .normal = 4, .group_id = 2}},
      LessonEvent{.start_tick = 960,
                  .duration = {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 2},
                  .notes = {make_ln(NoteLetter::B, Accidental::Flat, 4, 3)}},
      LessonEvent{.start_tick = 1800,
                  .duration = {.base = NoteBaseDuration::ThirtySecond},
                  .notes = {make_ln(NoteLetter::A, Accidental::Natural, 4, 2)}},
      LessonEvent{.start_tick = 1860,
                  .duration = {.base = NoteBaseDuration::ThirtySecond},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 4, 1)}},
  };
  // Bass: 16th sextuplet (80 x 6 = 480) + dotted half (1440)
  m1.bass_events = {
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 2, 5)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 80,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3, 2)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 160,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 3, 1)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 240,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::B, Accidental::Flat, 3, 2)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 320,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 3, 1)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 400,
                  .duration = {.base = NoteBaseDuration::Sixteenth},
                  .notes = {make_ln(NoteLetter::D, Accidental::Natural, 3, 2)},
                  .tuplet = TupletRatio{.actual = 6, .normal = 4, .group_id = 3}},
      LessonEvent{.start_tick = 480,
                  .duration = {.base = NoteBaseDuration::Half, .dotted = true},
                  .notes = {make_ln(NoteLetter::G, Accidental::Natural, 2, 5)}},
  };
  score.measures.push_back(m1);
  score.phrases = {LessonPhrase{.label = "Phrase A", .start_bar = 0, .end_bar = 0}};

  REQUIRE(m1.treble_events[0].event_ticks() == 160);
  REQUIRE(m1.treble_events[3].event_ticks() == 96);
  REQUIRE(m1.bass_events[0].event_ticks() == 80);
  REQUIRE(validate_lesson_score(score).has_value());

  const std::string json_str = lesson_score_to_json(score);
  const auto reparsed = lesson_score_from_json(json_str);
  REQUIRE(reparsed.has_value());
  REQUIRE(*reparsed == score);
  REQUIRE(reparsed->measures[0].treble_events[8].duration.effective_dots() == 2U);
  REQUIRE(reparsed->measures[0].treble_events[9].duration.base == NoteBaseDuration::ThirtySecond);
  REQUIRE(reparsed->measures[0].treble_events[0].tuplet ==
          TupletRatio{.actual = 3, .normal = 2, .group_id = 1});

  SECTION("Rejects invalid tuplet ratio") {
    auto bad = score;
    bad.measures[0].treble_events[0].tuplet = TupletRatio{.actual = 1, .normal = 2, .group_id = 1};
    REQUIRE_FALSE(validate_lesson_score(bad).has_value());
  }
}

TEST_CASE("LessonScore supports grace notes, mid-piece clef changes, and 8va/8vb ottava spans",
          "[lesson_score][phase25]") {
  auto score = make_valid_two_bar_score();
  // Bar 1: add a slashed grace note at tick 0 before the half note
  score.measures[0].treble_events.insert(
      score.measures[0].treble_events.begin(),
      LessonEvent{.start_tick = 0,
                  .duration = {.base = NoteBaseDuration::Eighth},
                  .notes = {make_ln(NoteLetter::F, Accidental::Sharp, 4, 2)},
                  .is_grace = true,
                  .grace_slash = true});
  // Bar 2: LH switches to Treble clef at tick 0
  score.measures[1].clef_changes = {
      ClefChange{.start_tick = 0, .clef = Clef::Treble, .treble_staff = false},
  };
  // 8va over Bar 2 treble
  score.ottavas = {
      OttavaSpan{.start_bar = 1,
                 .start_tick = 0,
                 .end_bar = 1,
                 .end_tick = 1920,
                 .treble = true,
                 .octave_shift = 1},
  };

  REQUIRE(score.measures[0].treble_events[0].event_ticks() == 0);
  REQUIRE(validate_lesson_score(score).has_value());

  const std::string json_str = lesson_score_to_json(score);
  const auto reparsed = lesson_score_from_json(json_str);
  REQUIRE(reparsed.has_value());
  REQUIRE(*reparsed == score);
  REQUIRE(reparsed->measures[0].treble_events[0].is_grace);
  REQUIRE(reparsed->measures[0].treble_events[0].grace_slash);
  REQUIRE(reparsed->measures[1].clef_changes.size() == 1U);
  REQUIRE(reparsed->ottavas.size() == 1U);
  REQUIRE(reparsed->ottavas[0].octave_shift == 1);

  SECTION("Rejects invalid ottava shift and out-of-range clef change") {
    auto bad_oct = score;
    bad_oct.ottavas[0].octave_shift = 2;
    REQUIRE_FALSE(validate_lesson_score(bad_oct).has_value());

    auto bad_clef = score;
    bad_clef.measures[1].clef_changes[0].start_tick = 2000;
    REQUIRE_FALSE(validate_lesson_score(bad_clef).has_value());
  }
}

TEST_CASE("TempoEvent round-trips in JSON and validates bounds", "[lesson_score][phase26]") {
  LessonScore score{};
  score.meta.id = "p26_tempo";
  score.meta.title = "Tempo Map";
  score.meta.target_bpm = 52.0;
  score.meta.tempo_marking = "Largo";

  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole},
       .notes = {LessonNote{.spelled = {NoteLetter::C, Accidental::Natural, 5}, .midi = 72}}}};
  m1.bass_events = {{.start_tick = 0,
                     .duration = {.base = NoteBaseDuration::Whole},
                     .is_rest = true,
                     .notes = {}}};
  LessonMeasure m2 = m1;
  m2.bar_number = 2;
  m2.tempo_changes = {{.start_tick = 0, .bpm = 104.0, .label = "Moderato"}};
  score.measures = {m1, m2};

  REQUIRE(validate_lesson_score(score).has_value());
  const auto reparsed = lesson_score_from_json(lesson_score_to_json(score));
  REQUIRE(reparsed.has_value());
  REQUIRE(*reparsed == score);

  auto bad_bpm = score;
  bad_bpm.measures[1].tempo_changes[0].bpm = 350.0;
  REQUIRE_FALSE(validate_lesson_score(bad_bpm).has_value());
}

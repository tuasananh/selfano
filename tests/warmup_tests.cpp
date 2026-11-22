#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace selfano::core;

TEST_CASE("kWeeklyKeyCycle defines classical/anime key progression and weekly rollover",
          "[warmup][weekly_key]") {
  REQUIRE(kWeeklyKeyCycle.size() == 24);

  const std::array<std::string, 14> expected_first_14{
      "C Major", "A Minor", "G Major",  "E Minor",  "D Major", "B Minor",  "F Major",
      "D Minor", "A Major", "F# Minor", "Bb Major", "G Minor", "Eb Major", "C Minor",
  };
  for (std::size_t i = 0; i < expected_first_14.size(); ++i) {
    REQUIRE(format_key_name(weekly_key_at(i)) == expected_first_14[i]);
    const auto found = find_weekly_key_index(weekly_key_at(i));
    REQUIRE(found.has_value());
    REQUIRE(*found == i);
  }

  REQUIRE(weekly_key_at(24) == weekly_key_at(0));
  REQUIRE(weekly_key_at(25) == weekly_key_at(1));
  REQUIRE_FALSE(find_weekly_key_index(KeySignature{7, Mode::Major}).has_value());

  SECTION("sync_weekly_key initializes week_start and auto-advances every 7 days") {
    WeeklyKeyState state{.key_index = 0, .week_start_epoch_day = 0, .manual_override = false};
    state = sync_weekly_key(state, 20000);
    REQUIRE(state.key_index == 0);
    REQUIRE(state.week_start_epoch_day == 20000);

    // Same week (day +6) stays on current key
    state = sync_weekly_key(state, 20006);
    REQUIRE(state.key_index == 0);
    REQUIRE(state.week_start_epoch_day == 20000);

    // Next week (day +7) advances by 1
    state = sync_weekly_key(state, 20007);
    REQUIRE(state.key_index == 1);
    REQUIRE(state.week_start_epoch_day == 20007);

    // Multi-week jump (+21 days = 3 weeks) advances by 3
    state = sync_weekly_key(state, 20028);
    REQUIRE(state.key_index == 4);
    REQUIRE(state.week_start_epoch_day == 20028);
  }

  SECTION("manual_override prevents automatic rotation") {
    WeeklyKeyState state{.key_index = 7, .week_start_epoch_day = 20000, .manual_override = true};
    state = sync_weekly_key(state, 20028);
    REQUIRE(state.key_index == 7);
    REQUIRE(state.week_start_epoch_day == 20000);
  }
}

TEST_CASE("WarmUpEngine generates 1- and 2-octave scales across all 24 keys without hand overlap",
          "[warmup]") {
  for (std::size_t k_idx = 0; k_idx < kWeeklyKeyCycle.size(); ++k_idx) {
    const auto key = weekly_key_at(k_idx);
    for (const int octaves : {1, 2}) {
      WarmUpEngine engine(WarmUpConfig{
          .key = key,
          .octaves = octaves,
          .hand = HandSelection::HandsTogether,
          .minor_mode = MinorScaleMode::Harmonic,
          .exercise = WarmUpExerciseKind::ScaleOnly,
      });

      const std::size_t expected_count = (octaves == 1) ? 15U : 29U;
      REQUIRE(engine.steps().size() == expected_count);

      for (std::size_t s = 0; s < engine.steps().size(); ++s) {
        const auto& step = engine.steps()[s];
        REQUIRE(step.notes.size() == 2);
        const auto bass_it = std::find_if(step.notes.begin(), step.notes.end(),
                                          [](const auto& n) { return n.clef == Clef::Bass; });
        const auto treble_it = std::find_if(step.notes.begin(), step.notes.end(),
                                            [](const auto& n) { return n.clef == Clef::Treble; });
        REQUIRE(bass_it != step.notes.end());
        REQUIRE(treble_it != step.notes.end());
        REQUIRE(bass_it->midi < treble_it->midi);
        REQUIRE((treble_it->midi - bass_it->midi) % 12 == 0);
        REQUIRE(step.expected_midi.size() == 2);
        REQUIRE(step.expected_midi[0] == bass_it->midi);
        REQUIRE(step.expected_midi[1] == treble_it->midi);
        REQUIRE_FALSE(step.is_cadence);
        REQUIRE(step.is_descending == (s > static_cast<std::size_t>(octaves * 7)));
      }
    }
  }
}

TEST_CASE("WarmUpEngine minor keys default to Harmonic minor and toggle to Natural minor",
          "[warmup]") {
  const auto a_min = KeySignature{0, Mode::Minor};
  const auto d_min = KeySignature{-1, Mode::Minor};
  const auto fs_min = KeySignature{3, Mode::Minor};
  const auto c_min = KeySignature{-3, Mode::Minor};

  SECTION("A Minor Harmonic (default) vs Natural") {
    WarmUpEngine harm(WarmUpConfig{.key = a_min,
                                   .octaves = 1,
                                   .hand = HandSelection::RightOnly,
                                   .minor_mode = MinorScaleMode::Harmonic,
                                   .exercise = WarmUpExerciseKind::ScaleAndCadence});
    REQUIRE(harm.steps().size() == 19); // 15 scale + 4 cadence
    // 7th degree ascending is index 6: G#5 (80) in A4..A5
    REQUIRE(format_note(harm.steps()[6].notes[0].spelled) == "G#5");
    REQUIRE(harm.steps()[6].expected_midi == std::vector<MidiPitch>{80});
    // Cadence steps 15..18: i (Am), iv (Dm), V (E), i (Am)
    REQUIRE(harm.steps()[15].label == "i (Am)");
    REQUIRE(harm.steps()[16].label == "iv (Dm)");
    REQUIRE(harm.steps()[17].label == "V (E)");
    REQUIRE(harm.steps()[18].label == "i (Am)");

    WarmUpEngine nat(WarmUpConfig{.key = a_min,
                                  .octaves = 1,
                                  .hand = HandSelection::RightOnly,
                                  .minor_mode = MinorScaleMode::Natural,
                                  .exercise = WarmUpExerciseKind::ScaleAndCadence});
    REQUIRE(format_note(nat.steps()[6].notes[0].spelled) == "G5");
    REQUIRE(nat.steps()[6].expected_midi == std::vector<MidiPitch>{79});
    REQUIRE(nat.steps()[17].label == "v (Em)");
  }

  SECTION("D Minor, F# Minor, and C Minor harmonic vs natural cadences") {
    WarmUpEngine d_harm(WarmUpConfig{.key = d_min,
                                     .octaves = 1,
                                     .hand = HandSelection::HandsTogether,
                                     .minor_mode = MinorScaleMode::Harmonic,
                                     .exercise = WarmUpExerciseKind::CadenceOnly});
    REQUIRE(d_harm.steps().size() == 4);
    REQUIRE(d_harm.steps()[0].label == "i (Dm)");
    REQUIRE(d_harm.steps()[1].label == "iv (Gm)");
    REQUIRE(d_harm.steps()[2].label == "V (A)");
    REQUIRE(d_harm.steps()[3].label == "i (Dm)");

    WarmUpEngine fs_harm(WarmUpConfig{.key = fs_min,
                                      .octaves = 1,
                                      .hand = HandSelection::RightOnly,
                                      .minor_mode = MinorScaleMode::Harmonic,
                                      .exercise = WarmUpExerciseKind::CadenceOnly});
    REQUIRE(fs_harm.steps()[0].label == "i (F#m)");
    REQUIRE(fs_harm.steps()[1].label == "iv (Bm)");
    REQUIRE(fs_harm.steps()[2].label == "V (C#)");

    WarmUpEngine c_nat(WarmUpConfig{.key = c_min,
                                    .octaves = 1,
                                    .hand = HandSelection::RightOnly,
                                    .minor_mode = MinorScaleMode::Natural,
                                    .exercise = WarmUpExerciseKind::CadenceOnly});
    REQUIRE(c_nat.steps()[0].label == "i (Cm)");
    REQUIRE(c_nat.steps()[1].label == "iv (Fm)");
    REQUIRE(c_nat.steps()[2].label == "v (Gm)");
  }
}

TEST_CASE("WarmUpEngine respects HandSelection in scale and cadence", "[warmup]") {
  const auto c_maj = KeySignature{0, Mode::Major};

  WarmUpEngine ht(WarmUpConfig{.key = c_maj,
                               .octaves = 1,
                               .hand = HandSelection::HandsTogether,
                               .exercise = WarmUpExerciseKind::CadenceOnly});
  REQUIRE(ht.steps().size() == 4);
  REQUIRE(ht.steps()[0].notes.size() == 4); // 3 RH triad + 1 LH bass root
  REQUIRE(ht.steps()[0].label == "I (C)");
  REQUIRE(ht.steps()[1].label == "IV (F)");
  REQUIRE(ht.steps()[2].label == "V (G)");
  REQUIRE(ht.steps()[3].label == "I (C)");

  WarmUpEngine rh(WarmUpConfig{.key = c_maj,
                               .octaves = 1,
                               .hand = HandSelection::RightOnly,
                               .exercise = WarmUpExerciseKind::CadenceOnly});
  REQUIRE(rh.steps()[0].notes.size() == 3);
  for (const auto& n : rh.steps()[0].notes) {
    REQUIRE(n.clef == Clef::Treble);
  }

  WarmUpEngine lh(WarmUpConfig{.key = c_maj,
                               .octaves = 1,
                               .hand = HandSelection::LeftOnly,
                               .exercise = WarmUpExerciseKind::CadenceOnly});
  REQUIRE(lh.steps()[0].notes.size() == 3);
  for (const auto& n : lh.steps()[0].notes) {
    REQUIRE(n.clef == Clef::Bass);
  }
}

TEST_CASE("WarmUpEngine evaluates input, advances automatically, and tracks telemetry",
          "[warmup]") {
  WarmUpEngine engine(WarmUpConfig{
      .key = KeySignature{0, Mode::Major},
      .octaves = 1,
      .hand = HandSelection::RightOnly,
      .exercise = WarmUpExerciseKind::CadenceOnly,
  });

  REQUIRE(engine.current_step_index() == 0);
  REQUIRE_FALSE(engine.is_complete());
  REQUIRE(engine.expected_cardinality() == 3);

  // Empty input ignored
  const auto ignored = engine.submit_played_notes({});
  REQUIRE(ignored.status == WarmUpEvalStatus::Ignored);
  REQUIRE(engine.current_step_index() == 0);

  // Wrong input marks step Red and does NOT advance
  const std::array<MidiPitch, 3> wrong{60, 63, 67}; // Cm instead of C
  const auto miss = engine.submit_played_notes(wrong);
  REQUIRE(miss.status == WarmUpEvalStatus::Incorrect);
  REQUIRE(miss.evaluated_step_index == 0);
  REQUIRE_FALSE(miss.exercise_completed);
  REQUIRE(engine.current_step_index() == 0);
  REQUIRE(engine.step_feedback(0) == WarmUpStepFeedback::Incorrect);
  REQUIRE(engine.total_attempts() == 1);
  REQUIRE(engine.correct_attempts() == 0);

  // Play all 4 cadence chords correctly (testing duplicate/unsorted input normalization too)
  for (std::size_t i = 0; i < 4; ++i) {
    auto target = engine.steps()[i].expected_midi;
    std::reverse(target.begin(), target.end());
    target.push_back(target.front()); // duplicate note should still match
    const auto hit = engine.submit_played_notes(target);
    REQUIRE(hit.status == WarmUpEvalStatus::Correct);
    REQUIRE(hit.evaluated_step_index == i);
    REQUIRE(engine.step_feedback(i) == WarmUpStepFeedback::Correct);
    REQUIRE(hit.exercise_completed == (i == 3));
  }

  REQUIRE(engine.is_complete());
  REQUIRE(engine.completed_runs() == 1);
  REQUIRE(engine.total_attempts() == 5);
  REQUIRE(engine.correct_attempts() == 4);
  REQUIRE(engine.accuracy_ratio() == 0.8);

  // Extra input after completion returns AlreadyComplete
  const std::array<MidiPitch, 3> extra{60, 64, 67};
  const auto post = engine.submit_played_notes(extra);
  REQUIRE(post.status == WarmUpEvalStatus::AlreadyComplete);

  // Reset restarts cursor and clears feedback while keeping completed_runs
  engine.reset();
  REQUIRE_FALSE(engine.is_complete());
  REQUIRE(engine.current_step_index() == 0);
  REQUIRE(engine.step_feedback(0) == WarmUpStepFeedback::Pending);
  REQUIRE(engine.completed_runs() == 1);
}

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <utility>
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
    REQUIRE(found == i);
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
        const auto bass_it =
            std::ranges::find_if(step.notes, [](const auto& n) { return n.clef == Clef::Bass; });
        const auto treble_it =
            std::ranges::find_if(step.notes, [](const auto& n) { return n.clef == Clef::Treble; });
        REQUIRE(bass_it != step.notes.end());
        REQUIRE(treble_it != step.notes.end());
        REQUIRE(bass_it->midi < treble_it->midi);
        REQUIRE((treble_it->midi - bass_it->midi) % 12 == 0);
        REQUIRE(step.expected_midi.size() == 2);
        REQUIRE(step.expected_midi[0] == bass_it->midi);
        REQUIRE(step.expected_midi[1] == treble_it->midi);
        REQUIRE_FALSE(step.is_cadence);
        REQUIRE(step.is_descending == (std::cmp_greater(s, octaves * 7)));
      }
    }
  }
}

TEST_CASE("WarmUpEngine minor keys default to Harmonic minor and toggle to Natural minor",
          "[warmup]") {
  const auto a_min = KeySignature{.fifths = 0, .mode = Mode::Minor};
  const auto d_min = KeySignature{.fifths = -1, .mode = Mode::Minor};
  const auto fs_min = KeySignature{.fifths = 3, .mode = Mode::Minor};
  const auto c_min = KeySignature{.fifths = -3, .mode = Mode::Minor};

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
  const auto c_maj = KeySignature{.fifths = 0, .mode = Mode::Major};

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
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
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
    std::ranges::reverse(target);
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

TEST_CASE("WarmUpEngine FullCircuit builds all 4 stations across all 24 keys", "[warmup]") {
  for (std::size_t k_idx = 0; k_idx < kWeeklyKeyCycle.size(); ++k_idx) {
    const auto key = weekly_key_at(k_idx);
    WarmUpEngine engine(WarmUpConfig{
        .key = key,
        .octaves = 1,
        .hand = HandSelection::HandsTogether,
        .minor_mode = MinorScaleMode::Harmonic,
        .exercise = WarmUpExerciseKind::FullCircuit,
    });

    // 1-oct Scale (15) + 1-oct Arpeggio (7) + Alternating Inversions (7 RH + 7 LH = 14) +
    // Voice-Led Cadence (4) = 40 steps
    REQUIRE(engine.steps().size() == 40);

    // Verify exact station ordering
    for (std::size_t i = 0; i < 15; ++i) {
      REQUIRE(engine.steps()[i].station == WarmUpStation::ParallelScale);
      REQUIRE(engine.steps()[i].beat_duration == 1);
    }
    for (std::size_t i = 15; i < 22; ++i) {
      REQUIRE(engine.steps()[i].station == WarmUpStation::TonicArpeggio);
      REQUIRE(engine.steps()[i].beat_duration == 1);
      REQUIRE(engine.steps()[i].notes.size() == 2);
      REQUIRE(engine.steps()[i].expected_midi[0] < engine.steps()[i].expected_midi[1]);
    }
    for (std::size_t i = 22; i < 29; ++i) {
      REQUIRE(engine.steps()[i].station == WarmUpStation::TriadInversions);
      REQUIRE(engine.steps()[i].beat_duration == 2);
      REQUIRE(engine.steps()[i].notes.size() == 3);
      for (const auto& n : engine.steps()[i].notes) {
        REQUIRE(n.clef == Clef::Treble);
      }
    }
    for (std::size_t i = 29; i < 36; ++i) {
      REQUIRE(engine.steps()[i].station == WarmUpStation::TriadInversions);
      REQUIRE(engine.steps()[i].beat_duration == 2);
      REQUIRE(engine.steps()[i].notes.size() == 3);
      for (const auto& n : engine.steps()[i].notes) {
        REQUIRE(n.clef == Clef::Bass);
      }
    }
    for (std::size_t i = 36; i < 40; ++i) {
      REQUIRE(engine.steps()[i].station == WarmUpStation::VoiceLedCadence);
      REQUIRE(engine.steps()[i].beat_duration == 2);
      REQUIRE(engine.steps()[i].is_cadence);
      REQUIRE(engine.steps()[i].notes.size() == 4); // 1 LH bass root + 3 RH voice-led triad
    }
  }
}

TEST_CASE("WarmUpEngine TonicArpeggio and TriadInversions generate exact pitches", "[warmup]") {
  SECTION("C Major 1-octave and 2-octave Tonic Arpeggio (RightOnly)") {
    WarmUpEngine arp1(WarmUpConfig{
        .key = KeySignature{.fifths = 0, .mode = Mode::Major},
        .octaves = 1,
        .hand = HandSelection::RightOnly,
        .exercise = WarmUpExerciseKind::ArpeggioOnly,
    });
    REQUIRE(arp1.steps().size() == 7);
    const std::array<MidiPitch, 7> expected_1oct{60, 64, 67, 72, 67, 64, 60};
    for (std::size_t i = 0; i < expected_1oct.size(); ++i) {
      REQUIRE(arp1.steps()[i].expected_midi == std::vector<MidiPitch>{expected_1oct[i]});
    }

    WarmUpEngine arp2(WarmUpConfig{
        .key = KeySignature{.fifths = 0, .mode = Mode::Major},
        .octaves = 2,
        .hand = HandSelection::RightOnly,
        .exercise = WarmUpExerciseKind::ArpeggioOnly,
    });
    REQUIRE(arp2.steps().size() == 13);
    const std::array<MidiPitch, 13> expected_2oct{60, 64, 67, 72, 76, 79, 84,
                                                  79, 76, 72, 67, 64, 60};
    for (std::size_t i = 0; i < expected_2oct.size(); ++i) {
      REQUIRE(arp2.steps()[i].expected_midi == std::vector<MidiPitch>{expected_2oct[i]});
    }
  }

  SECTION("C Major Triad Inversions arc Root -> 1st -> 2nd -> Root 8va -> 2nd -> 1st -> Root") {
    WarmUpEngine inv_rh(WarmUpConfig{
        .key = KeySignature{.fifths = 0, .mode = Mode::Major},
        .octaves = 1,
        .hand = HandSelection::RightOnly,
        .exercise = WarmUpExerciseKind::InversionsOnly,
    });
    REQUIRE(inv_rh.steps().size() == 7);
    REQUIRE(inv_rh.steps()[0].expected_midi == std::vector<MidiPitch>{60, 64, 67});
    REQUIRE(inv_rh.steps()[1].expected_midi == std::vector<MidiPitch>{64, 67, 72});
    REQUIRE(inv_rh.steps()[2].expected_midi == std::vector<MidiPitch>{67, 72, 76});
    REQUIRE(inv_rh.steps()[3].expected_midi == std::vector<MidiPitch>{72, 76, 79});
    REQUIRE(inv_rh.steps()[4].expected_midi == std::vector<MidiPitch>{67, 72, 76});
    REQUIRE(inv_rh.steps()[5].expected_midi == std::vector<MidiPitch>{64, 67, 72});
    REQUIRE(inv_rh.steps()[6].expected_midi == std::vector<MidiPitch>{60, 64, 67});
  }
}

TEST_CASE("WarmUpEngine VoiceLedCadence preserves common tones across all 24 keys", "[warmup]") {
  SECTION("C Major uses smooth I -> IV6/4 -> V6 -> I voicings") {
    WarmUpEngine cad(WarmUpConfig{
        .key = KeySignature{.fifths = 0, .mode = Mode::Major},
        .octaves = 1,
        .hand = HandSelection::RightOnly,
        .exercise = WarmUpExerciseKind::CadenceOnly,
    });
    REQUIRE(cad.steps().size() == 4);
    REQUIRE(cad.steps()[0].expected_midi == std::vector<MidiPitch>{60, 64, 67}); // C4 E4 G4
    REQUIRE(cad.steps()[1].expected_midi == std::vector<MidiPitch>{60, 65, 69}); // C4 F4 A4
    REQUIRE(cad.steps()[2].expected_midi == std::vector<MidiPitch>{59, 62, 67}); // B3 D4 G4
    REQUIRE(cad.steps()[3].expected_midi == std::vector<MidiPitch>{60, 64, 67}); // C4 E4 G4
  }

  SECTION("A Minor Harmonic uses smooth i -> iv6/4 -> V6 -> i voicings") {
    WarmUpEngine cad(WarmUpConfig{
        .key = KeySignature{.fifths = 0, .mode = Mode::Minor},
        .octaves = 1,
        .hand = HandSelection::RightOnly,
        .minor_mode = MinorScaleMode::Harmonic,
        .exercise = WarmUpExerciseKind::CadenceOnly,
    });
    REQUIRE(cad.steps().size() == 4);
    REQUIRE(cad.steps()[0].expected_midi == std::vector<MidiPitch>{57, 60, 64}); // A3 C4 E4
    REQUIRE(cad.steps()[1].expected_midi == std::vector<MidiPitch>{57, 62, 65}); // A3 D4 F4
    REQUIRE(cad.steps()[2].expected_midi == std::vector<MidiPitch>{56, 59, 64}); // G#3 B3 E4
    REQUIRE(cad.steps()[3].expected_midi == std::vector<MidiPitch>{57, 60, 64}); // A3 C4 E4
  }

  SECTION("All 24 keys stay tightly voice-led with LH bass strictly below RH triad") {
    for (std::size_t k_idx = 0; k_idx < kWeeklyKeyCycle.size(); ++k_idx) {
      WarmUpEngine cad(WarmUpConfig{
          .key = weekly_key_at(k_idx),
          .octaves = 1,
          .hand = HandSelection::HandsTogether,
          .minor_mode = MinorScaleMode::Harmonic,
          .exercise = WarmUpExerciseKind::CadenceOnly,
      });
      REQUIRE(cad.steps().size() == 4);
      auto rh_notes = [](const WarmUpStep& s) {
        std::vector<int> out;
        for (const auto& n : s.notes) {
          if (n.clef == Clef::Treble) {
            out.push_back(static_cast<int>(n.midi));
          }
        }
        std::ranges::sort(out);
        return out;
      };
      const auto rh0 = rh_notes(cad.steps()[0]);
      const auto rh1 = rh_notes(cad.steps()[1]);
      const auto rh2 = rh_notes(cad.steps()[2]);
      REQUIRE(rh0.size() == 3);
      REQUIRE(rh1.size() == 3);
      REQUIRE(rh2.size() == 3);
      // I -> IV6/4 shares the bottom tonic note
      REQUIRE(rh0[0] == rh1[0]);
      // V6 -> I shares the top dominant note
      REQUIRE(rh2[2] == rh0[2]);
      // LH bass is strictly below RH lowest note on every cadence step
      for (const auto& step : cad.steps()) {
        const auto bass_it =
            std::ranges::find_if(step.notes, [](const auto& n) { return n.clef == Clef::Bass; });
        REQUIRE(bass_it != step.notes.end());
        for (const auto& n : step.notes) {
          if (n.clef == Clef::Treble) {
            REQUIRE(bass_it->midi < n.midi);
          }
        }
      }
    }
  }
}

TEST_CASE("WarmUpEngine Adaptive 2-Phase Pacing, Guided Pulse evenness, and BPM ladder",
          "[warmup]") {
  WarmUpEngine engine(WarmUpConfig{
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
      .octaves = 1,
      .hand = HandSelection::RightOnly,
      .exercise = WarmUpExerciseKind::CadenceOnly,
      .start_bpm = 72.0,
      .auto_continue_passes = true,
  });

  REQUIRE(engine.pacing_mode() == WarmUpPacingMode::ExploreWait);
  REQUIRE(engine.phase_state() == WarmUpPhaseState::Playing);
  REQUIRE(engine.pass_number() == 1);
  REQUIRE_FALSE(engine.is_mid_station());

  SECTION("Unclean Pass 1 (<90%) repeats ExploreWait after transition breather") {
    // 1 miss + 4 hits = 80% accuracy (< 90%)
    (void)engine.submit_played_notes(std::array<MidiPitch, 3>{60, 63, 67});
    REQUIRE(engine.is_mid_station() == false); // still on step 0
    for (std::size_t i = 0; i < 4; ++i) {
      const auto res = engine.submit_played_notes(engine.steps()[i].expected_midi);
      REQUIRE(res.status == WarmUpEvalStatus::Correct);
      if (i == 0) {
        REQUIRE(engine.is_mid_station());
      }
    }
    REQUIRE(engine.phase_state() == WarmUpPhaseState::PassTransition);
    REQUIRE(engine.last_pass_summary().has_value());
    REQUIRE(engine.last_pass_summary()->next_pacing_mode == WarmUpPacingMode::ExploreWait);

    // Tick past 2500 ms breather -> restarts Pass 2 in ExploreWait
    const auto tick = engine.update(2600.0);
    REQUIRE(tick.pass_started);
    REQUIRE(engine.pacing_mode() == WarmUpPacingMode::ExploreWait);
    REQUIRE(engine.phase_state() == WarmUpPhaseState::Playing);
    REQUIRE(engine.pass_number() == 2);
  }

  SECTION("Clean Pass 1 enters Guided Pulse with 4-beat count-in, grades evenness, and bumps BPM") {
    // Complete Pass 1 cleanly (100%)
    for (std::size_t i = 0; i < 4; ++i) {
      (void)engine.submit_played_notes(engine.steps()[i].expected_midi);
    }
    REQUIRE(engine.phase_state() == WarmUpPhaseState::PassTransition);
    REQUIRE(engine.last_pass_summary()->next_pacing_mode == WarmUpPacingMode::GuidedPulse);
    REQUIRE(engine.last_pass_summary()->next_bpm == 72.0);

    // Finish breather -> enters 4-beat CountIn at 72 BPM
    auto tick = engine.update(2550.0);
    REQUIRE(engine.phase_state() == WarmUpPhaseState::CountIn);
    REQUIRE(engine.pacing_mode() == WarmUpPacingMode::GuidedPulse);
    REQUIRE(tick.metronome_click);
    REQUIRE(tick.metronome_accent); // Beat 1 of count-in

    // Advance remaining 3 beats of count-in (72 BPM -> 833.333 ms per beat)
    const double beat_ms = 60000.0 / 72.0;
    int clicks = 1;
    for (int step = 0; step < 40 && engine.phase_state() == WarmUpPhaseState::CountIn; ++step) {
      const auto t = engine.update(beat_ms / 4.0);
      if (t.metronome_click) {
        ++clicks;
      }
    }
    REQUIRE(clicks >= 4);
    REQUIRE(engine.phase_state() == WarmUpPhaseState::Playing);
    REQUIRE(engine.current_step_index() == 0);

    // Play all 4 cadence chords cleanly on-beat (each cadence chord is beat_duration == 2 beats)
    for (std::size_t i = 0; i < 4; ++i) {
      if (i > 0) {
        (void)engine.update(beat_ms * 2.0);
      }
      const auto res = engine.submit_played_notes(engine.steps()[i].expected_midi);
      REQUIRE(res.status == WarmUpEvalStatus::Correct);
    }
    REQUIRE(engine.phase_state() == WarmUpPhaseState::PassTransition);
    REQUIRE(engine.last_pass_summary()->evenness_ratio >= 0.90);
    REQUIRE(engine.last_pass_summary()->bpm_increased);
    REQUIRE(engine.last_pass_summary()->next_bpm == 80.0);
    REQUIRE(engine.best_clean_bpm().has_value());
    REQUIRE(*engine.best_clean_bpm() == 72.0);
  }

  SECTION("Uneven rhythm in Guided Pulse holds BPM steady without bumping") {
    for (std::size_t i = 0; i < 4; ++i) {
      (void)engine.submit_played_notes(engine.steps()[i].expected_midi);
    }
    (void)engine.update(2550.0);
    const double beat_ms = 60000.0 / 72.0;
    for (int step = 0; step < 40 && engine.phase_state() == WarmUpPhaseState::CountIn; ++step) {
      (void)engine.update(beat_ms / 4.0);
    }
    REQUIRE(engine.phase_state() == WarmUpPhaseState::Playing);

    // Play chords way too fast (100 ms apart instead of 2 beats = 1666 ms) -> low evenness
    for (std::size_t i = 0; i < 4; ++i) {
      if (i > 0) {
        (void)engine.update(100.0);
      }
      (void)engine.submit_played_notes(engine.steps()[i].expected_midi);
    }
    REQUIRE(engine.phase_state() == WarmUpPhaseState::PassTransition);
    REQUIRE(engine.last_pass_summary()->evenness_ratio < kWarmUpCleanEvennessThreshold);
    REQUIRE_FALSE(engine.last_pass_summary()->bpm_increased);
    REQUIRE(engine.last_pass_summary()->next_bpm == 72.0);
    REQUIRE_FALSE(engine.best_clean_bpm().has_value());
  }
}

TEST_CASE("WarmUpEngine attaches standard 24-key scale and arpeggio fingerings", "[warmup]") {
  // Verify all 24 keys (1 and 2 octaves, Harmonic and Natural minor) have valid 1..5 fingerings
  // with zero consecutive duplicate fingers on scales.
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
      std::uint8_t prev_rh = 0;
      std::uint8_t prev_lh = 0;
      for (const auto& step : engine.steps()) {
        for (const auto& n : step.notes) {
          REQUIRE(n.finger >= 1);
          REQUIRE(n.finger <= 5);
          if (n.clef == Clef::Treble) {
            REQUIRE(n.finger != prev_rh);
            prev_rh = n.finger;
          } else {
            REQUIRE(n.finger != prev_lh);
            prev_lh = n.finger;
          }
        }
      }
    }
  }

  // Verify thumb (1) never lands on a black key in standard keys (C, G, D, A, E, F, Bb, Eb, Ab,
  // B, Db, Gb major + Am, Em, Bm, Dm, Gm, Cm, Fm, F#m)
  auto is_black_key = [](MidiPitch m) {
    const int pc = static_cast<int>(m) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
  };
  const std::array<KeySignature, 18> kStandardKeys{{
      {.fifths = 0, .mode = Mode::Major},
      {.fifths = 1, .mode = Mode::Major},
      {.fifths = 2, .mode = Mode::Major},
      {.fifths = 3, .mode = Mode::Major},
      {.fifths = 4, .mode = Mode::Major},
      {.fifths = 5, .mode = Mode::Major},
      {.fifths = -1, .mode = Mode::Major},
      {.fifths = -2, .mode = Mode::Major},
      {.fifths = -3, .mode = Mode::Major},
      {.fifths = -4, .mode = Mode::Major},
      {.fifths = -5, .mode = Mode::Major},
      {.fifths = -6, .mode = Mode::Major},
      {.fifths = 0, .mode = Mode::Minor},
      {.fifths = 1, .mode = Mode::Minor},
      {.fifths = 2, .mode = Mode::Minor},
      {.fifths = -1, .mode = Mode::Minor},
      {.fifths = -2, .mode = Mode::Minor},
      {.fifths = -3, .mode = Mode::Minor},
  }};
  for (const auto key : kStandardKeys) {
    WarmUpEngine engine(WarmUpConfig{
        .key = key,
        .octaves = 2,
        .hand = HandSelection::HandsTogether,
        .minor_mode = MinorScaleMode::Harmonic,
        .exercise = WarmUpExerciseKind::ScaleOnly,
    });
    for (const auto& step : engine.steps()) {
      for (const auto& n : step.notes) {
        if (n.finger == 1U) {
          REQUIRE_FALSE(is_black_key(n.midi));
        }
      }
    }
  }
}

TEST_CASE("WarmUpEngine generates Chromatic, Dom7/Dim7 Arpeggios, Contrary Motion, and LH Stride "
          "across all 24 keys",
          "[warmup]") {
  for (std::size_t k_idx = 0; k_idx < kWeeklyKeyCycle.size(); ++k_idx) {
    const auto key = weekly_key_at(k_idx);

    SECTION("ChromaticScale spans 1 octave up & down (25 steps) with 1..3 fingerings") {
      WarmUpEngine chrom(WarmUpConfig{
          .key = key,
          .octaves = 1,
          .hand = HandSelection::HandsTogether,
          .exercise = WarmUpExerciseKind::ChromaticOnly,
      });
      REQUIRE(chrom.steps().size() == 25);
      REQUIRE(chrom.steps().front().station == WarmUpStation::ChromaticScale);
      REQUIRE(chrom.steps()[0].expected_midi == chrom.steps()[24].expected_midi);
      for (std::size_t i = 0; i < 12; ++i) {
        REQUIRE(chrom.steps()[i + 1].expected_midi[1] == chrom.steps()[i].expected_midi[1] + 1);
      }
      for (const auto& step : chrom.steps()) {
        for (const auto& n : step.notes) {
          REQUIRE(n.finger >= 1);
          REQUIRE(n.finger <= 3);
        }
      }
    }

    SECTION("SeventhArpeggios covers V7 and vii°7 up & down with valid fingerings") {
      WarmUpEngine arp7(WarmUpConfig{
          .key = key,
          .octaves = 1,
          .hand = HandSelection::HandsTogether,
          .exercise = WarmUpExerciseKind::SeventhArpeggiosOnly,
      });
      // V7: 5 notes up + 4 down = 9; vii°7: 5 notes up + 4 down = 9 -> 18 steps total
      REQUIRE(arp7.steps().size() == 18);
      REQUIRE(arp7.steps().front().station == WarmUpStation::SeventhArpeggios);
      for (const auto& step : arp7.steps()) {
        for (const auto& n : step.notes) {
          REQUIRE(n.finger >= 1);
          REQUIRE(n.finger <= 5);
        }
      }
    }

    SECTION("ContraryScale moves RH up while LH moves down symmetrically") {
      WarmUpEngine con(WarmUpConfig{
          .key = key,
          .octaves = 1,
          .hand = HandSelection::HandsTogether,
          .exercise = WarmUpExerciseKind::ContraryScaleOnly,
      });
      REQUIRE(con.steps().size() == 15);
      REQUIRE(con.steps().front().station == WarmUpStation::ContraryScale);
      const auto& s0 = con.steps()[0];
      const auto& s7 = con.steps()[7];
      // At apex (step 7), RH went up 12 semitones and LH went down 12 semitones
      REQUIRE(s7.expected_midi[1] == s0.expected_midi[1] + 12);
      REQUIRE(s7.expected_midi[0] + 12 == s0.expected_midi[0]);
    }

    SECTION("LhStrideJumps creates 12 bass-clef 3/4 steps (I -> IV -> V -> I)") {
      WarmUpEngine stride(WarmUpConfig{
          .key = key,
          .octaves = 1,
          .hand = HandSelection::HandsTogether,
          .exercise = WarmUpExerciseKind::LhStrideOnly,
      });
      REQUIRE(stride.steps().size() == 12);
      REQUIRE(stride.steps().front().station == WarmUpStation::LhStrideJumps);
      for (std::size_t bar = 0; bar < 4; ++bar) {
        const auto& bass_hit = stride.steps()[bar * 3 + 0];
        const auto& chord1 = stride.steps()[bar * 3 + 1];
        const auto& chord2 = stride.steps()[bar * 3 + 2];
        REQUIRE(bass_hit.solo_hand_clef == Clef::Bass);
        REQUIRE(chord1.notes.size() == 3);
        REQUIRE(chord2.notes.size() == 3);
        REQUIRE(bass_hit.expected_midi.front() < chord1.expected_midi.front());
      }
    }
  }

  SECTION("RomanticCircuit chains ParallelScale, Chromatic, SeventhArpeggios, and LhStrideJumps") {
    WarmUpEngine rom(WarmUpConfig{
        .key = KeySignature{.fifths = -2, .mode = Mode::Minor},
        .octaves = 1,
        .hand = HandSelection::HandsTogether,
        .exercise = WarmUpExerciseKind::RomanticCircuit,
    });
    REQUIRE(rom.steps().size() == 15 + 25 + 18 + 12);
    REQUIRE(rom.steps()[0].station == WarmUpStation::ParallelScale);
    REQUIRE(rom.steps()[15].station == WarmUpStation::ChromaticScale);
    REQUIRE(rom.steps()[40].station == WarmUpStation::SeventhArpeggios);
    REQUIRE(rom.steps()[58].station == WarmUpStation::LhStrideJumps);
  }
}

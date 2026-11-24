#include "core/lesson_engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace selfano::core;
using Catch::Matchers::WithinAbs;

namespace {

[[nodiscard]] LessonNote ln(NoteLetter l, Accidental a, int oct, std::uint8_t f = 0,
                            bool ts = false, bool tp = false) {
  const SpelledNote sp{l, a, oct};
  return LessonNote{sp, to_midi(sp).value(), f, ts, tp};
}

[[nodiscard]] LessonScore make_sample_lesson() {
  LessonScore s{};
  s.meta = LessonMetadata{
      .id = "sample_01",
      .tier = 1,
      .order_in_tier = 1,
      .title = "Sample Study",
      .composer_source = "Test",
      .key = KeySignature{0, Mode::Major},
      .time_signature = TimeSignature{4, 4},
      .target_bpm = 100.0,
  };

  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.harmony = {{0, "C", "I"}};
  m1.treble_events = {
      {0,
       {NoteBaseDuration::Half, false},
       false,
       0,
       {ln(NoteLetter::E, Accidental::Natural, 4, 3)}},
      {960,
       {NoteBaseDuration::Half, false},
       false,
       0,
       {ln(NoteLetter::G, Accidental::Natural, 4, 5, true, false)}},
  };
  m1.bass_events = {
      {0,
       {NoteBaseDuration::Whole, false},
       false,
       0,
       {ln(NoteLetter::C, Accidental::Natural, 3, 5)}},
  };

  LessonMeasure m2{};
  m2.bar_number = 2;
  m2.harmony = {{0, "G7", "V7"}};
  m2.treble_events = {
      {0,
       {NoteBaseDuration::Half, false},
       false,
       0,
       {ln(NoteLetter::G, Accidental::Natural, 4, 5, false, true)}},
      {960,
       {NoteBaseDuration::Half, false},
       false,
       0,
       {ln(NoteLetter::F, Accidental::Natural, 4, 4)}},
  };
  m2.bass_events = {
      {0,
       {NoteBaseDuration::Whole, false},
       false,
       0,
       {ln(NoteLetter::G, Accidental::Natural, 2, 5)}},
  };

  s.measures = {m1, m2};
  s.phrases = {
      LessonPhrase{"Phrase A (Bar 1)", 0, 0},
      LessonPhrase{"Phrase B (Bar 2)", 1, 1},
  };
  return s;
}

} // namespace

TEST_CASE("LessonEngine builds targets respecting hand mode and excluding tie_stop attacks",
          "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{LessonPracticeMode::WaitForNote, LessonHandMode::HandsTogether});

  // Bar 1 tick 0 (C3+E4), Bar 1 tick 960 (G4 tie_start), Bar 2 tick 0 (G2 only! G4 is tie_stop),
  // Bar 2 tick 960 (F4)
  REQUIRE(engine.targets().size() == 4);
  REQUIRE(engine.targets()[0].expected_midi == std::vector<MidiPitch>{48, 64});
  REQUIRE(engine.targets()[1].expected_midi == std::vector<MidiPitch>{67});
  REQUIRE(engine.targets()[2].expected_midi == std::vector<MidiPitch>{43});
  REQUIRE(engine.targets()[3].expected_midi == std::vector<MidiPitch>{65});

  engine.set_hand_mode(LessonHandMode::RightHand);
  // Bar 2 tick 0 has no fresh RH attack (G4 is tied over), so 3 RH targets remain
  REQUIRE(engine.targets().size() == 3);
  REQUIRE(engine.targets()[0].expected_midi == std::vector<MidiPitch>{64});
  REQUIRE(engine.targets()[1].expected_midi == std::vector<MidiPitch>{67});
  REQUIRE(engine.targets()[2].expected_midi == std::vector<MidiPitch>{65});
}

TEST_CASE("LessonEngine ChordSkeleton matches octave-flexible pitch classes", "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{LessonPracticeMode::ChordSkeleton, LessonHandMode::HandsTogether});

  REQUIRE(engine.targets().size() == 2);
  REQUIRE(engine.targets()[0].pitch_class_match);

  // Play C major triad in inverted upper octave -> matches bar 1 ("C")
  const auto r1 = engine.submit_played_notes(std::array<MidiPitch, 3>{64, 67, 72});
  REQUIRE(r1.matched);
  REQUIRE_FALSE(r1.run_completed);

  // Wrong chord -> flagged
  const auto bad = engine.submit_played_notes(std::array<MidiPitch, 3>{62, 66, 69});
  REQUIRE(bad.wrong_note);

  // G7 (G, B, D, F) in any octave -> completes run
  const auto r2 = engine.submit_played_notes(std::array<MidiPitch, 4>{43, 59, 62, 65});
  REQUIRE(r2.matched);
  REQUIRE(r2.run_completed);
}

TEST_CASE("LessonEngine SteadyPulse runs 1-bar count-in, grades hit windows, and loops",
          "[lesson_engine]") {
  LessonEngine engine;
  auto cfg = LessonConfig{LessonPracticeMode::SteadyPulse, LessonHandMode::RightHand,
                          TempoLadderStep::Target100};
  engine.load_score(make_sample_lesson(), cfg);

  REQUIRE_THAT(engine.effective_bpm(), WithinAbs(100.0, 1e-6)); // 600 ms / beat

  const auto start_ev = engine.start();
  REQUIRE(start_ev.metronome_click);
  REQUIRE(start_ev.metronome_accent);
  REQUIRE(engine.playback_state() == LessonPlaybackState::CountIn);
  REQUIRE(engine.count_in_beats_remaining() == 4);

  // Advance 3 more beats (3 * 600 ms = 1800 ms) -> enters Playing on beat 4 completion
  for (int i = 0; i < 3; ++i) {
    const auto tick = engine.update(600.0);
    REQUIRE(tick.metronome_click);
  }
  const auto enter_play = engine.update(600.0);
  REQUIRE(enter_play.metronome_click);
  REQUIRE(enter_play.metronome_accent);
  REQUIRE(engine.playback_state() == LessonPlaybackState::Playing);

  // Hit target 0 (E4=64) near tick 0
  const auto h0 = engine.submit_played_notes(std::array<MidiPitch, 1>{64});
  REQUIRE(h0.matched);

  // Advance 2 beats (1200 ms) -> tick 960 (G4=67)
  (void)engine.update(600.0);
  (void)engine.update(600.0);
  const auto h1 = engine.submit_played_notes(std::array<MidiPitch, 1>{67});
  REQUIRE(h1.matched);

  // Advance 4 beats (2400 ms) -> Bar 2 tick 960 (F4=65)
  for (int i = 0; i < 4; ++i) {
    (void)engine.update(600.0);
  }
  const auto h2 = engine.submit_played_notes(std::array<MidiPitch, 1>{65});
  REQUIRE(h2.matched);

  // Finish remaining 2 beats -> completed!
  (void)engine.update(600.0);
  const auto end_ev = engine.update(650.0);
  REQUIRE(end_ev.run_completed);
  REQUIRE(engine.playback_state() == LessonPlaybackState::Completed);
  REQUIRE(engine.hit_count() == 3);
  REQUIRE(engine.miss_count() == 0);
}

TEST_CASE("LessonEngine build_demo_playback merges tied durations monotonically",
          "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{LessonPracticeMode::WaitForNote, LessonHandMode::RightHand,
                                 TempoLadderStep::Target100});
  const auto demo = engine.build_demo_playback();
  // E4 (half=1200ms), G4 (half+half tied=2400ms), F4 (half=1200ms)
  REQUIRE(demo.size() == 3);
  REQUIRE(demo[0].midi == 64);
  REQUIRE_THAT(demo[0].duration_ms, WithinAbs(1200.0, 1.0));
  REQUIRE(demo[1].midi == 67);
  REQUIRE_THAT(demo[1].duration_ms, WithinAbs(2400.0, 1.0));
  REQUIRE(demo[2].midi == 65);
  REQUIRE_THAT(demo[2].start_ms, WithinAbs(3600.0, 1.0));
}

TEST_CASE("LessonEngine mastery graduation, SM-2 revision, and Stage 3 split planner",
          "[lesson_engine]") {
  LessonProgress prog{};
  const LessonRunSummary run_60{
      .lesson_id = "sample_01",
      .practice_mode = LessonPracticeMode::SteadyPulse,
      .hand_mode = LessonHandMode::HandsTogether,
      .is_full_piece = true,
      .effective_bpm = 60.0,
      .accuracy = 0.95,
      .hits = 19,
      .misses = 1,
  };
  const auto u1 = apply_run_to_progress(prog, run_60, 100.0, 20000);
  REQUIRE_FALSE(u1.just_mastered);
  REQUIRE(prog.status == LessonMasteryStatus::Learning);
  REQUIRE(prog.ladder_60_clean);
  REQUIRE_FALSE(prog.ladder_80_clean);

  const LessonRunSummary run_100{
      .lesson_id = "sample_01",
      .practice_mode = LessonPracticeMode::SteadyPulse,
      .hand_mode = LessonHandMode::HandsTogether,
      .is_full_piece = true,
      .effective_bpm = 100.0,
      .accuracy = 0.95,
      .hits = 19,
      .misses = 1,
  };
  const auto u2 = apply_run_to_progress(prog, run_100, 100.0, 20000);
  REQUIRE(u2.just_mastered);
  REQUIRE(prog.status == LessonMasteryStatus::Mastered);
  REQUIRE(prog.ladder_100_clean);
  REQUIRE(prog.review_due_epoch_day > 20000);
  REQUIRE(effective_mastery_status(prog, 20000) == LessonMasteryStatus::Mastered);
  REQUIRE(effective_mastery_status(prog, prog.review_due_epoch_day) ==
          LessonMasteryStatus::NeedsReview);

  // Stage 3 split planner picks overdue mastered lesson for 180s revision + 420s active lesson
  std::map<std::string, LessonProgress> map;
  map["sample_01"] = prog;
  const auto no_due = plan_stage3_session(map, "t1_02", 20000);
  REQUIRE_FALSE(no_due.has_revision);
  REQUIRE(no_due.active_duration_sec == 600.0);

  const auto with_due = plan_stage3_session(map, "t1_02", prog.review_due_epoch_day);
  REQUIRE(with_due.has_revision);
  REQUIRE(with_due.revision_lesson_id == "sample_01");
  REQUIRE(with_due.revision_duration_sec == 180.0);
  REQUIRE(with_due.active_lesson_id == "t1_02");
  REQUIRE(with_due.active_duration_sec == 420.0);
}

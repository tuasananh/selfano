#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace selfano::core;
using Catch::Matchers::WithinAbs;

namespace {

[[nodiscard]] LessonNote ln(NoteLetter l, Accidental a, int oct, std::uint8_t f = 0,
                            bool ts = false, bool tp = false) {
  const SpelledNote sp{.letter = l, .accidental = a, .octave = oct};
  return LessonNote{
      .spelled = sp, .midi = to_midi(sp).value(), .finger = f, .tie_start = ts, .tie_stop = tp};
}

[[nodiscard]] LessonScore make_sample_lesson() {
  LessonScore s{};
  s.meta = LessonMetadata{
      .id = "sample_01",
      .tier = 1,
      .order_in_tier = 1,
      .title = "Sample Study",
      .composer_source = "Test",
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
      .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
      .target_bpm = 100.0,
  };

  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.harmony = {{.start_tick = 0, .chord_symbol = "C", .roman_numeral = "I"}};
  m1.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::E, Accidental::Natural, 4, 3)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::G, Accidental::Natural, 4, 5, true, false)}},
  };
  m1.bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::C, Accidental::Natural, 3, 5)}},
  };

  LessonMeasure m2{};
  m2.bar_number = 2;
  m2.harmony = {{.start_tick = 0, .chord_symbol = "G7", .roman_numeral = "V7"}};
  m2.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::G, Accidental::Natural, 4, 5, false, true)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::F, Accidental::Natural, 4, 4)}},
  };
  m2.bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes = {ln(NoteLetter::G, Accidental::Natural, 2, 5)}},
  };

  s.measures = {m1, m2};
  s.phrases = {
      LessonPhrase{.label = "Phrase A (Bar 1)", .start_bar = 0, .end_bar = 0},
      LessonPhrase{.label = "Phrase B (Bar 2)", .start_bar = 1, .end_bar = 1},
  };
  return s;
}

} // namespace

TEST_CASE("LessonEngine builds targets respecting hand mode and excluding tie_stop attacks",
          "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{.practice_mode = LessonPracticeMode::WaitForNote,
                                 .hand_mode = LessonHandMode::HandsTogether});

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
                    LessonConfig{.practice_mode = LessonPracticeMode::ChordSkeleton,
                                 .hand_mode = LessonHandMode::HandsTogether});

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
  auto cfg = LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                          .hand_mode = LessonHandMode::RightHand,
                          .tempo_step = TempoLadderStep::Target100};
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
                    LessonConfig{.practice_mode = LessonPracticeMode::WaitForNote,
                                 .hand_mode = LessonHandMode::RightHand,
                                 .tempo_step = TempoLadderStep::Target100});
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
  map.emplace("sample_01", prog);
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

TEST_CASE("LessonEngine aligns SteadyPulse count-in and demo playback on pickup measures",
          "[lesson_engine]") {
  SECTION("3/4 with 1-quarter pickup counts in 2 beats then enters upbeat 3") {
    LessonScore s{};
    s.meta = LessonMetadata{
        .id = "pickup_34",
        .tier = 5,
        .order_in_tier = 1,
        .title = "Pickup 3/4",
        .composer_source = "Test",
        .key = KeySignature{.fifths = 0, .mode = Mode::Minor},
        .time_signature = TimeSignature{.numerator = 3, .denominator = 4},
        .target_bpm = 100.0, // 600 ms per beat
    };
    LessonMeasure p{};
    p.bar_number = 0;
    p.pickup_ticks = 480;
    p.treble_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::E, Accidental::Natural, 4, 1)}},
    };
    p.bass_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
         .is_rest = true,
         .voice = 0,
         .notes = {}},
    };
    LessonMeasure m1{};
    m1.bar_number = 1;
    m1.treble_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Half, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::A, Accidental::Natural, 4, 2)}},
    };
    m1.bass_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Half, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::A, Accidental::Natural, 2, 5)}},
    };
    s.measures = {p, m1};

    LessonEngine engine;
    engine.load_score(s, LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                      .hand_mode = LessonHandMode::HandsTogether,
                                      .tempo_step = TempoLadderStep::Target100});

    const auto demo = engine.build_demo_playback();
    REQUIRE(demo.size() == 3U);
    REQUIRE_THAT(demo[0].start_ms, WithinAbs(0.0, 0.5));
    REQUIRE_THAT(demo[0].duration_ms, WithinAbs(600.0, 0.5));
    REQUIRE_THAT(demo[1].start_ms, WithinAbs(600.0, 0.5));

    const auto st = engine.start();
    REQUIRE(st.metronome_click);
    REQUIRE(st.metronome_accent);
    REQUIRE(engine.playback_state() == LessonPlaybackState::CountIn);
    REQUIRE(engine.count_in_beats_remaining() == 2);

    const auto b2 = engine.update(600.0);
    REQUIRE(b2.metronome_click);
    REQUIRE_FALSE(b2.metronome_accent);
    REQUIRE(engine.playback_state() == LessonPlaybackState::CountIn);
    REQUIRE(engine.count_in_beats_remaining() == 1);

    const auto b3 = engine.update(600.0);
    REQUIRE(b3.metronome_click);
    REQUIRE_FALSE(b3.metronome_accent);
    REQUIRE(engine.playback_state() == LessonPlaybackState::Playing);
    REQUIRE_THAT(engine.playhead_global_beat(), WithinAbs(0.0, 1e-3));

    const auto bar1_downbeat = engine.update(600.0);
    REQUIRE(bar1_downbeat.metronome_click);
    REQUIRE(bar1_downbeat.metronome_accent);
    REQUIRE_THAT(engine.playhead_global_beat(), WithinAbs(1.0, 1e-3));
  }

  SECTION("4/4 with 8th-note pickup counts in 3.5 beats and hits Bar 1 downbeat cleanly") {
    LessonScore s{};
    s.meta = LessonMetadata{
        .id = "pickup_44_8th",
        .tier = 5,
        .order_in_tier = 1,
        .title = "Pickup 4/4",
        .composer_source = "Test",
        .key = KeySignature{.fifths = 0, .mode = Mode::Major},
        .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
        .target_bpm = 120.0, // 500 ms per beat
    };
    LessonMeasure p{};
    p.bar_number = 0;
    p.pickup_ticks = 240;
    p.treble_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::G, Accidental::Natural, 4, 1)}},
    };
    p.bass_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Eighth, .dotted = false},
         .is_rest = true,
         .voice = 0,
         .notes = {}},
    };
    LessonMeasure m1{};
    m1.bar_number = 1;
    m1.treble_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::C, Accidental::Natural, 5, 4)}},
    };
    m1.bass_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::C, Accidental::Natural, 3, 5)}},
    };
    s.measures = {p, m1};

    LessonEngine engine;
    engine.load_score(s, LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                      .hand_mode = LessonHandMode::HandsTogether,
                                      .tempo_step = TempoLadderStep::Target100});

    (void)engine.start();
    REQUIRE(engine.count_in_beats_remaining() == 4);
    for (int i = 0; i < 3; ++i) {
      const auto t = engine.update(500.0);
      REQUIRE(t.metronome_click);
      REQUIRE_FALSE(t.metronome_accent);
    }
    const auto enter_pickup = engine.update(250.0);
    REQUIRE(engine.playback_state() == LessonPlaybackState::Playing);
    REQUIRE_FALSE(enter_pickup.metronome_accent);

    const auto bar1 = engine.update(250.0);
    REQUIRE(bar1.metronome_click);
    REQUIRE(bar1.metronome_accent);
  }
}

TEST_CASE("LessonEngine ChordSkeleton uses smooth voice-leading and valid chord fingerings",
          "[lesson_engine]") {
  LessonScore s{};
  s.meta = LessonMetadata{
      .id = "skel_vl",
      .tier = 3,
      .order_in_tier = 1,
      .title = "Voice-Led Progression",
      .composer_source = "Test",
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
      .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
      .target_bpm = 90.0,
  };
  const std::array<std::pair<const char*, const char*>, 7> prog{{
      {"C", "I"},
      {"G7", "V7"},
      {"Am", "vi"},
      {"F", "IV"},
      {"Dm7", "ii7"},
      {"G7", "V7"},
      {"C", "I"},
  }};
  for (std::size_t i = 0; i < prog.size(); ++i) {
    LessonMeasure m{};
    m.bar_number = static_cast<std::uint32_t>(i + 1U);
    m.treble_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::C, Accidental::Natural, 4, 1)}},
    };
    m.bass_events = {
        {.start_tick = 0,
         .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(NoteLetter::C, Accidental::Natural, 3, 5)}},
    };
    m.harmony = {HarmonicAnnotation{
        .start_tick = 0, .chord_symbol = prog[i].first, .roman_numeral = prog[i].second}};
    s.measures.push_back(std::move(m));
  }

  LessonEngine engine;
  engine.load_score(s, LessonConfig{.practice_mode = LessonPracticeMode::ChordSkeleton,
                                    .hand_mode = LessonHandMode::HandsTogether});
  REQUIRE(engine.display_measures().size() == 7U);

  double prev_centroid = 0.0;
  double total_jump = 0.0;
  std::size_t jump_count = 0;
  for (std::size_t i = 0; i < engine.display_measures().size(); ++i) {
    const auto& tr = engine.display_measures()[i].treble_events;
    REQUIRE(tr.size() == 1U);
    const auto& notes = tr[0].notes;
    REQUIRE(notes.size() >= 3U);
    REQUIRE(notes.size() <= 4U);

    int min_m = 127;
    int max_m = 0;
    double sum_m = 0.0;
    std::uint8_t prev_f = 0;
    for (const auto& n : notes) {
      REQUIRE(n.finger >= 1U);
      REQUIRE(n.finger <= 5U);
      REQUIRE(n.finger > prev_f);
      prev_f = n.finger;
      min_m = std::min<int>(min_m, n.midi);
      max_m = std::max<int>(max_m, n.midi);
      sum_m += static_cast<double>(n.midi);
    }
    REQUIRE(max_m - min_m <= 12);

    const double centroid = sum_m / static_cast<double>(notes.size());
    if (i > 0U) {
      total_jump += std::abs(centroid - prev_centroid);
      ++jump_count;
    }
    prev_centroid = centroid;
  }

  REQUIRE(jump_count == 6U);
  REQUIRE(total_jump / static_cast<double>(jump_count) <= 4.0);
}

TEST_CASE("LessonEngine DemoPlaying transport streams notes, active keys, and loops/stops cleanly",
          "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{.practice_mode = LessonPracticeMode::WaitForNote,
                                 .hand_mode = LessonHandMode::HandsTogether,
                                 .tempo_step = TempoLadderStep::Target100});

  const auto start_ev = engine.start_demo();
  REQUIRE(engine.playback_state() == LessonPlaybackState::DemoPlaying);
  // Beat 0 triggers bass C3 (48) and treble E4 (64) immediately
  REQUIRE(start_ev.playback_notes.size() == 2U);
  REQUIRE(engine.active_demo_midi_notes() == std::vector<MidiPitch>{48, 64});
  REQUIRE_THAT(engine.playhead_global_beat(), WithinAbs(0.0, 1e-6));

  // Advance 2 beats (1200 ms at 100 BPM): E4 ends, G4 (67) enters, C3 still rings!
  const auto mid = engine.update(1200.0);
  REQUIRE(mid.playback_notes.size() == 1U);
  REQUIRE(mid.playback_notes[0].midi == 67);
  REQUIRE_THAT(mid.playback_notes[0].duration_ms, WithinAbs(2400.0, 1.0));
  REQUIRE(engine.active_demo_midi_notes() == std::vector<MidiPitch>{48, 67});
  REQUIRE_THAT(engine.playhead_global_beat(), WithinAbs(2.0, 1e-3));

  // stop_demo stops immediately and clears active notes
  engine.stop_demo();
  REQUIRE(engine.playback_state() == LessonPlaybackState::Stopped);
  REQUIRE(engine.active_demo_midi_notes().empty());

  // Looped demo wraps cleanly back to beat 0 and replays beat-0 notes
  engine.set_loop_phrase(true);
  (void)engine.start_demo();
  (void)engine.update(2400.0); // bar 2 tick 0 -> G2 (43)
  const auto wrap = engine.update(2450.0);
  REQUIRE(engine.playback_state() == LessonPlaybackState::DemoPlaying);
  bool found_c3 = false;
  for (const auto& n : wrap.playback_notes) {
    if (n.midi == 48U) {
      found_c3 = true;
    }
  }
  REQUIRE(found_c3);
}

TEST_CASE("LessonEngine SteadyPulse Auto-Play Other Hand emits accompaniment for RH/LH practice",
          "[lesson_engine]") {
  LessonEngine engine;
  engine.load_score(make_sample_lesson(),
                    LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                 .hand_mode = LessonHandMode::RightHand,
                                 .tempo_step = TempoLadderStep::Target100,
                                 .play_other_hand_accomp = true});

  (void)engine.start();
  // Count-in: 3 beats should not emit accompaniment yet
  for (int i = 0; i < 3; ++i) {
    const auto ci = engine.update(600.0);
    REQUIRE(ci.playback_notes.empty());
  }
  // Finishing beat 4 of count-in transitions to Playing at tick 0 -> emits LH C3 (48) at vel ~64
  const auto enter = engine.update(600.0);
  REQUIRE(engine.playback_state() == LessonPlaybackState::Playing);
  REQUIRE(enter.playback_notes.size() == 1U);
  REQUIRE(enter.playback_notes[0].midi == 48);
  REQUIRE(enter.playback_notes[0].velocity == 64);

  // Advance 4 beats into Bar 2 -> emits LH G2 (43)
  (void)engine.update(1200.0);
  const auto b2 = engine.update(1200.0);
  REQUIRE(b2.playback_notes.size() == 1U);
  REQUIRE(b2.playback_notes[0].midi == 43);

  // Disabling accompaniment suppresses further notes
  LessonEngine muted;
  muted.load_score(make_sample_lesson(),
                   LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                .hand_mode = LessonHandMode::RightHand,
                                .tempo_step = TempoLadderStep::Target100,
                                .play_other_hand_accomp = false});
  (void)muted.start();
  for (int i = 0; i < 6; ++i) {
    REQUIRE(muted.update(600.0).playback_notes.empty());
  }
}

TEST_CASE("LessonProgress records per-phrase masks and suggest_next_practice_step guides sequence",
          "[lesson_engine]") {
  LessonScore s = make_sample_lesson();
  s.meta.theory.concept_title = "Two-Bar Cadence Concept";
  LessonProgress prog{};

  // 1. Brief unread -> Read Brief & Listen
  auto step = suggest_next_practice_step(s, prog);
  REQUIRE(step.open_learn_brief);
  REQUIRE(step.milestone_index == 0);

  // 2. Brief read -> Chord Skeleton Map
  prog.brief_read = true;
  step = suggest_next_practice_step(s, prog);
  REQUIRE_FALSE(step.open_learn_brief);
  REQUIRE(step.milestone_index == 1);
  REQUIRE(step.target_config.practice_mode == LessonPracticeMode::ChordSkeleton);

  // Clear Chord Skeleton -> Phrase A (RH)
  (void)apply_run_to_progress(prog,
                              LessonRunSummary{.lesson_id = "sample_01",
                                               .practice_mode = LessonPracticeMode::ChordSkeleton,
                                               .hand_mode = LessonHandMode::HandsTogether,
                                               .is_full_piece = true,
                                               .effective_bpm = 60.0,
                                               .accuracy = 0.90,
                                               .hits = 2,
                                               .misses = 0},
                              100.0, 20000);
  REQUIRE(prog.skeleton_clean);
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.milestone_index == 2);
  REQUIRE(step.target_config.phrase_index == 0U);
  REQUIRE(step.target_config.hand_mode == LessonHandMode::RightHand);

  // Clear Phrase A RH -> Phrase A LH -> Phrase A Both -> Phrase B RH
  (void)apply_run_to_progress(prog,
                              LessonRunSummary{.lesson_id = "sample_01",
                                               .practice_mode = LessonPracticeMode::WaitForNote,
                                               .hand_mode = LessonHandMode::RightHand,
                                               .is_full_piece = false,
                                               .effective_bpm = 60.0,
                                               .accuracy = 0.95,
                                               .hits = 4,
                                               .misses = 0,
                                               .phrase_index = 0U},
                              100.0, 20000);
  REQUIRE((prog.phrase_rh_mask & 1U) != 0U);
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.target_config.phrase_index == 0U);
  REQUIRE(step.target_config.hand_mode == LessonHandMode::LeftHand);

  (void)apply_run_to_progress(prog,
                              LessonRunSummary{.lesson_id = "sample_01",
                                               .practice_mode = LessonPracticeMode::WaitForNote,
                                               .hand_mode = LessonHandMode::LeftHand,
                                               .is_full_piece = false,
                                               .effective_bpm = 60.0,
                                               .accuracy = 0.95,
                                               .hits = 4,
                                               .misses = 0,
                                               .phrase_index = 0U},
                              100.0, 20000);
  REQUIRE((prog.phrase_lh_mask & 1U) != 0U);
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.target_config.phrase_index == 0U);
  REQUIRE(step.target_config.hand_mode == LessonHandMode::HandsTogether);

  // Clear Phrase A Both & Phrase B Both -> Full Piece at 60%
  (void)apply_run_to_progress(prog,
                              LessonRunSummary{.lesson_id = "sample_01",
                                               .practice_mode = LessonPracticeMode::WaitForNote,
                                               .hand_mode = LessonHandMode::HandsTogether,
                                               .is_full_piece = false,
                                               .effective_bpm = 60.0,
                                               .accuracy = 1.0,
                                               .hits = 4,
                                               .misses = 0,
                                               .phrase_index = 0U},
                              100.0, 20000);
  (void)apply_run_to_progress(prog,
                              LessonRunSummary{.lesson_id = "sample_01",
                                               .practice_mode = LessonPracticeMode::WaitForNote,
                                               .hand_mode = LessonHandMode::HandsTogether,
                                               .is_full_piece = false,
                                               .effective_bpm = 60.0,
                                               .accuracy = 1.0,
                                               .hits = 4,
                                               .misses = 0,
                                               .phrase_index = 1U},
                              100.0, 20000);
  REQUIRE((prog.phrase_both_mask & 3U) == 3U);

  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.milestone_index == 3);
  REQUIRE(step.target_config.tempo_step == TempoLadderStep::Slow60);
  REQUIRE(step.target_config.practice_mode == LessonPracticeMode::SteadyPulse);

  prog.ladder_60_clean = true;
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.target_config.tempo_step == TempoLadderStep::Medium80);

  prog.ladder_80_clean = true;
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.target_config.tempo_step == TempoLadderStep::Target100);

  prog.ladder_100_clean = true;
  prog.status = LessonMasteryStatus::Mastered;
  step = suggest_next_practice_step(s, prog);
  REQUIRE(step.milestone_index == 4);
}

TEST_CASE("LessonEngine supports custom_bar_range across phrase seams with safe clamping",
          "[lesson_engine]") {
  LessonScore four_bar = make_sample_lesson();
  // Expand to 4 bars: Phrase A (0-1), Phrase B (2-3)
  LessonMeasure m3 = four_bar.measures[0];
  m3.bar_number = 3;
  m3.treble_events[0].notes[0].tie_stop = false;
  m3.treble_events[1].notes[0].tie_start = false;
  LessonMeasure m4 = four_bar.measures[1];
  m4.bar_number = 4;
  m4.treble_events[0].notes[0].tie_stop = false;
  four_bar.measures[1].treble_events[0].notes[0].tie_stop = true;
  four_bar.measures.push_back(m3);
  four_bar.measures.push_back(m4);
  four_bar.phrases = {
      LessonPhrase{.label = "Phrase A", .start_bar = 0, .end_bar = 1},
      LessonPhrase{.label = "Phrase B", .start_bar = 2, .end_bar = 3},
  };

  LessonEngine engine;
  engine.load_score(four_bar);

  // Set seam across Phrase A -> Phrase B (bars 1..2, reversed args {2, 1} + out-of-range test)
  engine.set_custom_bar_range(std::pair{2U, 1U});
  REQUIRE(engine.slice_start_bar() == 1U);
  REQUIRE(engine.slice_end_bar() == 2U);
  REQUIRE_FALSE(engine.is_full_piece_selected());
  REQUIRE_FALSE(engine.config().phrase_index.has_value());
  for (const auto& t : engine.targets()) {
    REQUIRE(t.bar_index >= 1U);
    REQUIRE(t.bar_index <= 2U);
  }
  REQUIRE_FALSE(engine.build_demo_playback().empty());

  // Clamp out-of-range upper bound safely
  engine.set_custom_bar_range(std::pair{3U, 99U});
  REQUIRE(engine.slice_start_bar() == 3U);
  REQUIRE(engine.slice_end_bar() == 3U);

  // Selecting preset phrase or Full Piece clears custom_bar_range
  engine.set_phrase_index(0U);
  REQUIRE_FALSE(engine.config().custom_bar_range.has_value());
  REQUIRE(engine.slice_start_bar() == 0U);
  REQUIRE(engine.slice_end_bar() == 1U);
}

TEST_CASE("LessonEngine SteadyPulse grades PartialHit chords while WaitForNote stays strict",
          "[lesson_engine]") {
  LessonScore s = make_sample_lesson();
  s.measures.resize(1);
  s.phrases = {LessonPhrase{.label = "Phrase A", .start_bar = 0, .end_bar = 0}};
  s.measures[0].treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes =
           {
               ln(NoteLetter::C, Accidental::Natural, 4, 1),
               ln(NoteLetter::E, Accidental::Natural, 4, 2),
               ln(NoteLetter::G, Accidental::Natural, 4, 3),
               ln(NoteLetter::B, Accidental::Flat, 4, 5),
           }},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .notes =
           {
               ln(NoteLetter::F, Accidental::Natural, 4, 1),
               ln(NoteLetter::A, Accidental::Natural, 4, 3),
               ln(NoteLetter::C, Accidental::Natural, 5, 5),
           }},
  };
  s.measures[0].bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
       .is_rest = true,
       .voice = 0,
       .notes = {}},
  };

  LessonEngine engine;
  engine.load_score(s, LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                    .hand_mode = LessonHandMode::RightHand,
                                    .tempo_step = TempoLadderStep::Target100});
  (void)engine.start();
  for (int i = 0; i < 4; ++i) {
    (void)engine.update(600.0);
  }
  REQUIRE(engine.playback_state() == LessonPlaybackState::Playing);

  // Hit 3 of 4 notes of C7 (omit Bb4=70), 0 wrong notes -> PartialHit!
  const auto p1 = engine.submit_played_notes(std::array<MidiPitch, 3>{60, 64, 67});
  REQUIRE(p1.partial_match);
  REQUIRE_FALSE(p1.matched);
  REQUIRE_FALSE(p1.wrong_note);
  REQUIRE(engine.partial_hit_count() == 1U);
  REQUIRE(engine.targets()[0].status == LessonTargetStatus::PartialHit);
  REQUIRE(engine.targets()[0].missed_midi == std::vector<MidiPitch>{70});
  REQUIRE_THAT(engine.accuracy(), WithinAbs(0.5, 1e-6));

  // Advance to beat 3 (tick 960 -> F4, A4, C5) and play wrong note -> Missed!
  (void)engine.update(1200.0);
  const auto bad = engine.submit_played_notes(std::array<MidiPitch, 3>{65, 68, 72}); // Ab4 wrong
  REQUIRE(bad.wrong_note);
  REQUIRE_FALSE(bad.partial_match);
  REQUIRE(engine.targets()[1].status == LessonTargetStatus::Missed);

  // WaitForNote stays strict: 3 of 4 notes triggers wrong_note, never PartialHit
  engine.set_practice_mode(LessonPracticeMode::WaitForNote);
  const auto strict_res = engine.submit_played_notes(std::array<MidiPitch, 3>{60, 64, 67});
  REQUIRE(strict_res.wrong_note);
  REQUIRE_FALSE(strict_res.partial_match);
}

TEST_CASE("find_trouble_spot detects 1-bar and 2-bar seam hotspots accurately", "[lesson_engine]") {
  LessonRunSummary clean{};
  clean.bar_diagnostics = {
      {.bar_index = 0, .bar_number = 1, .hits = 4, .partial_hits = 0, .misses = 0},
      {.bar_index = 1, .bar_number = 2, .hits = 4, .partial_hits = 0, .misses = 0},
  };
  REQUIRE_FALSE(find_trouble_spot(clean).has_value());

  LessonRunSummary seam{};
  seam.bar_diagnostics = {
      {.bar_index = 2, .bar_number = 3, .hits = 4, .partial_hits = 0, .misses = 0},
      {.bar_index = 3, .bar_number = 4, .hits = 1, .partial_hits = 1, .misses = 2},
      {.bar_index = 4, .bar_number = 5, .hits = 1, .partial_hits = 0, .misses = 2},
      {.bar_index = 5, .bar_number = 6, .hits = 3, .partial_hits = 1, .misses = 0},
  };
  const auto spot = find_trouble_spot(seam);
  REQUIRE(spot.has_value());
  REQUIRE(spot->start_bar_index == 3U);
  REQUIRE(spot->end_bar_index == 4U);
  REQUIRE(spot->start_bar_number == 4U);
  REQUIRE(spot->end_bar_number == 5U);
  REQUIRE(spot->misses == 4U);
  REQUIRE(spot->partial_hits == 1U);
}

TEST_CASE("LessonEngine build_demo_playback applies dynamics, staccato, accent, and arpeggio roll",
          "[lesson_engine]") {
  auto ln = [](NoteLetter l, Accidental a, int oct) {
    const SpelledNote sp{.letter = l, .accidental = a, .octave = oct};
    return LessonNote{.spelled = sp, .midi = to_midi(sp).value_or(60), .finger = 1};
  };

  LessonScore s{};
  s.meta = LessonMetadata{
      .id = "exp_demo",
      .tier = 5,
      .order_in_tier = 1,
      .title = "Expressive Test",
      .composer_source = "Test",
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
      .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
      .target_bpm = 120.0, // 500ms per quarter beat
      .tempo_marking = "Allegro",
  };
  LessonMeasure m{};
  m.bar_number = 1;
  m.dynamics = {
      {.start_tick = 0, .level = DynamicLevel::PP},
      {.start_tick = 480, .level = DynamicLevel::FF},
  };
  m.treble_events = {
      // Beat 1 (pp + staccato -> ~46 vel, ~225ms)
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .articulations = articulation_bit(ArticulationFlag::Staccato),
       .notes = {ln(NoteLetter::C, Accidental::Natural, 5)}},
      // Beat 2 (ff + accent -> >114 vel, 500ms)
      {.start_tick = 480,
       .duration = {.base = NoteBaseDuration::Quarter, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .articulations = articulation_bit(ArticulationFlag::Accent),
       .notes = {ln(NoteLetter::G, Accidental::Natural, 5)}},
      // Beats 3-4 (ff + arpeggiate 3-note chord -> rolled ~35ms stagger)
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .voice = 0,
       .articulations = articulation_bit(ArticulationFlag::Arpeggiate),
       .notes =
           {
               ln(NoteLetter::C, Accidental::Natural, 5),
               ln(NoteLetter::E, Accidental::Natural, 5),
               ln(NoteLetter::G, Accidental::Natural, 5),
           }},
  };
  m.bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Whole, .dotted = false},
       .is_rest = true,
       .voice = 0,
       .notes = {}},
  };
  s.measures.push_back(std::move(m));

  LessonEngine engine;
  engine.load_score(s, LessonConfig{.practice_mode = LessonPracticeMode::WaitForNote,
                                    .hand_mode = LessonHandMode::RightHand,
                                    .tempo_step = TempoLadderStep::Target100});
  const auto notes = engine.build_demo_playback();
  REQUIRE(notes.size() == 5U);

  // Beat 1: PP + Staccato
  REQUIRE(notes[0].midi == 72);
  REQUIRE(notes[0].velocity <= 50U);
  REQUIRE_THAT(notes[0].duration_ms, WithinAbs(225.0, 2.0));

  // Beat 2: FF + Accent
  REQUIRE(notes[1].midi == 79);
  REQUIRE(notes[1].velocity >= 120U);
  REQUIRE_THAT(notes[1].start_ms, WithinAbs(500.0, 1.0));
  REQUIRE_THAT(notes[1].duration_ms, WithinAbs(500.0, 1.0));

  // Beats 3-4: 3-note rolled Arpeggio (C5, E5, G5) staggered +35ms each
  REQUIRE(notes[2].midi == 72);
  REQUIRE(notes[3].midi == 76);
  REQUIRE(notes[4].midi == 79);
  REQUIRE_THAT(notes[2].start_ms, WithinAbs(1000.0, 1.0));
  REQUIRE_THAT(notes[3].start_ms, WithinAbs(1035.0, 1.0));
  REQUIRE_THAT(notes[4].start_ms, WithinAbs(1070.0, 1.0));
  REQUIRE(notes[4].duration_ms < notes[2].duration_ms);
}

namespace {

LessonNote ln_tied(NoteLetter letter, Accidental acc, int octave, bool tie_start, bool tie_stop) {
  auto n = ln(letter, acc, octave);
  n.tie_start = tie_start;
  n.tie_stop = tie_stop;
  return n;
}

LessonScore make_phase16_two_hand_score() {
  LessonScore s{};
  s.meta = LessonMetadata{
      .id = "p16_voicing",
      .tier = 2,
      .order_in_tier = 1,
      .title = "Voicing & Pedal Test",
      .composer_source = "Unit",
      .key = KeySignature{.fifths = 0, .mode = Mode::Major},
      .time_signature = TimeSignature{.numerator = 4, .denominator = 4},
      .target_bpm = 120.0,
      .tempo_marking = "Moderato",
  };
  LessonMeasure m1{};
  m1.bar_number = 1;
  m1.treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .notes = {ln_tied(NoteLetter::G, Accidental::Natural, 5, true, false)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .notes = {ln_tied(NoteLetter::G, Accidental::Natural, 5, false, true)}},
  };
  m1.bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .notes = {ln(NoteLetter::C, Accidental::Natural, 3)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half, .dotted = false},
       .is_rest = false,
       .notes = {ln(NoteLetter::G, Accidental::Natural, 3)}},
  };
  s.measures.push_back(std::move(m1));
  return s;
}

void advance_past_count_in(LessonEngine& eng) {
  for (int i = 0; i < 40 && eng.playback_state() == LessonPlaybackState::CountIn; ++i) {
    (void)eng.update(200.0);
  }
}

} // namespace

TEST_CASE(
    "Phase 16: Full 2-bar voicing, sustain hold, damper pedal cleanliness & non-punitive guarantee",
    "[lesson_engine][phase16]") {
  auto score = make_phase16_two_hand_score();
  score.measures.push_back(score.measures.front());
  score.measures[1].bar_number = 2;
  score.pedals = {
      PedalSpan{.start_bar = 0, .start_tick = 0, .end_bar = 0, .end_tick = 1440},
      PedalSpan{.start_bar = 1, .start_tick = 0, .end_bar = 1, .end_tick = 1440},
  };

  LessonEngine engine;
  engine.load_score(score, LessonConfig{.practice_mode = LessonPracticeMode::SteadyPulse,
                                        .hand_mode = LessonHandMode::HandsTogether,
                                        .tempo_step = TempoLadderStep::Target100});

  auto run_once = [&](bool hw, std::uint8_t mel_vel, std::uint8_t acc_vel, bool hold_long,
                      bool lift_pedal_cleanly, bool use_pedal) {
    (void)engine.start();
    advance_past_count_in(engine);
    bool completed = false;
    std::vector<MidiPitch> active_notes;
    for (std::size_t ti = 0; ti < engine.targets().size(); ++ti) {
      const auto& t = engine.targets()[ti];
      const double target_tick = static_cast<double>(t.global_tick);
      while (engine.elapsed_ticks() + 1e-3 < target_tick &&
             engine.playback_state() == LessonPlaybackState::Playing) {
        (void)engine.update(25.0);
      }
      if (use_pedal && t.tick_in_bar == 0) {
        engine.on_damper_pedal(true, hw);
      }
      // Release notes that were NOT tied across this boundary; tied notes stay down when hold_long!
      for (const auto p : active_notes) {
        if (!hold_long || std::ranges::find(t.expected_midi, p) != t.expected_midi.end()) {
          engine.on_note_off(p);
        }
      }
      std::vector<std::uint8_t> vels;
      for (const auto p : t.expected_midi) {
        const bool is_rh = std::ranges::find(t.treble_midi, p) != t.treble_midi.end();
        vels.push_back(is_rh ? mel_vel : acc_vel);
      }
      const auto sr = engine.submit_played_notes(t.expected_midi, vels, hw);
      REQUIRE(sr.matched);
      active_notes = t.expected_midi;

      // Advance ~1650 ticks (~1700ms at 120bpm) for whole tied notes vs ~120 ticks for chopped
      const int hold_steps = hold_long ? 34 : 3;
      for (int k = 0; k < hold_steps && engine.playback_state() == LessonPlaybackState::Playing;
           ++k) {
        (void)engine.update(25.0);
        if (use_pedal && lift_pedal_cleanly &&
            ((engine.elapsed_ticks() >= 1420.0 && engine.elapsed_ticks() <= 1520.0) ||
             (engine.elapsed_ticks() >= 3340.0 && engine.elapsed_ticks() <= 3450.0))) {
          engine.on_damper_pedal(false, hw);
        }
      }
      if (!hold_long) {
        for (const auto p : active_notes) {
          engine.on_note_off(p);
        }
        active_notes.clear();
      } else {
        // Release half notes in bass while keeping tied whole G5 down if we are still in beat 1->3
        for (const auto p : t.bass_midi) {
          engine.on_note_off(p);
        }
      }
    }
    for (int i = 0; i < 200 && engine.playback_state() == LessonPlaybackState::Playing; ++i) {
      const auto tk = engine.update(25.0);
      if (use_pedal && lift_pedal_cleanly &&
          ((engine.elapsed_ticks() >= 1420.0 && engine.elapsed_ticks() <= 1520.0) ||
           (engine.elapsed_ticks() >= 3340.0 && engine.elapsed_ticks() <= 3450.0))) {
        engine.on_damper_pedal(false, hw);
      }
      if (tk.phrase_completed) {
        completed = true;
        break;
      }
    }
    for (const auto p : active_notes) {
      engine.on_note_off(p);
    }
    if (use_pedal) {
      engine.on_damper_pedal(false, hw);
    }
    REQUIRE(completed);
    return engine.build_run_summary();
  };

  // A. Balanced expressive hardware performance + clean sustain + clean pedal
  const auto clean = run_once(true, 96U, 60U, true, true, true);
  REQUIRE_THAT(clean.accuracy, WithinAbs(1.0, 1e-6));
  REQUIRE(clean.musicality.voicing_available);
  REQUIRE(clean.musicality.voicing_balanced);
  REQUIRE(clean.musicality.voicing_delta >= 30.0);
  REQUIRE(clean.musicality.sustain_available);
  REQUIRE(clean.musicality.sustain_hold_rate >= 0.75);
  REQUIRE(clean.musicality.pedal_available);
  REQUIRE(clean.musicality.pedal_clean_rate >= 0.75);
  REQUIRE(clean.musicality.coach_tip.empty());

  // B. Inverted voicing (LH louder than RH) + smeared pedal -> NEVER punishes accuracy/mastery!
  const auto rough = run_once(true, 62U, 95U, true, false, true);
  REQUIRE_THAT(rough.accuracy, WithinAbs(1.0, 1e-6));
  REQUIRE(rough.musicality.voicing_available);
  REQUIRE(!rough.musicality.voicing_balanced);
  REQUIRE(rough.musicality.voicing_delta < 0.0);
  REQUIRE(rough.musicality.pedal_available);
  REQUIRE(rough.musicality.pedal_clean_rate < 0.75);
  REQUIRE(!rough.musicality.coach_tip.empty());

  // C. Chopped dry sustain (no damper pedal compensating) -> drops sustain rate, stays non-punitive
  const auto chopped_dry = run_once(true, 96U, 60U, false, false, false);
  REQUIRE_THAT(chopped_dry.accuracy, WithinAbs(1.0, 1e-6));
  REQUIRE(!chopped_dry.musicality.pedal_available);
  REQUIRE(chopped_dry.musicality.sustain_available);
  REQUIRE(chopped_dry.musicality.sustain_hold_rate < 0.75);
  REQUIRE(!chopped_dry.musicality.coach_tip.empty());

  LessonProgress prog_clean{};
  LessonProgress prog_rough{};
  LessonProgress prog_chop{};
  const auto upd_clean = apply_run_to_progress(prog_clean, clean, score.meta.target_bpm, 20000);
  const auto upd_rough = apply_run_to_progress(prog_rough, rough, score.meta.target_bpm, 20000);
  const auto upd_chop = apply_run_to_progress(prog_chop, chopped_dry, score.meta.target_bpm, 20000);
  REQUIRE(upd_rough.just_mastered == upd_clean.just_mastered);
  REQUIRE(upd_rough.suggested_rating == upd_clean.suggested_rating);
  REQUIRE(upd_chop.just_mastered == upd_clean.just_mastered);
  REQUIRE(upd_chop.suggested_rating == upd_clean.suggested_rating);
  REQUIRE(prog_rough == prog_clean);
  REQUIRE(prog_chop == prog_clean);

  // D. Virtual keyboard (from_hardware = false, flat velocity, no pedal): gracefully omits voicing
  // & pedal
  const auto virt = run_once(false, 96U, 96U, true, false, false);
  REQUIRE_THAT(virt.accuracy, WithinAbs(1.0, 1e-6));
  REQUIRE(!virt.musicality.voicing_available);
  REQUIRE(!virt.musicality.pedal_available);
  REQUIRE(virt.musicality.sustain_available);
  REQUIRE(virt.musicality.sustain_hold_rate >= 0.75);
}

TEST_CASE("summarize_lesson_progress tracks brief, skeleton, phrases, ladder, and mastery",
          "[lesson_engine]") {
  auto score = make_sample_lesson();
  score.meta.theory.concept_title = "Sample Concept";

  LessonProgress prog{};
  // 1. New (unread brief)
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK_FALSE(s.brief_done);
    CHECK(s.skeleton_applicable);
    CHECK_FALSE(s.skeleton_done);
    CHECK(s.phrases_cleared == 0U);
    CHECK(s.phrases_total == 2U);
    CHECK(s.ladder_cleared == 0U);
    CHECK(s.milestone_index == 0U);
    CHECK(s.milestone_label == "Step 1/4 · Brief");
  }

  // 2. Brief read -> ChordSkeleton pending
  prog.brief_read = true;
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK(s.brief_done);
    CHECK_FALSE(s.skeleton_done);
    CHECK(s.milestone_index == 1U);
    CHECK(s.milestone_label == "Step 2/4 · Chord shapes");
  }

  // 3. Skeleton clean -> Phrases 0/2 -> 1/2 -> 2/2
  prog.skeleton_clean = true;
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK(s.skeleton_done);
    CHECK(s.phrases_cleared == 0U);
    CHECK(s.milestone_index == 2U);
    CHECK(s.milestone_label == "Step 3/4 · Phrases 0/2");
  }

  prog.phrase_both_mask = 1U; // Phrase 0 cleared
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK(s.phrases_cleared == 1U);
    CHECK(s.milestone_index == 2U);
    CHECK(s.milestone_label == "Step 3/4 · Phrases 1/2");
  }

  // Solo-hand phrase check: make Phrase 1 treble-only and clear via RH bit
  score.measures[1].bass_events.clear();
  prog.phrase_rh_mask = 2U;
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK(s.phrases_cleared == 2U);
    CHECK(s.ladder_cleared == 0U);
    CHECK(s.milestone_index == 3U);
    CHECK(s.milestone_label == "Step 4/4 · Tempo 0/3");
  }

  // 4. Tempo ladder 60% -> 80% -> 100% / Mastered
  prog.ladder_60_clean = true;
  CHECK(summarize_lesson_progress(score, prog).ladder_cleared == 1U);
  CHECK(summarize_lesson_progress(score, prog).milestone_label == "Step 4/4 · Tempo 1/3");

  prog.ladder_80_clean = true;
  CHECK(summarize_lesson_progress(score, prog).ladder_cleared == 2U);
  CHECK(summarize_lesson_progress(score, prog).milestone_label == "Step 4/4 · Tempo 2/3");

  prog.ladder_100_clean = true;
  prog.status = LessonMasteryStatus::Mastered;
  {
    const auto s = summarize_lesson_progress(score, prog);
    CHECK(s.ladder_cleared == 3U);
    CHECK(s.milestone_index == 4U);
    CHECK(s.milestone_label == "Mastered");
  }

  // 5. Imported score without harmony skips brief and skeleton cleanly
  LessonScore imported = make_sample_lesson();
  imported.meta.is_imported = true;
  imported.meta.theory.concept_title.clear();
  for (auto& m : imported.measures) {
    m.harmony.clear();
  }
  {
    const auto s = summarize_lesson_progress(imported, LessonProgress{});
    CHECK(s.brief_done);
    CHECK_FALSE(s.skeleton_applicable);
    CHECK_FALSE(s.skeleton_done);
    CHECK(s.phrases_cleared == 0U);
    CHECK(s.phrases_total == 2U);
    CHECK(s.milestone_index == 2U);
  }
}

TEST_CASE("LessonEngine micro-increment tempo ramp steps up, caps, and steps down on slips",
          "[lesson_engine][phase21]") {
  LessonScore score = make_sample_lesson(); // target_bpm = 100.0, 2 bars
  LessonEngine engine;

  auto advance_past_count_in = [&]() {
    while (engine.playback_state() == LessonPlaybackState::CountIn) {
      (void)engine.update(100.0);
    }
  };

  auto play_loop_pass = [&](bool play_all_clean, bool skip_all) -> LessonTickResult {
    const double ms_per_beat = 60000.0 / engine.effective_bpm();
    const double ms_per_tick = ms_per_beat / static_cast<double>(kTicksPerQuarter);
    const std::size_t n_targets = engine.targets().size();
    std::vector<bool> played(n_targets, false);
    LessonTickResult last_tick{};

    for (int step = 0; step < 400; ++step) {
      const double cur_tick = engine.elapsed_ticks();
      if (!skip_all) {
        for (std::size_t i = 0; i < n_targets; ++i) {
          if (played[i]) {
            continue;
          }
          const auto& tgt = engine.targets()[i];
          if (cur_tick + 20.0 >= static_cast<double>(tgt.global_tick)) {
            played[i] = true;
            if (play_all_clean || i < 2U) {
              (void)engine.submit_played_notes(tgt.expected_midi);
            } else {
              const std::array<MidiPitch, 1> wrong{21};
              (void)engine.submit_played_notes(wrong);
            }
          }
        }
      }
      last_tick = engine.update(ms_per_tick * 120.0);
      if (last_tick.phrase_completed) {
        break;
      }
    }
    return last_tick;
  };

  SECTION("Auto-ramp OFF leaves BPM unchanged on clean and slip loops") {
    LessonConfig cfg{};
    cfg.practice_mode = LessonPracticeMode::SteadyPulse;
    cfg.hand_mode = LessonHandMode::HandsTogether;
    cfg.tempo_step = TempoLadderStep::Slow60; // 60 BPM
    cfg.loop_phrase = true;
    cfg.auto_tempo_ramp = false;
    engine.load_score(score, cfg);
    REQUIRE(engine.effective_bpm() == Catch::Approx(60.0));

    (void)engine.start();
    advance_past_count_in();
    const auto t1 = play_loop_pass(true, false);
    REQUIRE(t1.phrase_completed);
    CHECK(t1.tempo_ramped_delta == Catch::Approx(0.0));
    const auto t2 = play_loop_pass(true, false);
    REQUIRE(t2.phrase_completed);
    CHECK(t2.tempo_ramped_delta == Catch::Approx(0.0));
    CHECK(engine.effective_bpm() == Catch::Approx(60.0));
    CHECK(engine.consecutive_clean_loops() == 0);
  }

  SECTION("Auto-ramp steps +4 BPM after 2 clean loops, caps at target, and steps down on slips") {
    LessonConfig cfg{};
    cfg.practice_mode = LessonPracticeMode::SteadyPulse;
    cfg.hand_mode = LessonHandMode::HandsTogether;
    cfg.tempo_step = TempoLadderStep::Slow60; // 60 BPM
    cfg.loop_phrase = true;
    cfg.auto_tempo_ramp = true;
    cfg.ramp_step_bpm = 12.0; // Should clamp to [2.0, 8.0] -> 8.0
    engine.load_score(score, cfg);
    CHECK(engine.config().ramp_step_bpm == Catch::Approx(8.0));

    engine.set_ramp_step_bpm(4.0);
    CHECK(engine.config().ramp_step_bpm == Catch::Approx(4.0));
    REQUIRE(engine.effective_bpm() == Catch::Approx(60.0));

    (void)engine.start();
    advance_past_count_in();

    // 1st clean loop: banks streak = 1, no ramp yet
    const auto l1 = play_loop_pass(true, false);
    REQUIRE(l1.phrase_completed);
    CHECK(l1.loop_accuracy >= 0.95);
    CHECK(l1.tempo_ramped_delta == Catch::Approx(0.0));
    CHECK(engine.consecutive_clean_loops() == 1);
    CHECK(engine.effective_bpm() == Catch::Approx(60.0));

    // 2nd clean loop: ramps +4 BPM -> 64 BPM, streak resets to 0
    const auto l2 = play_loop_pass(true, false);
    REQUIRE(l2.phrase_completed);
    CHECK(l2.tempo_ramped_delta == Catch::Approx(4.0));
    CHECK(engine.consecutive_clean_loops() == 0);
    CHECK(engine.config().tempo_step == TempoLadderStep::Custom);
    CHECK(engine.effective_bpm() == Catch::Approx(64.0));

    // Slip loop (< 80% accuracy / >= 2 misses): steps down -4 BPM -> 60 BPM
    const auto l3 = play_loop_pass(false, true);
    REQUIRE(l3.phrase_completed);
    CHECK(l3.loop_accuracy < 0.80);
    CHECK(l3.tempo_ramped_delta == Catch::Approx(-4.0));
    CHECK(engine.consecutive_clean_loops() == 0);
    CHECK(engine.effective_bpm() == Catch::Approx(60.0));

    // Floor check: at 50% of 100 BPM (= 50 BPM), another slip stays at 50 BPM
    engine.set_tempo_step(TempoLadderStep::Custom, 52.0);
    const auto l4 = play_loop_pass(false, true);
    REQUIRE(l4.phrase_completed);
    CHECK(l4.tempo_ramped_delta == Catch::Approx(-2.0));
    CHECK(engine.effective_bpm() == Catch::Approx(50.0));
    const auto l5 = play_loop_pass(false, true);
    REQUIRE(l5.phrase_completed);
    CHECK(l5.tempo_ramped_delta == Catch::Approx(0.0));
    CHECK(engine.effective_bpm() == Catch::Approx(50.0));

    // Cap check: near target_bpm = 100.0, 98 + 4 caps at 100.0 (+2.0)
    engine.set_tempo_step(TempoLadderStep::Custom, 98.0);
    const auto c1 = play_loop_pass(true, false);
    REQUIRE(c1.phrase_completed);
    CHECK(engine.consecutive_clean_loops() == 1);
    const auto c2 = play_loop_pass(true, false);
    REQUIRE(c2.phrase_completed);
    CHECK(c2.tempo_ramped_delta == Catch::Approx(2.0));
    CHECK(engine.effective_bpm() == Catch::Approx(100.0));
  }
}

TEST_CASE("Trouble-Spot Micro-SRS queue deduplicates, schedules, and graduates",
          "[lesson_engine][phase21]") {
  std::vector<TroubleSpotCard> queue;
  const std::int64_t day0 = 20700;

  enqueue_or_update_trouble_spot(queue, "t3_04", 2, 3, 112.0, 67.0, day0);
  REQUIRE(queue.size() == 1U);
  CHECK(queue[0].lesson_id == "t3_04");
  CHECK(queue[0].start_bar_index == 2U);
  CHECK(queue[0].end_bar_index == 3U);
  CHECK(queue[0].due_epoch_day == day0 + 1);

  // Overlapping window merges instead of duplicating
  enqueue_or_update_trouble_spot(queue, "t3_04", 3, 4, 112.0, 65.0, day0);
  REQUIRE(queue.size() == 1U);
  CHECK(queue[0].start_bar_index == 2U);
  CHECK(queue[0].end_bar_index == 4U);
  CHECK(queue[0].current_clean_bpm == Catch::Approx(67.0));

  // Non-overlapping window on same lesson stays separate
  enqueue_or_update_trouble_spot(queue, "t3_04", 8, 9, 112.0, 67.0, day0);
  REQUIRE(queue.size() == 2U);

  // Due check: not due on day0, due on day0 + 1
  CHECK(due_trouble_spot_indices(queue, day0).empty());
  const auto due = due_trouble_spot_indices(queue, day0 + 1);
  REQUIRE(due.size() == 2U);

  // Clean review below target_bpm advances SM-2 interval without graduating
  const auto r1 = complete_trouble_spot_review(queue, "t3_04", 2, 4, 0.94, 80.0, day0 + 1);
  CHECK(r1.found);
  CHECK_FALSE(r1.graduated);
  CHECK(r1.next_interval_days >= 1);
  CHECK(queue[0].current_clean_bpm == Catch::Approx(80.0));
  CHECK(queue[0].due_epoch_day > day0 + 1);

  // Clean review at >= 100% target_bpm graduates (removes) the card
  const auto r2 = complete_trouble_spot_review(queue, "t3_04", 2, 4, 0.96, 112.0, day0 + 5);
  CHECK(r2.found);
  CHECK(r2.graduated);
  REQUIRE(queue.size() == 1U);
  CHECK(queue[0].start_bar_index == 8U);

  // Queue cap enforcement (kMaxTroubleSpotQueueSize)
  for (std::size_t i = 0; i < kMaxTroubleSpotQueueSize + 5U; ++i) {
    enqueue_or_update_trouble_spot(queue, "t4_02", i * 2U, i * 2U + 1U, 76.0, 46.0, day0);
  }
  CHECK(queue.size() == kMaxTroubleSpotQueueSize);
}

TEST_CASE("LessonEngine applies PracticeTransform non-destructively and guards Mastery",
          "[lesson_engine][phase22]") {
  LessonLibrary lib;
  REQUIRE(lib.load_built_in_from_directory(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) /
                                           "lessons"));
  const auto* score_ptr = lib.find_lesson("t2_01");
  REQUIRE(score_ptr != nullptr);
  const LessonScore orig = *score_ptr;

  LessonEngine engine;
  LessonConfig cfg{};
  cfg.practice_mode = LessonPracticeMode::WaitForNote;
  cfg.hand_mode = LessonHandMode::HandsTogether;
  cfg.practice_transform = PracticeTransform::HandBlocking;
  engine.load_score(orig, cfg);

  // Original score stays untouched while display_measures and targets reflect blocked chords
  CHECK(engine.score() == orig);
  REQUIRE_FALSE(engine.display_measures().empty());
  CHECK(engine.display_measures() != orig.measures);
  REQUIRE_FALSE(engine.targets().empty());
  CHECK_FALSE(engine.build_demo_playback().empty());

  const auto summary = engine.build_run_summary();
  CHECK(summary.practice_transform == PracticeTransform::HandBlocking);

  // Guardrail: transformed run must NOT graduate Tempo Ladder or Mastered status
  LessonProgress prog{};
  LessonRunSummary fake_run{};
  fake_run.lesson_id = orig.meta.id;
  fake_run.practice_mode = LessonPracticeMode::SteadyPulse;
  fake_run.hand_mode = LessonHandMode::HandsTogether;
  fake_run.practice_transform = PracticeTransform::HandBlocking;
  fake_run.is_full_piece = true;
  fake_run.effective_bpm = orig.meta.target_bpm;
  fake_run.accuracy = 1.0;

  const auto upd = apply_run_to_progress(prog, fake_run, orig.meta.target_bpm, 20700);
  CHECK_FALSE(upd.just_mastered);
  CHECK_FALSE(prog.ladder_60_clean);
  CHECK_FALSE(prog.ladder_80_clean);
  CHECK_FALSE(prog.ladder_100_clean);
  CHECK(prog.status != LessonMasteryStatus::Mastered);
}

TEST_CASE("LessonEngine MemoryFadeLevel tracks peek-on-mistake and computes SM-2 recall rating",
          "[lesson_engine][phase23]") {
  LessonScore score = make_sample_lesson();
  LessonEngine engine;

  SECTION("WaitForNote HarmonyMapOnly increments memory_peek_count once per missed target") {
    LessonConfig cfg{};
    cfg.practice_mode = LessonPracticeMode::WaitForNote;
    cfg.hand_mode = LessonHandMode::HandsTogether;
    cfg.memory_fade = MemoryFadeLevel::HarmonyMapOnly;
    engine.load_score(score, cfg);

    REQUIRE(engine.targets().size() >= 3U);
    // Target 0: hit cleanly -> no peek
    const auto r0 = engine.submit_played_notes(engine.targets()[0].expected_midi);
    CHECK(r0.matched);
    CHECK(engine.memory_peek_count() == 0U);
    CHECK_FALSE(engine.targets()[0].memory_peeked);

    // Target 1: wrong note twice -> peeked once, count == 1
    const std::array<MidiPitch, 1> wrong{21};
    const auto w1 = engine.submit_played_notes(wrong);
    CHECK(w1.wrong_note);
    CHECK(engine.memory_peek_count() == 1U);
    CHECK(engine.targets()[1].memory_peeked);

    const auto w2 = engine.submit_played_notes(wrong);
    CHECK(w2.wrong_note);
    CHECK(engine.memory_peek_count() == 1U);

    // Now hit Target 1 and complete remaining targets
    CHECK(engine.submit_played_notes(engine.targets()[1].expected_midi).matched);
    for (std::size_t i = 2U; i < engine.targets().size(); ++i) {
      CHECK(engine.submit_played_notes(engine.targets()[i].expected_midi).matched);
    }

    const auto summary = engine.build_run_summary();
    CHECK(summary.memory_fade == MemoryFadeLevel::HarmonyMapOnly);
    CHECK(summary.memory_peek_count == 1U);
  }

  SECTION("SteadyPulse BlankMemory marks missed beats as peeked") {
    LessonConfig cfg{};
    cfg.practice_mode = LessonPracticeMode::SteadyPulse;
    cfg.hand_mode = LessonHandMode::HandsTogether;
    cfg.memory_fade = MemoryFadeLevel::BlankMemory;
    engine.load_score(score, cfg);

    (void)engine.start();
    while (engine.playback_state() == LessonPlaybackState::CountIn) {
      (void)engine.update(100.0);
    }
    // Let the entire piece time out without playing
    for (int i = 0; i < 200 && engine.playback_state() == LessonPlaybackState::Playing; ++i) {
      (void)engine.update(100.0);
    }
    CHECK(engine.playback_state() == LessonPlaybackState::Completed);
    CHECK(engine.memory_peek_count() == engine.targets().size());
    for (const auto& t : engine.targets()) {
      CHECK(t.memory_peeked);
    }
  }

  SECTION("suggest_memory_srs_rating and apply_run_to_progress track memory mastery") {
    CHECK(suggest_memory_srs_rating(0.95, 0U) == SrsRating::Good);
    CHECK(suggest_memory_srs_rating(0.92, 0U) == SrsRating::Good);
    CHECK(suggest_memory_srs_rating(0.95, 1U) == SrsRating::Hard);
    CHECK(suggest_memory_srs_rating(0.82, 2U) == SrsRating::Hard);
    CHECK(suggest_memory_srs_rating(0.85, 3U) == SrsRating::Again);
    CHECK(suggest_memory_srs_rating(0.75, 0U) == SrsRating::Again);

    LessonProgress prog{};
    LessonRunSummary map_run{};
    map_run.lesson_id = score.meta.id;
    map_run.practice_mode = LessonPracticeMode::WaitForNote;
    map_run.hand_mode = LessonHandMode::HandsTogether;
    map_run.practice_transform = PracticeTransform::Straight;
    map_run.memory_fade = MemoryFadeLevel::HarmonyMapOnly;
    map_run.memory_peek_count = 0U;
    map_run.is_full_piece = true;
    map_run.accuracy = 1.0;

    const auto u1 = apply_run_to_progress(prog, map_run, score.meta.target_bpm, 20700);
    CHECK(u1.suggested_rating == SrsRating::Good);
    CHECK(prog.memory_map_clean);
    CHECK_FALSE(prog.memory_blank_clean);

    LessonRunSummary blank_run = map_run;
    blank_run.memory_fade = MemoryFadeLevel::BlankMemory;
    const auto u2 = apply_run_to_progress(prog, blank_run, score.meta.target_bpm, 20701);
    CHECK(u2.suggested_rating == SrsRating::Good);
    CHECK(prog.memory_blank_clean);
  }
}

TEST_CASE("LessonEngine handles 32nd runs and 2-against-3 polyrhythm in SteadyPulse and Demo",
          "[lesson_engine][phase24]") {
  LessonScore score = make_sample_lesson();
  score.measures.resize(1);
  score.phrases = {LessonPhrase{.label = "Phrase A", .start_bar = 0, .end_bar = 0}};
  // Beat 1: RH 8th triplet (0, 160, 320) vs LH 8ths (0, 240)
  // Beat 2: RH four 32nds (480, 540, 600, 660) + quarter (720)
  // Beat 3-4: Half (960)
  auto make_n = [](NoteLetter l, int oct) {
    const SpelledNote sp{.letter = l, .accidental = Accidental::Natural, .octave = oct};
    return LessonNote{.spelled = sp, .midi = to_midi(sp).value()};
  };
  score.measures[0].treble_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_n(NoteLetter::C, 5)},
       .tuplet = TupletRatio{3, 2, 1}},
      {.start_tick = 160,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_n(NoteLetter::D, 5)},
       .tuplet = TupletRatio{3, 2, 1}},
      {.start_tick = 320,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_n(NoteLetter::E, 5)},
       .tuplet = TupletRatio{3, 2, 1}},
      {.start_tick = 480,
       .duration = {.base = NoteBaseDuration::ThirtySecond},
       .notes = {make_n(NoteLetter::F, 5)}},
      {.start_tick = 540,
       .duration = {.base = NoteBaseDuration::ThirtySecond},
       .notes = {make_n(NoteLetter::E, 5)}},
      {.start_tick = 600,
       .duration = {.base = NoteBaseDuration::ThirtySecond},
       .notes = {make_n(NoteLetter::D, 5)}},
      {.start_tick = 660,
       .duration = {.base = NoteBaseDuration::ThirtySecond},
       .notes = {make_n(NoteLetter::C, 5)}},
      {.start_tick = 720,
       .duration = {.base = NoteBaseDuration::Quarter},
       .notes = {make_n(NoteLetter::D, 5)}},
      {.start_tick = 960,
       .duration = {.base = NoteBaseDuration::Half},
       .notes = {make_n(NoteLetter::C, 5)}},
  };
  score.measures[0].bass_events = {
      {.start_tick = 0,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_n(NoteLetter::C, 3)}},
      {.start_tick = 240,
       .duration = {.base = NoteBaseDuration::Eighth},
       .notes = {make_n(NoteLetter::G, 3)}},
      {.start_tick = 480,
       .duration = {.base = NoteBaseDuration::Half, .dotted = true},
       .notes = {make_n(NoteLetter::C, 3)}},
  };

  LessonEngine engine;
  LessonConfig cfg{};
  cfg.practice_mode = LessonPracticeMode::WaitForNote;
  cfg.hand_mode = LessonHandMode::HandsTogether;
  engine.load_score(score, cfg);

  // Ticks: 0 (C3+C5), 160 (D5), 240 (G3), 320 (E5), 480 (C3+F5), 540 (E5), 600 (D5), 660 (C5),
  // 720 (D5), 960 (C5) -> 10 targets
  REQUIRE(engine.targets().size() == 10U);
  CHECK(engine.targets()[1].tick_in_bar == 160);
  CHECK(engine.targets()[2].tick_in_bar == 240);
  CHECK(engine.targets()[3].tick_in_bar == 320);

  const auto demo = engine.build_demo_playback();
  REQUIRE(demo.size() == 12U);
  for (std::size_t i = 1; i < demo.size(); ++i) {
    CHECK(demo[i].start_ms >= demo[i - 1U].start_ms);
  }
}

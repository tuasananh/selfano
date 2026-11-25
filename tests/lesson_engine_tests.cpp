#include "core/lesson_engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

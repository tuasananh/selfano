#include "core/etude_engine.hpp"
#include "core/weekly_key.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <vector>

using namespace selfano::core;

namespace {

std::set<std::uint8_t> allowed_key_pitch_classes(KeySignature key) {
  std::set<std::uint8_t> pcs;
  for (int i = 0; i < 7; ++i) {
    const auto letter = static_cast<NoteLetter>(i);
    const Accidental acc = key_accidental_for(key, letter);
    pcs.insert(pitch_class(SpelledNote{letter, acc, 4}));
  }
  return pcs;
}

void verify_system_invariants(const EtudeSystem& sys, KeySignature key, EtudeHandMode hand_mode,
                              EtudeMaxJump max_jump, std::uint32_t expected_first_bar) {
  const auto allowed_pcs = allowed_key_pitch_classes(key);
  std::optional<int> prev_melody_step;

  for (std::size_t b = 0; b < 4; ++b) {
    const auto& m = sys.measures[b];
    REQUIRE(m.bar_number == expected_first_bar + static_cast<std::uint32_t>(b));

    if (hand_mode == EtudeHandMode::RightHand || hand_mode == EtudeHandMode::EasyHandsTogether) {
      REQUIRE_FALSE(m.treble_events.empty());
      int sum = 0;
      for (const auto& ev : m.treble_events) {
        REQUIRE(ev.beat_start == sum);
        sum += static_cast<int>(ev.duration);
        if (!ev.is_rest) {
          REQUIRE(ev.notes.size() == 1);
          REQUIRE(ev.midi.size() == 1);
          REQUIRE(allowed_pcs.contains(pitch_class(ev.notes[0])));
          REQUIRE(ev.midi[0] >= 60);
          REQUIRE(ev.midi[0] <= 79);
          const int cur_step = diatonic_step(ev.notes[0]);
          if (prev_melody_step.has_value()) {
            REQUIRE(std::abs(cur_step - *prev_melody_step) <= static_cast<int>(max_jump));
          }
          prev_melody_step = cur_step;
        }
      }
      REQUIRE(sum == 4);
    } else {
      REQUIRE(m.treble_events.empty());
    }

    if (hand_mode == EtudeHandMode::LeftHand || hand_mode == EtudeHandMode::EasyHandsTogether) {
      REQUIRE_FALSE(m.bass_events.empty());
      int sum = 0;
      for (const auto& ev : m.bass_events) {
        REQUIRE(ev.beat_start == sum);
        sum += static_cast<int>(ev.duration);
        if (!ev.is_rest) {
          REQUIRE_FALSE(ev.notes.empty());
          REQUIRE(ev.notes.size() == ev.midi.size());
          for (std::size_t i = 0; i < ev.notes.size(); ++i) {
            REQUIRE(allowed_pcs.contains(pitch_class(ev.notes[i])));
            if (hand_mode == EtudeHandMode::EasyHandsTogether) {
              REQUIRE(ev.midi[i] >= 40);
              REQUIRE(ev.midi[i] < 60); // Zero LH/RH collision
            } else {
              REQUIRE(ev.midi[i] >= 41);
              REQUIRE(ev.midi[i] <= 60);
            }
          }
          if (hand_mode == EtudeHandMode::LeftHand) {
            REQUIRE(ev.notes.size() == 1);
            const int cur_step = diatonic_step(ev.notes[0]);
            if (prev_melody_step.has_value()) {
              REQUIRE(std::abs(cur_step - *prev_melody_step) <= static_cast<int>(max_jump));
            }
            prev_melody_step = cur_step;
          }
        }
      }
      REQUIRE(sum == 4);
    } else {
      REQUIRE(m.bass_events.empty());
    }
  }

  REQUIRE_FALSE(sys.targets.empty());
  for (std::size_t i = 1; i < sys.targets.size(); ++i) {
    REQUIRE(sys.targets[i].system_beat > sys.targets[i - 1].system_beat);
  }
}

} // namespace

TEST_CASE("EtudeEngine generates valid 4/4 systems within key, range, and max_jump constraints",
          "[etude]") {
  const std::array<KeySignature, 6> keys{{
      {0, Mode::Major},
      {1, Mode::Major},
      {-1, Mode::Minor},
      {-3, Mode::Major},
      {3, Mode::Minor},
      {6, Mode::Major},
  }};
  const std::array<EtudeHandMode, 3> hand_modes{{
      EtudeHandMode::RightHand,
      EtudeHandMode::LeftHand,
      EtudeHandMode::EasyHandsTogether,
  }};
  const std::array<EtudeMaxJump, 4> jumps{{
      EtudeMaxJump::Steps,
      EtudeMaxJump::Thirds,
      EtudeMaxJump::Fifths,
      EtudeMaxJump::ChordSkips,
  }};

  for (const auto key : keys) {
    for (const auto hm : hand_modes) {
      for (const auto jump : jumps) {
        EtudeEngine engine(EtudeConfig{
            .key = key,
            .hand_mode = hm,
            .max_jump = jump,
            .bpm = 75.0,
            .seed = 4242U,
        });
        verify_system_invariants(engine.active_system(), key, hm, jump, 1U);
        verify_system_invariants(engine.preview_system(), key, hm, jump, 5U);
      }
    }
  }

  // Fixed seed determinism check
  EtudeEngine a(EtudeConfig{.key = KeySignature{-2, Mode::Major}, .seed = 9001U});
  EtudeEngine b(EtudeConfig{.key = KeySignature{-2, Mode::Major}, .seed = 9001U});
  REQUIRE(a.active_system().targets.size() == b.active_system().targets.size());
  for (std::size_t i = 0; i < a.active_system().targets.size(); ++i) {
    REQUIRE(a.active_system().targets[i].expected_midi ==
            b.active_system().targets[i].expected_midi);
    REQUIRE(a.active_system().targets[i].system_beat == b.active_system().targets[i].system_beat);
  }
}

TEST_CASE("EtudeEngine Steady-Pulse mode runs 4-beat count-in, +-40% window, and rolling promotion",
          "[etude]") {
  // 60 BPM = 1 beat per 1000 ms for clean deterministic arithmetic
  EtudeEngine engine(EtudeConfig{
      .key = KeySignature{0, Mode::Major},
      .hand_mode = EtudeHandMode::RightHand,
      .timing_mode = EtudeTimingMode::SteadyPulse,
      .max_jump = EtudeMaxJump::Thirds,
      .bpm = 60.0,
      .hit_window_fraction = 0.40,
      .seed = 123U,
  });

  REQUIRE(engine.playback_state() == EtudePlaybackState::Stopped);
  const auto start_out = engine.start();
  REQUIRE(engine.playback_state() == EtudePlaybackState::CountIn);
  REQUIRE(start_out.metronome_click);
  REQUIRE(start_out.metronome_accent);
  REQUIRE(engine.count_in_beat_number() == 1);

  // Advance through count-in beats 2, 3, 4
  const auto c2 = engine.update(1000.0);
  REQUIRE(c2.metronome_click);
  REQUIRE_FALSE(c2.metronome_accent);
  REQUIRE(engine.count_in_beat_number() == 2);

  const auto c3 = engine.update(1000.0);
  REQUIRE(c3.metronome_click);
  REQUIRE(engine.count_in_beat_number() == 3);

  const auto c4 = engine.update(1000.0);
  REQUIRE(c4.metronome_click);
  REQUIRE(engine.count_in_beat_number() == 4);

  // At 3.5s (playhead = -0.5 beats), beat 0 target is outside +-0.40 window -> Ignored
  (void)engine.update(500.0);
  const auto early_miss =
      engine.submit_played_notes(engine.active_system().targets[0].expected_midi);
  REQUIRE(early_miss.status == EtudeInputStatus::Ignored);

  // Advance 200ms (playhead = -0.3 beats) -> inside +-0.40 window for beat 0 -> Hit!
  (void)engine.update(200.0);
  const auto early_hit =
      engine.submit_played_notes(engine.active_system().targets[0].expected_midi);
  REQUIRE(early_hit.status == EtudeInputStatus::Hit);
  REQUIRE(engine.active_system().targets[0].status == EtudeTargetStatus::Hit);
  REQUIRE(engine.hit_targets() == 1);

  // Advance 300ms to cross into Playing (beat 0.0)
  const auto enter_play = engine.update(300.0);
  REQUIRE(engine.playback_state() == EtudePlaybackState::Playing);
  REQUIRE(enter_play.metronome_click);
  REQUIRE(enter_play.metronome_accent);

  // Let remaining beats in active system pass unplayed -> marked Missed without stopping
  const auto prev_bar1_num = engine.preview_system().measures[0].bar_number;
  const auto prev_first_target = engine.preview_system().targets[0].expected_midi;

  bool promoted = false;
  for (int step = 0; step < 33; ++step) {
    const auto tick = engine.update(500.0);
    if (tick.system_promoted) {
      promoted = true;
    }
  }
  REQUIRE(promoted);
  REQUIRE(engine.playback_state() == EtudePlaybackState::Playing);
  REQUIRE(engine.completed_systems() == 1);
  REQUIRE(engine.completed_bars() == 4);
  REQUIRE(engine.missed_targets() > 0);
  // Old preview system is now active, and fresh bars 9..12 generated in preview
  REQUIRE(engine.active_system().measures[0].bar_number == prev_bar1_num);
  REQUIRE(engine.active_system().targets[0].expected_midi == prev_first_target);
  REQUIRE(engine.preview_system().measures[0].bar_number == 9U);
}

TEST_CASE("EtudeEngine Wait-for-Note mode holds playhead on error and promotes cleanly on bar 4",
          "[etude]") {
  EtudeEngine engine(EtudeConfig{
      .key = KeySignature{1, Mode::Major},
      .hand_mode = EtudeHandMode::EasyHandsTogether,
      .timing_mode = EtudeTimingMode::WaitForNote,
      .max_jump = EtudeMaxJump::Fifths,
      .bpm = 80.0,
      .seed = 555U,
  });

  (void)engine.start();
  REQUIRE(engine.playback_state() == EtudePlaybackState::Playing);
  REQUIRE(engine.wait_target_index() == 0);

  // Time updates do NOT advance playhead in Wait-for-Note mode
  const double initial_beat = engine.playhead_beat();
  const auto tick = engine.update(5000.0);
  REQUIRE_FALSE(tick.metronome_click);
  REQUIRE_FALSE(tick.system_promoted);
  REQUIRE(engine.playhead_beat() == initial_beat);
  REQUIRE(engine.wait_target_index() == 0);

  // Wrong note marks target Missed and holds position
  const MidiPitch bad =
      static_cast<MidiPitch>(engine.active_system().targets[0].expected_midi[0] + 1U);
  const auto miss = engine.submit_played_notes(std::array<MidiPitch, 1>{bad});
  REQUIRE(miss.status == EtudeInputStatus::Missed);
  REQUIRE(engine.wait_target_index() == 0);
  REQUIRE(engine.active_system().targets[0].status == EtudeTargetStatus::Missed);
  REQUIRE(engine.missed_targets() == 1);

  // Play every target in active system cleanly to trigger rolling promotion
  const std::size_t n_targets = engine.active_system().targets.size();
  const std::uint32_t expected_next_bar = engine.preview_system().measures[0].bar_number;
  for (std::size_t i = 0; i < n_targets; ++i) {
    const auto res = engine.submit_played_notes(engine.active_system().targets[i].expected_midi);
    REQUIRE(res.status == EtudeInputStatus::Hit);
    REQUIRE(res.system_promoted == (i + 1 == n_targets));
  }

  REQUIRE(engine.completed_systems() == 1);
  REQUIRE(engine.completed_bars() == 4);
  REQUIRE(engine.wait_target_index() == 0);
  REQUIRE(engine.active_system().measures[0].bar_number == expected_next_bar);
  REQUIRE(engine.preview_system().measures[0].bar_number == 9U);
  REQUIRE(engine.hit_targets() == n_targets - 1); // Target 0 had prior error
}

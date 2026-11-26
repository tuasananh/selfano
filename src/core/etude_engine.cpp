#include "core/etude_engine.hpp"

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

struct PoolNote {
  SpelledNote spelled;
  MidiPitch midi;
};

struct RhythmSlot {
  int beat_start;
  EtudeDurationBeats duration;
  bool is_rest;
};

[[nodiscard]] std::vector<PoolNote> build_diatonic_pool(KeySignature key, int min_midi,
                                                        int max_midi) {
  std::vector<PoolNote> pool;
  for (int step = 12; step <= 48; ++step) {
    int oct = step / 7;
    int l_idx = step % 7;
    if (l_idx < 0) {
      l_idx += 7;
      oct -= 1;
    }
    const auto letter = static_cast<NoteLetter>(l_idx);
    const SpelledNote sn{
        .letter = letter, .accidental = key_accidental_for(key, letter), .octave = oct};
    const auto m = to_midi(sn);
    if (m.has_value() && std::cmp_greater_equal(*m, min_midi) &&
        std::cmp_less_equal(*m, max_midi)) {
      pool.push_back(PoolNote{.spelled = sn, .midi = *m});
    }
  }
  return pool;
}

[[nodiscard]] const std::vector<RhythmSlot>& pick_melody_rhythm(bool is_cadence_bar,
                                                                std::mt19937& rng) {
  static const std::array<std::vector<RhythmSlot>, 8> kBodyPatterns{{
      {{.beat_start = 0, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 1, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 3, .duration = EtudeDurationBeats::Quarter, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Half, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 3, .duration = EtudeDurationBeats::Quarter, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 1, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Half, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Half, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Half, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 1, .duration = EtudeDurationBeats::Quarter, .is_rest = true},
       {.beat_start = 2, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 3, .duration = EtudeDurationBeats::Quarter, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Half, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Quarter, .is_rest = true},
       {.beat_start = 3, .duration = EtudeDurationBeats::Quarter, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 1, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Half, .is_rest = true}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Whole, .is_rest = false}},
  }};

  static const std::array<std::vector<RhythmSlot>, 3> kCadencePatterns{{
      {{.beat_start = 0, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 1, .duration = EtudeDurationBeats::Quarter, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Half, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Half, .is_rest = false},
       {.beat_start = 2, .duration = EtudeDurationBeats::Half, .is_rest = false}},
      {{.beat_start = 0, .duration = EtudeDurationBeats::Whole, .is_rest = false}},
  }};

  if (is_cadence_bar) {
    std::uniform_int_distribution<std::size_t> dist(0, kCadencePatterns.size() - 1);
    return kCadencePatterns[dist(rng)];
  }
  std::uniform_int_distribution<std::size_t> dist(0, kBodyPatterns.size() - 1);
  return kBodyPatterns[dist(rng)];
}

[[nodiscard]] std::vector<PoolNote> build_accompaniment_voicing(KeySignature key, int degree,
                                                                EtudeDurationBeats duration,
                                                                std::mt19937& rng) {
  std::optional<Triad> chosen;
  for (const int oct : {2, 3}) {
    const auto t = build_diatonic_triad(key, degree, oct, Inversion::Root, false);
    if (!t.has_value()) {
      continue;
    }
    bool ok = true;
    for (const auto& n : t->notes) {
      const auto m = to_midi(n);
      if (!m.has_value() || *m < 40U || *m >= 60U) {
        ok = false;
        break;
      }
    }
    if (ok) {
      chosen = *t;
      break;
    }
  }

  std::vector<PoolNote> out;
  if (chosen.has_value()) {
    // 0 = root only, 1 = root + 5th, 2 = full triad (preferred for Whole notes)
    int style = 0;
    if (duration == EtudeDurationBeats::Whole) {
      std::uniform_int_distribution<int> dist(0, 2);
      style = dist(rng);
    } else {
      std::uniform_int_distribution<int> dist(0, 1);
      style = dist(rng);
    }

    auto push_note = [&](const SpelledNote& sn) {
      if (const auto m = to_midi(sn); m.has_value()) {
        out.push_back(PoolNote{.spelled = sn, .midi = *m});
      }
    };
    push_note(chosen->notes[0]);
    if (style == 1) {
      push_note(chosen->notes[2]);
    } else if (style == 2) {
      push_note(chosen->notes[1]);
      push_note(chosen->notes[2]);
    }
    return out;
  }

  // Fallback to a single bass diatonic note in [40, 59]
  const auto bass_pool = build_diatonic_pool(key, 40, 59);
  if (!bass_pool.empty()) {
    out.push_back(bass_pool[static_cast<std::size_t>(degree - 1) % bass_pool.size()]);
  }
  return out;
}

[[nodiscard]] bool is_cadential_degree_pc(KeySignature key, std::uint8_t pc) noexcept {
  const auto tonic = tonic_of(key);
  const int t_idx = static_cast<int>(tonic.letter);
  for (const int offset : {0, 2, 4}) {
    const auto letter = static_cast<NoteLetter>((t_idx + offset) % 7);
    const SpelledNote sn{
        .letter = letter, .accidental = key_accidental_for(key, letter), .octave = 4};
    if (pitch_class(sn) == pc) {
      return true;
    }
  }
  return false;
}

} // namespace

EtudeEngine::EtudeEngine(EtudeConfig config) : config_{config}, rng_{config.seed} {
  config_.bpm = std::clamp(config_.bpm, 40.0, 120.0);
  config_.hit_window_fraction = std::clamp(config_.hit_window_fraction, 0.15, 0.49);
  rebuild_systems();
}

void EtudeEngine::configure(EtudeConfig config) {
  config.bpm = std::clamp(config.bpm, 40.0, 120.0);
  config.hit_window_fraction = std::clamp(config.hit_window_fraction, 0.15, 0.49);
  config_ = config;
  rebuild_systems();
}

void EtudeEngine::regenerate(std::uint32_t new_seed) {
  config_.seed = new_seed;
  rebuild_systems();
}

void EtudeEngine::set_bpm(double bpm) noexcept {
  config_.bpm = std::clamp(bpm, 40.0, 120.0);
}

void EtudeEngine::set_timing_mode(EtudeTimingMode mode) noexcept {
  config_.timing_mode = mode;
  if (mode == EtudeTimingMode::WaitForNote &&
      (state_ == EtudePlaybackState::CountIn || state_ == EtudePlaybackState::Playing)) {
    state_ = EtudePlaybackState::Playing;
    wait_target_idx_ = 0;
    for (std::size_t i = 0; i < active_system_.targets.size(); ++i) {
      if (active_system_.targets[i].status == EtudeTargetStatus::Pending) {
        wait_target_idx_ = i;
        break;
      }
    }
    if (wait_target_idx_ < active_system_.targets.size()) {
      playhead_beat_ = active_system_.targets[wait_target_idx_].system_beat;
    }
  }
}

EtudeTickOutput EtudeEngine::start() {
  EtudeTickOutput out{};
  if (config_.timing_mode == EtudeTimingMode::SteadyPulse) {
    state_ = EtudePlaybackState::CountIn;
    count_in_beats_ = 0.0;
    playhead_beat_ = -4.0;
    out.metronome_click = true;
    out.metronome_accent = true;
  } else {
    state_ = EtudePlaybackState::Playing;
    wait_target_idx_ = 0;
    playhead_beat_ = active_system_.targets.empty() ? 0.0 : active_system_.targets[0].system_beat;
  }
  return out;
}

void EtudeEngine::pause() noexcept {
  if (state_ == EtudePlaybackState::CountIn || state_ == EtudePlaybackState::Playing) {
    state_ = EtudePlaybackState::Paused;
  }
}

void EtudeEngine::resume() noexcept {
  if (state_ == EtudePlaybackState::Paused) {
    if (config_.timing_mode == EtudeTimingMode::WaitForNote) {
      state_ = EtudePlaybackState::Playing;
    } else {
      state_ = (playhead_beat_ < 0.0) ? EtudePlaybackState::CountIn : EtudePlaybackState::Playing;
    }
  }
}

void EtudeEngine::stop() noexcept {
  state_ = EtudePlaybackState::Stopped;
  count_in_beats_ = 0.0;
  playhead_beat_ = 0.0;
  wait_target_idx_ = 0;
}

EtudeTickOutput EtudeEngine::update(double dt_ms) {
  EtudeTickOutput out{};
  if (dt_ms <= 0.0 || config_.timing_mode != EtudeTimingMode::SteadyPulse) {
    return out;
  }
  if (state_ != EtudePlaybackState::CountIn && state_ != EtudePlaybackState::Playing) {
    return out;
  }

  const double beats_delta = (dt_ms / 1000.0) * (config_.bpm / 60.0);

  if (state_ == EtudePlaybackState::CountIn) {
    const int prev_ci = static_cast<int>(std::floor(count_in_beats_ + 1e-9));
    count_in_beats_ += beats_delta;
    if (count_in_beats_ < 4.0 - 1e-9) {
      const int cur_ci = static_cast<int>(std::floor(count_in_beats_ + 1e-9));
      playhead_beat_ = count_in_beats_ - 4.0;
      if (cur_ci > prev_ci) {
        out.metronome_click = true;
        out.metronome_accent = false;
      }
      return out;
    }

    state_ = EtudePlaybackState::Playing;
    playhead_beat_ = std::max(0.0, count_in_beats_ - 4.0);
    out.metronome_click = true;
    out.metronome_accent = true;
  } else {
    const int prev_beat_int = static_cast<int>(std::floor(playhead_beat_ + 1e-9));
    playhead_beat_ += beats_delta;
    const int cur_beat_int = static_cast<int>(std::floor(playhead_beat_ + 1e-9));
    if (cur_beat_int > prev_beat_int) {
      out.metronome_click = true;
      out.metronome_accent = (cur_beat_int % 4 == 0);
    }
  }

  auto mark_expired_targets = [&]() {
    for (auto& target : active_system_.targets) {
      if (target.status == EtudeTargetStatus::Pending &&
          playhead_beat_ > target.system_beat + config_.hit_window_fraction + 1e-9) {
        target.status = EtudeTargetStatus::Missed;
        ++missed_targets_;
        ++out.newly_missed;
      }
    }
  };

  mark_expired_targets();

  while (playhead_beat_ >= 16.0 - 1e-9) {
    for (auto& target : active_system_.targets) {
      if (target.status == EtudeTargetStatus::Pending) {
        target.status = EtudeTargetStatus::Missed;
        ++missed_targets_;
        ++out.newly_missed;
      }
    }
    promote_system();
    playhead_beat_ = std::max(0.0, playhead_beat_ - 16.0);
    out.system_promoted = true;
    mark_expired_targets();
  }

  return out;
}

EtudeInputResult EtudeEngine::submit_played_notes(std::span<const MidiPitch> played_midi) {
  auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty() || state_ == EtudePlaybackState::Stopped ||
      state_ == EtudePlaybackState::Paused) {
    return EtudeInputResult{};
  }

  if (config_.timing_mode == EtudeTimingMode::SteadyPulse) {
    if (state_ == EtudePlaybackState::CountIn &&
        playhead_beat_ < -(config_.hit_window_fraction + 1e-9)) {
      return EtudeInputResult{};
    }

    std::optional<std::size_t> best_idx;
    double best_dist = 1e9;
    for (std::size_t i = 0; i < active_system_.targets.size(); ++i) {
      const auto& t = active_system_.targets[i];
      if (t.status != EtudeTargetStatus::Pending) {
        continue;
      }
      const double dist = std::abs(playhead_beat_ - t.system_beat);
      if (dist <= config_.hit_window_fraction + 1e-9 && dist < best_dist) {
        best_dist = dist;
        best_idx = i;
      }
    }

    if (!best_idx.has_value()) {
      return EtudeInputResult{};
    }

    auto& target = active_system_.targets[*best_idx];
    target.played_midi = normalized;
    if (normalized == target.expected_midi) {
      target.status = EtudeTargetStatus::Hit;
      ++hit_targets_;
      return EtudeInputResult{
          .status = EtudeInputStatus::Hit,
          .target_index = best_idx,
          .system_promoted = false,
      };
    }

    target.status = EtudeTargetStatus::Missed;
    target.had_error = true;
    ++missed_targets_;
    return EtudeInputResult{
        .status = EtudeInputStatus::Missed,
        .target_index = best_idx,
        .system_promoted = false,
    };
  }

  // WaitForNote mode
  if (wait_target_idx_ >= active_system_.targets.size()) {
    return EtudeInputResult{};
  }

  auto& target = active_system_.targets[wait_target_idx_];
  target.played_midi = normalized;

  if (normalized == target.expected_midi) {
    target.status = EtudeTargetStatus::Hit;
    if (!target.had_error) {
      ++hit_targets_;
    }
    const std::size_t hit_idx = wait_target_idx_;
    ++wait_target_idx_;
    bool promoted = false;
    if (wait_target_idx_ >= active_system_.targets.size()) {
      promote_system();
      wait_target_idx_ = 0;
      promoted = true;
    }
    playhead_beat_ =
        active_system_.targets.empty() ? 0.0 : active_system_.targets[wait_target_idx_].system_beat;
    return EtudeInputResult{
        .status = EtudeInputStatus::Hit,
        .target_index = hit_idx,
        .system_promoted = promoted,
    };
  }

  target.status = EtudeTargetStatus::Missed;
  if (!target.had_error) {
    target.had_error = true;
    ++missed_targets_;
  }
  return EtudeInputResult{
      .status = EtudeInputStatus::Missed,
      .target_index = wait_target_idx_,
      .system_promoted = false,
  };
}

int EtudeEngine::count_in_beat_number() const noexcept {
  if (state_ != EtudePlaybackState::CountIn) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::floor(count_in_beats_ + 1e-9)) + 1, 1, 4);
}

std::size_t EtudeEngine::expected_cardinality() const noexcept {
  if (active_system_.targets.empty()) {
    return 1;
  }
  if (config_.timing_mode == EtudeTimingMode::WaitForNote) {
    if (wait_target_idx_ < active_system_.targets.size()) {
      return std::max<std::size_t>(1,
                                   active_system_.targets[wait_target_idx_].expected_midi.size());
    }
    return 1;
  }

  std::optional<std::size_t> best_idx;
  double best_dist = 1e9;
  for (std::size_t i = 0; i < active_system_.targets.size(); ++i) {
    const auto& t = active_system_.targets[i];
    if (t.status != EtudeTargetStatus::Pending) {
      continue;
    }
    const double dist = std::abs(playhead_beat_ - t.system_beat);
    if (dist <= config_.hit_window_fraction + 1e-9 && dist < best_dist) {
      best_dist = dist;
      best_idx = i;
    }
  }
  if (best_idx.has_value()) {
    return std::max<std::size_t>(1, active_system_.targets[*best_idx].expected_midi.size());
  }

  for (const auto& t : active_system_.targets) {
    if (t.status == EtudeTargetStatus::Pending) {
      return std::max<std::size_t>(1, t.expected_midi.size());
    }
  }
  return 1;
}

double EtudeEngine::accuracy_ratio() const noexcept {
  const std::uint32_t total = hit_targets_ + missed_targets_;
  if (total == 0) {
    return 0.0;
  }
  return static_cast<double>(hit_targets_) / static_cast<double>(total);
}

void EtudeEngine::rebuild_systems() {
  stop();
  rng_.seed(config_.seed);
  next_bar_number_ = 1;
  last_melody_pool_idx_.reset();
  completed_bars_ = 0;
  completed_systems_ = 0;
  hit_targets_ = 0;
  missed_targets_ = 0;

  active_system_ = generate_system(next_bar_number_);
  next_bar_number_ += 4U;
  preview_system_ = generate_system(next_bar_number_);
  next_bar_number_ += 4U;
}

void EtudeEngine::promote_system() {
  active_system_ = std::move(preview_system_);
  preview_system_ = generate_system(next_bar_number_);
  next_bar_number_ += 4U;
  ++completed_systems_;
  completed_bars_ += 4U;
}

EtudeSystem EtudeEngine::generate_system(std::uint32_t first_bar_number) {
  static constexpr std::array<std::array<int, 4>, 4> kProgressions{{
      {1, 4, 5, 1},
      {1, 6, 4, 5},
      {1, 5, 6, 4},
      {1, 2, 5, 1},
  }};

  const bool melody_is_bass = (config_.hand_mode == EtudeHandMode::LeftHand);
  const auto melody_pool = melody_is_bass ? build_diatonic_pool(config_.key, 41, 60)
                                          : build_diatonic_pool(config_.key, 60, 79);

  if (!last_melody_pool_idx_.has_value() || *last_melody_pool_idx_ >= melody_pool.size()) {
    last_melody_pool_idx_ = melody_pool.empty() ? 0U : (melody_pool.size() / 3U);
  }

  std::uniform_int_distribution<std::size_t> prog_dist(0, kProgressions.size() - 1);
  const auto& prog = kProgressions[prog_dist(rng_)];

  EtudeSystem sys{};
  const int max_jump = static_cast<int>(config_.max_jump);
  std::optional<std::size_t> prev_prev_idx;

  auto pick_next_melody_idx = [&](bool prefer_cadence) -> std::size_t {
    if (melody_pool.empty()) {
      return 0;
    }
    const int cur = static_cast<int>(*last_melody_pool_idx_);
    const int lo = std::max(0, cur - max_jump);
    const int hi = std::min(static_cast<int>(melody_pool.size()) - 1, cur + max_jump);

    std::vector<std::size_t> candidates;
    for (int idx = lo; idx <= hi; ++idx) {
      const auto u_idx = static_cast<std::size_t>(idx);
      if (prev_prev_idx.has_value() && *prev_prev_idx == u_idx && *last_melody_pool_idx_ == u_idx &&
          hi > lo) {
        continue;
      }
      candidates.push_back(u_idx);
    }
    if (candidates.empty()) {
      candidates.push_back(*last_melody_pool_idx_);
    }

    if (prefer_cadence) {
      std::vector<std::size_t> cadential;
      for (const std::size_t c : candidates) {
        if (is_cadential_degree_pc(config_.key, pitch_class(melody_pool[c].spelled))) {
          cadential.push_back(c);
        }
      }
      if (!cadential.empty()) {
        candidates = std::move(cadential);
      }
    }

    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    const std::size_t chosen = candidates[dist(rng_)];
    prev_prev_idx = last_melody_pool_idx_;
    last_melody_pool_idx_ = chosen;
    return chosen;
  };

  for (std::size_t b = 0; b < 4; ++b) {
    auto& measure = sys.measures[b];
    measure.bar_number = first_bar_number + static_cast<std::uint32_t>(b);
    const bool is_cadence_bar = (b == 3);

    const auto& melody_rhythm = pick_melody_rhythm(is_cadence_bar, rng_);
    std::vector<EtudeVoiceEvent> melody_events;
    melody_events.reserve(melody_rhythm.size());

    std::size_t last_note_slot_idx = 0;
    for (std::size_t s = 0; s < melody_rhythm.size(); ++s) {
      if (!melody_rhythm[s].is_rest) {
        last_note_slot_idx = s;
      }
    }

    for (std::size_t s = 0; s < melody_rhythm.size(); ++s) {
      const auto& slot = melody_rhythm[s];
      EtudeVoiceEvent ev{};
      ev.beat_start = slot.beat_start;
      ev.duration = slot.duration;
      ev.is_rest = slot.is_rest;
      if (!slot.is_rest && !melody_pool.empty()) {
        const bool prefer_cad = is_cadence_bar && (s == last_note_slot_idx);
        const std::size_t idx = pick_next_melody_idx(prefer_cad);
        ev.notes.push_back(melody_pool[idx].spelled);
        ev.midi.push_back(melody_pool[idx].midi);
      }
      melody_events.push_back(std::move(ev));
    }

    if (config_.hand_mode == EtudeHandMode::RightHand) {
      measure.treble_events = std::move(melody_events);
    } else if (config_.hand_mode == EtudeHandMode::LeftHand) {
      measure.bass_events = std::move(melody_events);
    } else {
      measure.treble_events = std::move(melody_events);
      // Bass accompaniment: Whole or Half+Half in [40, 59]
      std::uniform_int_distribution<int> acc_rhythm_dist(0, 1);
      const bool use_two_halves = (!is_cadence_bar && acc_rhythm_dist(rng_) == 1);
      if (use_two_halves) {
        for (const int beat_start : {0, 2}) {
          const auto voicing =
              build_accompaniment_voicing(config_.key, prog[b], EtudeDurationBeats::Half, rng_);
          EtudeVoiceEvent bass_ev{};
          bass_ev.beat_start = beat_start;
          bass_ev.duration = EtudeDurationBeats::Half;
          bass_ev.is_rest = false;
          for (const auto& pn : voicing) {
            bass_ev.notes.push_back(pn.spelled);
            bass_ev.midi.push_back(pn.midi);
          }
          measure.bass_events.push_back(std::move(bass_ev));
        }
      } else {
        const auto voicing =
            build_accompaniment_voicing(config_.key, prog[b], EtudeDurationBeats::Whole, rng_);
        EtudeVoiceEvent bass_ev{};
        bass_ev.beat_start = 0;
        bass_ev.duration = EtudeDurationBeats::Whole;
        bass_ev.is_rest = false;
        for (const auto& pn : voicing) {
          bass_ev.notes.push_back(pn.spelled);
          bass_ev.midi.push_back(pn.midi);
        }
        measure.bass_events.push_back(std::move(bass_ev));
      }
    }

    // Build beat targets from union of non-rest onsets per beat in measure
    std::map<int, std::vector<MidiPitch>> beat_midi;
    for (const auto& ev : measure.treble_events) {
      if (!ev.is_rest) {
        auto& vec = beat_midi[ev.beat_start];
        vec.insert(vec.end(), ev.midi.begin(), ev.midi.end());
      }
    }
    for (const auto& ev : measure.bass_events) {
      if (!ev.is_rest) {
        auto& vec = beat_midi[ev.beat_start];
        vec.insert(vec.end(), ev.midi.begin(), ev.midi.end());
      }
    }

    for (auto& [beat_in_bar, midi_list] : beat_midi) {
      sys.targets.push_back(EtudeBeatTarget{
          .bar_index = b,
          .beat_in_bar = beat_in_bar,
          .system_beat = static_cast<double>(b * 4U) + static_cast<double>(beat_in_bar),
          .expected_midi = normalize_midi_set(midi_list),
          .status = EtudeTargetStatus::Pending,
          .had_error = false,
          .played_midi = {},
      });
    }
  }

  return sys;
}

} // namespace selfano::core

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

void EtudeEngine::set_corpus(const SightReadingCorpus* corpus) {
  corpus_ = corpus;
  if (config_.source_mode == EtudeSourceMode::HybridClassical) {
    rebuild_systems();
  }
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
    const double ci_beats = static_cast<double>(active_system_.time_signature.pulse_count());
    playhead_beat_ = -ci_beats;
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
  const int bar_pulses = std::max(1, active_system_.time_signature.pulse_count());
  const double ci_beats = static_cast<double>(bar_pulses);

  if (state_ == EtudePlaybackState::CountIn) {
    const int prev_ci = static_cast<int>(std::floor(count_in_beats_ + 1e-9));
    count_in_beats_ += beats_delta;
    if (count_in_beats_ < ci_beats - 1e-9) {
      const int cur_ci = static_cast<int>(std::floor(count_in_beats_ + 1e-9));
      playhead_beat_ = count_in_beats_ - ci_beats;
      if (cur_ci > prev_ci) {
        out.metronome_click = true;
        out.metronome_accent = false;
      }
      return out;
    }

    state_ = EtudePlaybackState::Playing;
    playhead_beat_ = std::max(0.0, count_in_beats_ - ci_beats);
    out.metronome_click = true;
    out.metronome_accent = true;
  } else {
    const int prev_beat_int = static_cast<int>(std::floor(playhead_beat_ + 1e-9));
    playhead_beat_ += beats_delta;
    const int cur_beat_int = static_cast<int>(std::floor(playhead_beat_ + 1e-9));
    if (cur_beat_int > prev_beat_int) {
      out.metronome_click = true;
      out.metronome_accent = (cur_beat_int % bar_pulses == 0);
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

  const double sys_beats = static_cast<double>(bar_pulses * 4);
  while (playhead_beat_ >= sys_beats - 1e-9) {
    for (auto& target : active_system_.targets) {
      if (target.status == EtudeTargetStatus::Pending) {
        target.status = EtudeTargetStatus::Missed;
        ++missed_targets_;
        ++out.newly_missed;
      }
    }
    promote_system();
    playhead_beat_ = std::max(0.0, playhead_beat_ - sys_beats);
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
  const int bar_pulses = std::max(1, active_system_.time_signature.pulse_count());
  return std::clamp(static_cast<int>(std::floor(count_in_beats_ + 1e-9)) + 1, 1, bar_pulses);
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
  corpus_cursor_ = 0;
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
  if (config_.source_mode == EtudeSourceMode::HybridClassical && corpus_ != nullptr &&
      !corpus_->excerpts.empty()) {
    std::vector<const SightReadingExcerpt*> matching;
    const int target_lvl = static_cast<int>(config_.difficulty);
    for (const auto& ex : corpus_->excerpts) {
      if (ex.level == target_lvl) {
        matching.push_back(&ex);
      }
    }
    if (matching.empty()) {
      for (const auto& ex : corpus_->excerpts) {
        matching.push_back(&ex);
      }
    }
    if (!matching.empty()) {
      const std::size_t idx = corpus_cursor_ % matching.size();
      ++corpus_cursor_;
      const auto transposed = transpose_excerpt_to_key(*matching[idx], config_.key);
      EtudeSystem sys{};
      sys.time_signature = transposed.time_signature;
      sys.source_label = std::format("{} — {}", transposed.title, transposed.composer);
      const double beat_ticks_d = static_cast<double>(std::max(1, sys.time_signature.beat_ticks()));
      const int bar_pulses = std::max(1, sys.time_signature.pulse_count());

      for (std::size_t b = 0; b < 4U; ++b) {
        auto& em = sys.measures[b];
        em.bar_number = first_bar_number + static_cast<std::uint32_t>(b);
        const auto& lm = transposed.measures[b];
        auto convert_evs = [&](std::span<const LessonEvent> src,
                               std::vector<EtudeVoiceEvent>& dst) {
          for (const auto& lev : src) {
            EtudeVoiceEvent ev{};
            ev.start_tick = lev.start_tick;
            ev.beat_start = lev.start_tick / std::max(1, sys.time_signature.beat_ticks());
            ev.rhythmic_value = lev.duration;
            ev.is_rest = lev.is_rest;
            if (lev.duration.base == NoteBaseDuration::Whole) {
              ev.duration = EtudeDurationBeats::Whole;
            } else if (lev.duration.base == NoteBaseDuration::Half) {
              ev.duration = EtudeDurationBeats::Half;
            } else {
              ev.duration = EtudeDurationBeats::Quarter;
            }
            for (const auto& ln : lev.notes) {
              if (ln.tie_start) {
                ev.tie_start = true;
              }
              if (ln.tie_stop) {
                ev.tie_stop = true;
              }
              ev.notes.push_back(ln.spelled);
              ev.midi.push_back(ln.midi);
            }
            dst.push_back(std::move(ev));
          }
        };
        if (config_.hand_mode != EtudeHandMode::LeftHand) {
          convert_evs(lm.treble_events, em.treble_events);
        }
        if (config_.hand_mode != EtudeHandMode::RightHand) {
          convert_evs(lm.bass_events, em.bass_events);
        }
        std::map<int, std::vector<MidiPitch>> tick_midi;
        auto collect_targets = [&](const std::vector<EtudeVoiceEvent>& evs) {
          for (const auto& ev : evs) {
            if (!ev.is_rest && !ev.tie_stop) {
              auto& vec = tick_midi[ev.start_tick];
              vec.insert(vec.end(), ev.midi.begin(), ev.midi.end());
            }
          }
        };
        collect_targets(em.treble_events);
        collect_targets(em.bass_events);
        for (auto& [tick, mlist] : tick_midi) {
          sys.targets.push_back(EtudeBeatTarget{
              .bar_index = b,
              .beat_in_bar = tick / std::max(1, sys.time_signature.beat_ticks()),
              .tick_in_bar = tick,
              .system_beat = static_cast<double>(b * static_cast<std::size_t>(bar_pulses)) +
                             static_cast<double>(tick) / beat_ticks_d,
              .expected_midi = normalize_midi_set(mlist),
              .status = EtudeTargetStatus::Pending,
              .had_error = false,
              .played_midi = {},
          });
        }
      }
      return sys;
    }
  }

  if (config_.difficulty != EtudeDifficultyLevel::Level1Foundations) {
    EtudeSystem sys{};
    switch (config_.difficulty) {
    case EtudeDifficultyLevel::Level2EighthsAndWaltz:
      sys.time_signature = {.numerator = 3, .denominator = 4};
      break;
    case EtudeDifficultyLevel::Level3CompoundAndDotted:
      sys.time_signature = {.numerator = 6, .denominator = 8};
      break;
    case EtudeDifficultyLevel::Level4AlbertiAndAccidentals:
      sys.time_signature = {.numerator = 4, .denominator = 4};
      break;
    case EtudeDifficultyLevel::Level5RomanticTextures:
      sys.time_signature = {.numerator = 6, .denominator = 4};
      break;
    default:
      sys.time_signature = {.numerator = 4, .denominator = 4};
      break;
    }

    const bool melody_is_bass = (config_.hand_mode == EtudeHandMode::LeftHand);
    const auto melody_pool = melody_is_bass ? build_diatonic_pool(config_.key, 41, 60)
                                            : build_diatonic_pool(config_.key, 60, 79);
    if (!last_melody_pool_idx_.has_value() || *last_melody_pool_idx_ >= melody_pool.size()) {
      last_melody_pool_idx_ = melody_pool.empty() ? 0U : (melody_pool.size() / 3U);
    }
    const int max_jump = static_cast<int>(config_.max_jump);
    auto next_note = [&]() -> PoolNote {
      if (melody_pool.empty()) {
        return PoolNote{.spelled = {NoteLetter::C, Accidental::Natural, 4}, .midi = 60};
      }
      const int cur = static_cast<int>(*last_melody_pool_idx_);
      const int lo = std::max(0, cur - max_jump);
      const int hi = std::min(static_cast<int>(melody_pool.size()) - 1, cur + max_jump);
      std::uniform_int_distribution<int> d(lo, hi);
      *last_melody_pool_idx_ = static_cast<std::size_t>(d(rng_));
      return melody_pool[*last_melody_pool_idx_];
    };

    const int beat_ticks = std::max(1, sys.time_signature.beat_ticks());
    const double beat_ticks_d = static_cast<double>(beat_ticks);
    const int bar_pulses = std::max(1, sys.time_signature.pulse_count());
    static constexpr std::array<int, 4> kProg{1, 4, 5, 1};

    for (std::size_t b = 0; b < 4U; ++b) {
      auto& measure = sys.measures[b];
      measure.bar_number = first_bar_number + static_cast<std::uint32_t>(b);
      const int deg = kProg[b];
      const auto triad =
          build_diatonic_triad(config_.key, deg, 3, Inversion::Root, true).value_or(Triad{});

      auto emit_mel_ev = [&](int st_tick, RhythmicValue rv, bool tie_s = false,
                             bool tie_e = false) {
        const PoolNote pn = next_note();
        EtudeVoiceEvent ev{};
        ev.start_tick = st_tick;
        ev.beat_start = st_tick / beat_ticks;
        ev.rhythmic_value = rv;
        ev.tie_start = tie_s;
        ev.tie_stop = tie_e;
        ev.notes.push_back(pn.spelled);
        ev.midi.push_back(pn.midi);
        return ev;
      };

      std::vector<EtudeVoiceEvent> mel_evs;
      if (config_.difficulty == EtudeDifficultyLevel::Level2EighthsAndWaltz) {
        // 3/4: Quarter + 2 Eighths + Quarter (1440 ticks)
        mel_evs.push_back(emit_mel_ev(0, {.base = NoteBaseDuration::Quarter}));
        mel_evs.push_back(emit_mel_ev(480, {.base = NoteBaseDuration::Eighth}));
        mel_evs.push_back(emit_mel_ev(720, {.base = NoteBaseDuration::Eighth}));
        mel_evs.push_back(emit_mel_ev(960, {.base = NoteBaseDuration::Quarter}));
      } else if (config_.difficulty == EtudeDifficultyLevel::Level3CompoundAndDotted) {
        // 6/8: Dotted Quarter + 3 Eighths (1440 ticks); Bar 0->1 tied across barline
        mel_evs.push_back(
            emit_mel_ev(0, {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 1}));
        mel_evs.push_back(emit_mel_ev(720, {.base = NoteBaseDuration::Eighth}));
        mel_evs.push_back(emit_mel_ev(960, {.base = NoteBaseDuration::Eighth}));
        mel_evs.push_back(emit_mel_ev(1200, {.base = NoteBaseDuration::Eighth}, b == 0U, false));
        if (b == 1U && !sys.measures[0].treble_events.empty()) {
          mel_evs.front().notes = sys.measures[0].treble_events.back().notes;
          mel_evs.front().midi = sys.measures[0].treble_events.back().midi;
          mel_evs.front().tie_stop = true;
        }
      } else if (config_.difficulty == EtudeDifficultyLevel::Level4AlbertiAndAccidentals) {
        // 4/4: 4x16ths + Quarter + Half (1920 ticks)
        mel_evs.push_back(emit_mel_ev(0, {.base = NoteBaseDuration::Sixteenth}));
        mel_evs.push_back(emit_mel_ev(120, {.base = NoteBaseDuration::Sixteenth}));
        mel_evs.push_back(emit_mel_ev(240, {.base = NoteBaseDuration::Sixteenth}));
        mel_evs.push_back(emit_mel_ev(360, {.base = NoteBaseDuration::Sixteenth}));
        mel_evs.push_back(emit_mel_ev(480, {.base = NoteBaseDuration::Quarter}));
        mel_evs.push_back(emit_mel_ev(960, {.base = NoteBaseDuration::Half}));
      } else {
        // Level 5 (6/4): Dotted Half + Quarter + Half (2880 ticks)
        mel_evs.push_back(
            emit_mel_ev(0, {.base = NoteBaseDuration::Half, .dotted = true, .dots = 1}));
        mel_evs.push_back(emit_mel_ev(1440, {.base = NoteBaseDuration::Quarter}));
        mel_evs.push_back(emit_mel_ev(1920, {.base = NoteBaseDuration::Half}));
      }

      if (config_.hand_mode == EtudeHandMode::RightHand) {
        measure.treble_events = std::move(mel_evs);
      } else if (config_.hand_mode == EtudeHandMode::LeftHand) {
        measure.bass_events = std::move(mel_evs);
      } else {
        measure.treble_events = std::move(mel_evs);
        auto make_bass_ev = [&](int st_tick, RhythmicValue rv,
                                std::initializer_list<SpelledNote> sns) {
          EtudeVoiceEvent bev{};
          bev.start_tick = st_tick;
          bev.beat_start = st_tick / beat_ticks;
          bev.rhythmic_value = rv;
          for (const auto& sn : sns) {
            if (const auto m = to_midi(sn); m.has_value()) {
              bev.notes.push_back(sn);
              bev.midi.push_back(*m);
            }
          }
          return bev;
        };
        if (config_.difficulty == EtudeDifficultyLevel::Level2EighthsAndWaltz) {
          measure.bass_events.push_back(
              make_bass_ev(0, {.base = NoteBaseDuration::Quarter}, {triad.notes[0]}));
          measure.bass_events.push_back(make_bass_ev(480, {.base = NoteBaseDuration::Quarter},
                                                     {triad.notes[1], triad.notes[2]}));
          measure.bass_events.push_back(make_bass_ev(960, {.base = NoteBaseDuration::Quarter},
                                                     {triad.notes[1], triad.notes[2]}));
        } else if (config_.difficulty == EtudeDifficultyLevel::Level3CompoundAndDotted) {
          measure.bass_events.push_back(make_bass_ev(
              0, {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 1}, {triad.notes[0]}));
          measure.bass_events.push_back(
              make_bass_ev(720, {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 1},
                           {triad.notes[1], triad.notes[2]}));
        } else if (config_.difficulty == EtudeDifficultyLevel::Level4AlbertiAndAccidentals) {
          const std::array<SpelledNote, 4> alb{triad.notes[0], triad.notes[2], triad.notes[1],
                                               triad.notes[2]};
          for (int k = 0; k < 8; ++k) {
            measure.bass_events.push_back(make_bass_ev(k * 240, {.base = NoteBaseDuration::Eighth},
                                                       {alb[static_cast<std::size_t>(k % 4)]}));
          }
        } else {
          // Level 5 (6/4) sweeping barcarolle bass
          for (int k = 0; k < 6; ++k) {
            measure.bass_events.push_back(
                make_bass_ev(k * 480, {.base = NoteBaseDuration::Quarter},
                             {triad.notes[static_cast<std::size_t>(k % 3)]}));
          }
        }
      }

      std::map<int, std::vector<MidiPitch>> tick_midi;
      auto collect_t = [&](const std::vector<EtudeVoiceEvent>& evs) {
        for (const auto& ev : evs) {
          if (!ev.is_rest && !ev.tie_stop) {
            auto& vec = tick_midi[ev.start_tick];
            vec.insert(vec.end(), ev.midi.begin(), ev.midi.end());
          }
        }
      };
      collect_t(measure.treble_events);
      collect_t(measure.bass_events);
      for (auto& [tick, mlist] : tick_midi) {
        sys.targets.push_back(EtudeBeatTarget{
            .bar_index = b,
            .beat_in_bar = tick / beat_ticks,
            .tick_in_bar = tick,
            .system_beat = static_cast<double>(b * static_cast<std::size_t>(bar_pulses)) +
                           static_cast<double>(tick) / beat_ticks_d,
            .expected_midi = normalize_midi_set(mlist),
            .status = EtudeTargetStatus::Pending,
            .had_error = false,
            .played_midi = {},
        });
      }
    }
    return sys;
  }

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
      ev.start_tick = slot.beat_start * kTicksPerQuarter;
      ev.duration = slot.duration;
      ev.rhythmic_value.base =
          (slot.duration == EtudeDurationBeats::Whole)
              ? NoteBaseDuration::Whole
              : ((slot.duration == EtudeDurationBeats::Half) ? NoteBaseDuration::Half
                                                             : NoteBaseDuration::Quarter);
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

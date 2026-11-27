#include "core/warmup_engine.hpp"

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"
#include "core/weekly_key.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

[[nodiscard]] std::expected<Triad, std::string_view>
pick_triad_in_midi_range(KeySignature key, int degree, bool harmonic_dom,
                         std::span<const int> candidate_octaves, int min_root_midi,
                         int max_root_midi) noexcept {
  for (const int oct : candidate_octaves) {
    const auto triad = build_diatonic_triad(key, degree, oct, Inversion::Root, harmonic_dom);
    if (!triad.has_value()) {
      continue;
    }
    const auto root_midi = to_midi(triad->notes[0]);
    if (root_midi.has_value() && std::cmp_greater_equal(*root_midi, min_root_midi) &&
        std::cmp_less_equal(*root_midi, max_root_midi)) {
      return triad;
    }
  }
  return build_diatonic_triad(key, degree, candidate_octaves.front(), Inversion::Root,
                              harmonic_dom);
}

[[nodiscard]] std::expected<Triad, std::string_view>
pick_voice_led_triad(KeySignature key, int degree, Inversion inv, bool harmonic_dom,
                     int anchor_note_midi, std::size_t match_voice_idx) noexcept {
  constexpr std::array<int, 5> kCandidateOcts{1, 2, 3, 4, 5};
  std::optional<Triad> best;
  int best_dist = std::numeric_limits<int>::max();

  for (const int oct : kCandidateOcts) {
    const auto triad = build_diatonic_triad(key, degree, oct, inv, harmonic_dom);
    if (!triad.has_value()) {
      continue;
    }
    const auto voice_midi = to_midi(triad->notes[match_voice_idx]);
    if (!voice_midi.has_value()) {
      continue;
    }
    const int dist = std::abs(static_cast<int>(*voice_midi) - anchor_note_midi);
    if (dist < best_dist) {
      best_dist = dist;
      best = *triad;
    }
  }
  if (best.has_value()) {
    return *best;
  }
  return build_diatonic_triad(key, degree, 3, inv, harmonic_dom);
}

[[nodiscard]] std::optional<SpelledNote>
pick_bass_root_below(KeySignature key, int degree, bool harmonic_dom, int max_exclusive_midi) {
  constexpr std::array<int, 3> kBassOcts{3, 2, 1};
  std::optional<SpelledNote> fallback;
  for (const int oct : kBassOcts) {
    const auto triad = build_diatonic_triad(key, degree, oct, Inversion::Root, harmonic_dom);
    if (!triad.has_value()) {
      continue;
    }
    const auto m = to_midi(triad->notes[0]);
    if (!m.has_value() || std::cmp_greater_equal(*m, max_exclusive_midi)) {
      continue;
    }
    if (!fallback.has_value()) {
      fallback = triad->notes[0];
    }
    if (static_cast<int>(*m) >= 40 && static_cast<int>(*m) <= 51) {
      return triad->notes[0];
    }
  }
  return fallback;
}

[[nodiscard]] std::string_view cadence_roman_numeral(Mode mode, int degree, bool harmonic_dom) {
  if (mode == Mode::Major) {
    switch (degree) {
    case 1:
      return "I";
    case 4:
      return "IV";
    case 5:
      return "V";
    default:
      return "?";
    }
  }
  switch (degree) {
  case 1:
    return "i";
  case 4:
    return "iv";
  case 5:
    return harmonic_dom ? "V" : "v";
  default:
    return "?";
  }
}

void finalize_expected_midi(WarmUpStep& step) {
  std::vector<MidiPitch> raw;
  raw.reserve(step.notes.size());
  for (const auto& n : step.notes) {
    raw.push_back(n.midi);
  }
  step.expected_midi = normalize_midi_set(raw);
}

// Standard classical 2-octave ascending scale fingerings indexed by (fifths + 7) in [0..14].
// Thumb (1) never lands on a black key in any standard key.
using ScaleFingerSeq = std::array<std::uint8_t, 15>;

constexpr ScaleFingerSeq kRhCGroup{1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 5};
constexpr ScaleFingerSeq kRhFGroup{1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 4};
constexpr ScaleFingerSeq kRhBbGroup{4, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4};
constexpr ScaleFingerSeq kRhEbGroup{3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3};
constexpr ScaleFingerSeq kRhAbGroup{3, 4, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3};
constexpr ScaleFingerSeq kRhDbGroup{2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 1, 2};
constexpr ScaleFingerSeq kRhGbGroup{2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2};
constexpr ScaleFingerSeq kRhFsmGroup{2, 3, 1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3};

constexpr ScaleFingerSeq kLhCGroup{5, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1};
constexpr ScaleFingerSeq kLhBGroup{4, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1};
constexpr ScaleFingerSeq kLhBbGroup{3, 2, 1, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3};
constexpr ScaleFingerSeq kLhGbGroup{4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1, 4};
constexpr ScaleFingerSeq kLhGsmGroup{3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1, 4, 3};
constexpr ScaleFingerSeq kLhEbmGroup{2, 1, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2};
constexpr ScaleFingerSeq kLhBbmGroup{2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2};

constexpr std::array<ScaleFingerSeq, 15> kRhMajorFingerings{{
    kRhCGroup,  // -7 Cb (same as B)
    kRhGbGroup, // -6 Gb
    kRhDbGroup, // -5 Db
    kRhAbGroup, // -4 Ab
    kRhEbGroup, // -3 Eb
    kRhBbGroup, // -2 Bb
    kRhFGroup,  // -1 F
    kRhCGroup,  //  0 C
    kRhCGroup,  // +1 G
    kRhCGroup,  // +2 D
    kRhCGroup,  // +3 A
    kRhCGroup,  // +4 E
    kRhCGroup,  // +5 B
    kRhGbGroup, // +6 F#
    kRhDbGroup, // +7 C#
}};

constexpr std::array<ScaleFingerSeq, 15> kLhMajorFingerings{{
    kLhBGroup,  // -7 Cb
    kLhGbGroup, // -6 Gb
    kLhBbGroup, // -5 Db
    kLhBbGroup, // -4 Ab
    kLhBbGroup, // -3 Eb
    kLhBbGroup, // -2 Bb
    kLhCGroup,  // -1 F
    kLhCGroup,  //  0 C
    kLhCGroup,  // +1 G
    kLhCGroup,  // +2 D
    kLhCGroup,  // +3 A
    kLhCGroup,  // +4 E
    kLhBGroup,  // +5 B
    kLhGbGroup, // +6 F#
    kLhBbGroup, // +7 C#
}};

constexpr std::array<ScaleFingerSeq, 15> kRhMinorFingerings{{
    kRhAbGroup,  // -7 Abm
    kRhEbGroup,  // -6 Ebm
    kRhBbGroup,  // -5 Bbm
    kRhFGroup,   // -4 Fm
    kRhCGroup,   // -3 Cm
    kRhCGroup,   // -2 Gm
    kRhCGroup,   // -1 Dm
    kRhCGroup,   //  0 Am
    kRhCGroup,   // +1 Em
    kRhCGroup,   // +2 Bm
    kRhFsmGroup, // +3 F#m
    kRhAbGroup,  // +4 C#m
    kRhAbGroup,  // +5 G#m
    kRhEbGroup,  // +6 D#m
    kRhBbGroup,  // +7 A#m
}};

constexpr std::array<ScaleFingerSeq, 15> kLhMinorFingerings{{
    kLhGsmGroup, // -7 Abm
    kLhEbmGroup, // -6 Ebm
    kLhBbmGroup, // -5 Bbm
    kLhCGroup,   // -4 Fm
    kLhCGroup,   // -3 Cm
    kLhCGroup,   // -2 Gm
    kLhCGroup,   // -1 Dm
    kLhCGroup,   //  0 Am
    kLhCGroup,   // +1 Em
    kLhBGroup,   // +2 Bm
    kLhGbGroup,  // +3 F#m
    kLhBbGroup,  // +4 C#m
    kLhGsmGroup, // +5 G#m
    kLhEbmGroup, // +6 D#m
    kLhBbmGroup, // +7 A#m
}};

[[nodiscard]] std::uint8_t scale_finger_at(KeySignature key, bool is_rh, int octaves,
                                           std::size_t asc_idx) noexcept {
  const int f_idx = std::clamp(key.fifths + 7, 0, 14);
  const auto& seq = (key.mode == Mode::Major)
                        ? (is_rh ? kRhMajorFingerings[static_cast<std::size_t>(f_idx)]
                                 : kLhMajorFingerings[static_cast<std::size_t>(f_idx)])
                        : (is_rh ? kRhMinorFingerings[static_cast<std::size_t>(f_idx)]
                                 : kLhMinorFingerings[static_cast<std::size_t>(f_idx)]);
  const std::size_t idx = std::min<std::size_t>(asc_idx, seq.size() - 1U);
  if (octaves == 1 && idx == 7U && is_rh && seq[7] == 1U) {
    return (seq[6] == 4U) ? 5U : 4U;
  }
  return seq[idx];
}

[[nodiscard]] std::uint8_t tonic_arpeggio_finger(bool is_rh, std::size_t asc_idx,
                                                 std::size_t total_asc) noexcept {
  if (is_rh) {
    if (asc_idx + 1U >= total_asc) {
      return 5U;
    }
    constexpr std::array<std::uint8_t, 3> kPat{1U, 2U, 3U};
    return kPat[asc_idx % 3U];
  }
  constexpr std::array<std::uint8_t, 3> kLhPat{5U, 4U, 2U};
  if (asc_idx == 0U) {
    return 5U;
  }
  if (asc_idx + 1U >= total_asc) {
    return 1U;
  }
  return (asc_idx % 3U == 0U) ? 1U : kLhPat[asc_idx % 3U];
}

[[nodiscard]] std::uint8_t seventh_arpeggio_finger(bool is_rh, std::size_t asc_idx,
                                                   std::size_t total_asc) noexcept {
  if (is_rh) {
    if (asc_idx + 1U >= total_asc) {
      return 5U;
    }
    return static_cast<std::uint8_t>((asc_idx % 4U) + 1U);
  }
  if (asc_idx == 0U) {
    return 5U;
  }
  if (asc_idx + 1U >= total_asc) {
    return 1U;
  }
  return static_cast<std::uint8_t>(4U - ((asc_idx - 1U) % 4U));
}

[[nodiscard]] SpelledNote spell_chromatic_midi(MidiPitch midi, bool ascending) noexcept {
  const int oct = static_cast<int>(midi) / 12 - 1;
  const int pc = static_cast<int>(midi) % 12;
  constexpr std::array<std::pair<NoteLetter, Accidental>, 12> kSharpSpell{{
      {NoteLetter::C, Accidental::Natural},
      {NoteLetter::C, Accidental::Sharp},
      {NoteLetter::D, Accidental::Natural},
      {NoteLetter::D, Accidental::Sharp},
      {NoteLetter::E, Accidental::Natural},
      {NoteLetter::F, Accidental::Natural},
      {NoteLetter::F, Accidental::Sharp},
      {NoteLetter::G, Accidental::Natural},
      {NoteLetter::G, Accidental::Sharp},
      {NoteLetter::A, Accidental::Natural},
      {NoteLetter::A, Accidental::Sharp},
      {NoteLetter::B, Accidental::Natural},
  }};
  constexpr std::array<std::pair<NoteLetter, Accidental>, 12> kFlatSpell{{
      {NoteLetter::C, Accidental::Natural},
      {NoteLetter::D, Accidental::Flat},
      {NoteLetter::D, Accidental::Natural},
      {NoteLetter::E, Accidental::Flat},
      {NoteLetter::E, Accidental::Natural},
      {NoteLetter::F, Accidental::Natural},
      {NoteLetter::G, Accidental::Flat},
      {NoteLetter::G, Accidental::Natural},
      {NoteLetter::A, Accidental::Flat},
      {NoteLetter::A, Accidental::Natural},
      {NoteLetter::B, Accidental::Flat},
      {NoteLetter::B, Accidental::Natural},
  }};
  const auto [letter, acc] = ascending ? kSharpSpell[static_cast<std::size_t>(pc)]
                                       : kFlatSpell[static_cast<std::size_t>(pc)];
  return SpelledNote{.letter = letter, .accidental = acc, .octave = oct};
}

[[nodiscard]] std::uint8_t chromatic_finger(MidiPitch midi, bool is_rh) noexcept {
  const int pc = static_cast<int>(midi) % 12;
  const bool is_black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
  if (is_black) {
    return 3U;
  }
  if (is_rh) {
    return (pc == 0 || pc == 5) ? 2U : 1U;
  }
  return (pc == 4 || pc == 11) ? 2U : 1U;
}

} // namespace

WarmUpEngine::WarmUpEngine(WarmUpConfig config) : config_{config} {
  config_.octaves = std::clamp(config_.octaves, 1, 2);
  config_.start_bpm = std::clamp(config_.start_bpm, kMinWarmUpBpm, kMaxWarmUpBpm);
  current_bpm_ = config_.start_bpm;
  pending_next_bpm_ = config_.start_bpm;
  rebuild_steps();
}

void WarmUpEngine::configure(WarmUpConfig config) {
  const bool key_changed = (config.key != config_.key);
  config.octaves = std::clamp(config.octaves, 1, 2);
  config.start_bpm = std::clamp(config.start_bpm, kMinWarmUpBpm, kMaxWarmUpBpm);
  config_ = config;
  if (key_changed) {
    best_clean_bpm_.reset();
    pass_number_ = 1;
    pacing_mode_ = WarmUpPacingMode::ExploreWait;
    current_bpm_ = config_.start_bpm;
    pending_next_bpm_ = config_.start_bpm;
    last_pass_summary_.reset();
  } else {
    current_bpm_ = std::clamp(config_.start_bpm, kMinWarmUpBpm, kMaxWarmUpBpm);
    pending_next_bpm_ = current_bpm_;
  }
  rebuild_steps();
}

void WarmUpEngine::reset() {
  begin_pass(pacing_mode_, current_bpm_, pacing_mode_ == WarmUpPacingMode::GuidedPulse);
  // Manual reset starts immediately in Playing state so the user can begin step 1 without delay.
  phase_state_ = WarmUpPhaseState::Playing;
  count_in_beats_done_ = 0;
  beat_phase_ms_ = 0.0;
  metronome_beat_index_ = 0;
  paused_ = false;
}

void WarmUpEngine::set_paused(bool paused) noexcept {
  paused_ = paused;
}

void WarmUpEngine::set_bpm(double bpm) noexcept {
  current_bpm_ = std::clamp(bpm, kMinWarmUpBpm, kMaxWarmUpBpm);
  config_.start_bpm = current_bpm_;
  pending_next_bpm_ = current_bpm_;
}

void WarmUpEngine::set_pacing_mode(WarmUpPacingMode mode) noexcept {
  pacing_mode_ = mode;
  pending_next_mode_ = mode;
  beat_phase_ms_ = 0.0;
  since_last_hit_ms_ = 0.0;
  evenness_sum_ = 0.0;
  evenness_samples_ = 0;
  last_timing_hint_ = {};
  if (phase_state_ == WarmUpPhaseState::CountIn && mode == WarmUpPacingMode::ExploreWait) {
    phase_state_ = WarmUpPhaseState::Playing;
  }
}

void WarmUpEngine::skip_transition_now() noexcept {
  if (phase_state_ != WarmUpPhaseState::PassTransition) {
    return;
  }
  ++pass_number_;
  begin_pass(pending_next_mode_, pending_next_bpm_,
             pending_next_mode_ == WarmUpPacingMode::GuidedPulse);
}

void WarmUpEngine::begin_pass(WarmUpPacingMode mode, double bpm, bool with_count_in) noexcept {
  pacing_mode_ = mode;
  current_bpm_ = std::clamp(bpm, kMinWarmUpBpm, kMaxWarmUpBpm);
  current_step_index_ = 0;
  std::ranges::fill(feedback_, WarmUpStepFeedback::Pending);
  last_played_midi_.clear();
  pass_attempts_ = 0;
  pass_correct_ = 0;
  evenness_sum_ = 0.0;
  evenness_samples_ = 0;
  since_last_hit_ms_ = 0.0;
  step_had_mistake_ = false;
  last_timing_hint_ = {};
  beat_phase_ms_ = 0.0;
  metronome_beat_index_ = 0;
  transition_elapsed_ms_ = 0.0;
  if (with_count_in && mode == WarmUpPacingMode::GuidedPulse) {
    phase_state_ = WarmUpPhaseState::CountIn;
    count_in_beats_done_ = 1;
  } else {
    phase_state_ = WarmUpPhaseState::Playing;
    count_in_beats_done_ = 0;
  }
}

WarmUpEvalResult WarmUpEngine::submit_played_notes(std::span<const MidiPitch> played_midi) {
  auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty()) {
    return WarmUpEvalResult{};
  }
  if (phase_state_ == WarmUpPhaseState::CountIn) {
    return WarmUpEvalResult{};
  }
  if (is_complete() || phase_state_ == WarmUpPhaseState::PassTransition) {
    return WarmUpEvalResult{
        .status = WarmUpEvalStatus::AlreadyComplete,
        .evaluated_step_index = steps_.size(),
        .expected_midi = {},
        .played_midi = std::move(normalized),
        .exercise_completed = false,
        .station_completed = false,
    };
  }

  last_played_midi_ = normalized;
  ++total_attempts_;
  ++pass_attempts_;

  const std::size_t idx = current_step_index_;
  const auto& step = steps_[idx];

  if (normalized == step.expected_midi) {
    ++correct_attempts_;
    ++pass_correct_;
    feedback_[idx] = WarmUpStepFeedback::Correct;

    if (pacing_mode_ == WarmUpPacingMode::GuidedPulse && idx > 0) {
      const double beat_ms = 60000.0 / current_bpm_;
      const double expected_dt_ms =
          static_cast<double>(std::max(1, steps_[idx - 1].beat_duration)) * beat_ms;
      double step_score = 0.25;
      if (!step_had_mistake_ && since_last_hit_ms_ <= expected_dt_ms * 2.5) {
        const double rel_err = std::abs(since_last_hit_ms_ - expected_dt_ms) / expected_dt_ms;
        step_score = std::clamp(1.0 - rel_err * 1.25, 0.0, 1.0);
        if (rel_err <= 0.15) {
          last_timing_hint_ = "Steady";
        } else if (since_last_hit_ms_ < expected_dt_ms) {
          last_timing_hint_ = "Slightly rushed";
        } else {
          last_timing_hint_ = "Slightly behind";
        }
      } else if (step_had_mistake_) {
        last_timing_hint_ = "Recovered";
      } else {
        last_timing_hint_ = "Paused";
      }
      evenness_sum_ += step_score;
      ++evenness_samples_;
    }

    since_last_hit_ms_ = 0.0;
    step_had_mistake_ = false;
    ++current_step_index_;

    const bool completed = is_complete();
    const bool station_done =
        completed || (steps_[current_step_index_].station != steps_[idx].station);

    if (completed) {
      ++completed_runs_;
      const double pitch_acc = pass_accuracy_ratio();
      const double evenness = pass_evenness_ratio();
      WarmUpPacingMode next_mode = pacing_mode_;
      double next_bpm = current_bpm_;
      bool bpm_bumped = false;

      if (pacing_mode_ == WarmUpPacingMode::ExploreWait) {
        if (pitch_acc >= kWarmUpCleanPitchThreshold) {
          next_mode = WarmUpPacingMode::GuidedPulse;
          next_bpm = current_bpm_;
        }
      } else {
        const bool clean_pulse = (pitch_acc >= kWarmUpCleanPitchThreshold) &&
                                 (evenness >= kWarmUpCleanEvennessThreshold);
        if (clean_pulse) {
          best_clean_bpm_ =
              best_clean_bpm_.has_value() ? std::max(*best_clean_bpm_, current_bpm_) : current_bpm_;
          next_bpm = std::clamp(current_bpm_ + kWarmUpBpmStep, kMinWarmUpBpm, kMaxWarmUpBpm);
          bpm_bumped = (next_bpm > current_bpm_ + 0.01);
        }
      }

      pending_next_mode_ = next_mode;
      pending_next_bpm_ = next_bpm;
      last_pass_summary_ = WarmUpPassSummary{
          .pass_number = pass_number_,
          .completed_mode = pacing_mode_,
          .completed_bpm = current_bpm_,
          .pitch_accuracy = pitch_acc,
          .evenness_ratio = evenness,
          .bpm_increased = bpm_bumped,
          .next_pacing_mode = next_mode,
          .next_bpm = next_bpm,
      };
      if (config_.auto_continue_passes) {
        phase_state_ = WarmUpPhaseState::PassTransition;
        transition_elapsed_ms_ = 0.0;
      }
    }

    return WarmUpEvalResult{
        .status = WarmUpEvalStatus::Correct,
        .evaluated_step_index = idx,
        .expected_midi = step.expected_midi,
        .played_midi = std::move(normalized),
        .exercise_completed = completed,
        .station_completed = station_done,
    };
  }

  step_had_mistake_ = true;
  feedback_[idx] = WarmUpStepFeedback::Incorrect;
  return WarmUpEvalResult{
      .status = WarmUpEvalStatus::Incorrect,
      .evaluated_step_index = idx,
      .expected_midi = step.expected_midi,
      .played_midi = std::move(normalized),
      .exercise_completed = false,
      .station_completed = false,
  };
}

WarmUpTickResult WarmUpEngine::update(double dt_ms) noexcept {
  WarmUpTickResult tick{};
  if (paused_ || dt_ms <= 0.0) {
    return tick;
  }

  if (phase_state_ == WarmUpPhaseState::PassTransition) {
    transition_elapsed_ms_ += dt_ms;
    if (transition_elapsed_ms_ >= kWarmUpTransitionMs) {
      ++pass_number_;
      const bool use_count_in = (pending_next_mode_ == WarmUpPacingMode::GuidedPulse);
      begin_pass(pending_next_mode_, pending_next_bpm_, use_count_in);
      if (use_count_in) {
        tick.metronome_click = true;
        tick.metronome_accent = true;
      } else {
        tick.pass_started = true;
      }
    }
    return tick;
  }

  if (phase_state_ == WarmUpPhaseState::CountIn) {
    const double beat_ms = 60000.0 / current_bpm_;
    beat_phase_ms_ += dt_ms;
    while (beat_phase_ms_ >= beat_ms && phase_state_ == WarmUpPhaseState::CountIn) {
      beat_phase_ms_ -= beat_ms;
      ++count_in_beats_done_;
      if (count_in_beats_done_ <= 4) {
        tick.metronome_click = true;
        tick.metronome_accent = false;
      } else {
        phase_state_ = WarmUpPhaseState::Playing;
        since_last_hit_ms_ = 0.0;
        metronome_beat_index_ = 0;
        tick.pass_started = true;
        tick.metronome_click = true;
        tick.metronome_accent = true;
      }
    }
    return tick;
  }

  if (phase_state_ == WarmUpPhaseState::Playing && pacing_mode_ == WarmUpPacingMode::GuidedPulse &&
      !is_complete()) {
    since_last_hit_ms_ += dt_ms;
    const double beat_ms = 60000.0 / current_bpm_;
    beat_phase_ms_ += dt_ms;
    if (beat_phase_ms_ >= beat_ms) {
      beat_phase_ms_ = std::fmod(beat_phase_ms_, beat_ms);
      ++metronome_beat_index_;
      tick.metronome_click = true;
      tick.metronome_accent = ((metronome_beat_index_ % 4) == 0);
    }
  }

  return tick;
}

bool WarmUpEngine::is_mid_station() const noexcept {
  if (steps_.empty() || current_step_index_ == 0 || is_complete() ||
      phase_state_ == WarmUpPhaseState::PassTransition) {
    return false;
  }
  return steps_[current_step_index_].station == steps_[current_step_index_ - 1].station;
}

std::size_t WarmUpEngine::expected_cardinality() const noexcept {
  if (is_complete()) {
    return 1;
  }
  const std::size_t sz = steps_[current_step_index_].expected_midi.size();
  return sz > 0 ? sz : 1;
}

WarmUpStepFeedback WarmUpEngine::step_feedback(std::size_t idx) const noexcept {
  if (idx >= feedback_.size()) {
    return WarmUpStepFeedback::Pending;
  }
  return feedback_[idx];
}

double WarmUpEngine::accuracy_ratio() const noexcept {
  if (total_attempts_ == 0) {
    return 0.0;
  }
  return static_cast<double>(correct_attempts_) / static_cast<double>(total_attempts_);
}

double WarmUpEngine::pass_accuracy_ratio() const noexcept {
  if (pass_attempts_ == 0) {
    return 0.0;
  }
  return static_cast<double>(pass_correct_) / static_cast<double>(pass_attempts_);
}

double WarmUpEngine::pass_evenness_ratio() const noexcept {
  if (evenness_samples_ == 0) {
    return 0.0;
  }
  return evenness_sum_ / static_cast<double>(evenness_samples_);
}

double WarmUpEngine::transition_remaining_ms() const noexcept {
  if (phase_state_ != WarmUpPhaseState::PassTransition) {
    return 0.0;
  }
  return std::max(0.0, kWarmUpTransitionMs - transition_elapsed_ms_);
}

std::optional<WarmUpStation> WarmUpEngine::active_station() const noexcept {
  if (steps_.empty()) {
    return std::nullopt;
  }
  if (current_step_index_ < steps_.size()) {
    return steps_[current_step_index_].station;
  }
  return steps_.back().station;
}

void WarmUpEngine::rebuild_steps() {
  steps_.clear();
  feedback_.clear();
  current_step_index_ = 0;
  last_played_midi_.clear();
  pass_attempts_ = 0;
  pass_correct_ = 0;
  evenness_sum_ = 0.0;
  evenness_samples_ = 0;
  since_last_hit_ms_ = 0.0;
  step_had_mistake_ = false;
  last_timing_hint_ = {};
  phase_state_ = WarmUpPhaseState::Playing;
  transition_elapsed_ms_ = 0.0;
  count_in_beats_done_ = 0;
  beat_phase_ms_ = 0.0;

  const bool include_rh =
      (config_.hand == HandSelection::HandsTogether || config_.hand == HandSelection::RightOnly);
  const bool include_lh =
      (config_.hand == HandSelection::HandsTogether || config_.hand == HandSelection::LeftOnly);

  const bool include_scale = (config_.exercise == WarmUpExerciseKind::FullCircuit ||
                              config_.exercise == WarmUpExerciseKind::RomanticCircuit ||
                              config_.exercise == WarmUpExerciseKind::ScaleAndCadence ||
                              config_.exercise == WarmUpExerciseKind::ScaleOnly);
  const bool include_arpeggio = (config_.exercise == WarmUpExerciseKind::FullCircuit ||
                                 config_.exercise == WarmUpExerciseKind::ArpeggioOnly);
  const bool include_inversions = (config_.exercise == WarmUpExerciseKind::FullCircuit ||
                                   config_.exercise == WarmUpExerciseKind::InversionsOnly);
  const bool include_cadence = (config_.exercise == WarmUpExerciseKind::FullCircuit ||
                                config_.exercise == WarmUpExerciseKind::ScaleAndCadence ||
                                config_.exercise == WarmUpExerciseKind::CadenceOnly);
  const bool include_chromatic = (config_.exercise == WarmUpExerciseKind::RomanticCircuit ||
                                  config_.exercise == WarmUpExerciseKind::ChromaticOnly);
  const bool include_seventh_arp = (config_.exercise == WarmUpExerciseKind::RomanticCircuit ||
                                    config_.exercise == WarmUpExerciseKind::SeventhArpeggiosOnly);
  const bool include_contrary = (config_.exercise == WarmUpExerciseKind::ContraryScaleOnly);
  const bool include_stride = (config_.exercise == WarmUpExerciseKind::RomanticCircuit ||
                               config_.exercise == WarmUpExerciseKind::LhStrideOnly);

  const ScaleType scale_type =
      (config_.key.mode == Mode::Major)
          ? ScaleType::Major
          : ((config_.minor_mode == MinorScaleMode::Harmonic) ? ScaleType::HarmonicMinor
                                                              : ScaleType::NaturalMinor);
  const auto tonic = tonic_of(config_.key);
  const bool harmonic_dom =
      (config_.key.mode == Mode::Minor && config_.minor_mode == MinorScaleMode::Harmonic);

  int scale_rh_oct = 4;
  int scale_lh_oct = 3;
  if (config_.octaves == 1) {
    scale_rh_oct = 4;
    scale_lh_oct = (tonic.letter <= NoteLetter::E) ? 3 : 2;
  } else {
    scale_rh_oct = (tonic.letter <= NoteLetter::F) ? 4 : 3;
    scale_lh_oct = (tonic.letter <= NoteLetter::G) ? 2 : 1;
  }

  std::optional<std::vector<SpelledNote>> rh_scale_cache;
  std::optional<std::vector<SpelledNote>> lh_scale_cache;
  if (include_scale || include_arpeggio || include_contrary) {
    if (const auto s = build_scale(config_.key, scale_type, scale_rh_oct, config_.octaves);
        s.has_value()) {
      rh_scale_cache = *s;
    }
    if (const auto s = build_scale(config_.key, scale_type, scale_lh_oct, config_.octaves);
        s.has_value()) {
      lh_scale_cache = *s;
    }
  }

  auto emit_parallel_note_step = [&](const SpelledNote& rh_sn, const SpelledNote& lh_sn,
                                     WarmUpStation station, bool descending,
                                     std::uint8_t rh_finger = 0, std::uint8_t lh_finger = 0) {
    WarmUpStep step{};
    step.station = station;
    step.beat_duration = 1;
    step.is_cadence = false;
    step.is_descending = descending;
    if (include_lh) {
      if (const auto midi = to_midi(lh_sn); midi.has_value()) {
        step.notes.push_back(
            WarmUpNote{.spelled = lh_sn, .clef = Clef::Bass, .midi = *midi, .finger = lh_finger});
      }
    }
    if (include_rh) {
      if (const auto midi = to_midi(rh_sn); midi.has_value()) {
        step.notes.push_back(
            WarmUpNote{.spelled = rh_sn, .clef = Clef::Treble, .midi = *midi, .finger = rh_finger});
      }
    }
    step.label = include_rh ? format_note(rh_sn) : format_note(lh_sn);
    finalize_expected_midi(step);
    steps_.push_back(std::move(step));
  };

  // Station 1: Parallel Scale
  if (include_scale && rh_scale_cache.has_value() && lh_scale_cache.has_value() &&
      rh_scale_cache->size() == lh_scale_cache->size() && !rh_scale_cache->empty()) {
    const std::size_t n = rh_scale_cache->size();
    for (std::size_t i = 0; i < n; ++i) {
      emit_parallel_note_step((*rh_scale_cache)[i], (*lh_scale_cache)[i],
                              WarmUpStation::ParallelScale, false,
                              scale_finger_at(config_.key, true, config_.octaves, i),
                              scale_finger_at(config_.key, false, config_.octaves, i));
    }
    for (std::size_t i = n - 1; i-- > 0;) {
      emit_parallel_note_step((*rh_scale_cache)[i], (*lh_scale_cache)[i],
                              WarmUpStation::ParallelScale, true,
                              scale_finger_at(config_.key, true, config_.octaves, i),
                              scale_finger_at(config_.key, false, config_.octaves, i));
    }
  }

  // Station 2: Tonic Arpeggio (degrees 1-3-5-8... up & down)
  if (include_arpeggio && rh_scale_cache.has_value() && lh_scale_cache.has_value() &&
      rh_scale_cache->size() == lh_scale_cache->size() && !rh_scale_cache->empty()) {
    std::vector<std::size_t> arp_indices;
    for (std::size_t i = 0; i < rh_scale_cache->size(); ++i) {
      const std::size_t deg = i % 7U;
      if (deg == 0U || deg == 2U || deg == 4U) {
        arp_indices.push_back(i);
      }
    }
    const std::size_t m = arp_indices.size();
    for (std::size_t k = 0; k < m; ++k) {
      const std::size_t idx = arp_indices[k];
      emit_parallel_note_step(
          (*rh_scale_cache)[idx], (*lh_scale_cache)[idx], WarmUpStation::TonicArpeggio, false,
          tonic_arpeggio_finger(true, k, m), tonic_arpeggio_finger(false, k, m));
    }
    for (std::size_t k = m - 1; k-- > 0;) {
      const std::size_t idx = arp_indices[k];
      emit_parallel_note_step((*rh_scale_cache)[idx], (*lh_scale_cache)[idx],
                              WarmUpStation::TonicArpeggio, true, tonic_arpeggio_finger(true, k, m),
                              tonic_arpeggio_finger(false, k, m));
    }
  }

  // Station 3: Triad Inversions (Alternating Hands in HandsTogether: 7 RH then 7 LH)
  if (include_inversions) {
    auto emit_inversion_arc = [&](Clef clef, int base_oct) {
      const auto c0 = build_diatonic_triad(config_.key, 1, base_oct, Inversion::Root, harmonic_dom);
      const auto c1 =
          build_diatonic_triad(config_.key, 1, base_oct, Inversion::First, harmonic_dom);
      const auto c2 =
          build_diatonic_triad(config_.key, 1, base_oct, Inversion::Second, harmonic_dom);
      const auto c3 =
          build_diatonic_triad(config_.key, 1, base_oct + 1, Inversion::Root, harmonic_dom);
      if (!c0.has_value() || !c1.has_value() || !c2.has_value() || !c3.has_value()) {
        return;
      }

      const std::string sym = format_chord_symbol(*c0);
      struct InvSlot {
        Triad triad;
        std::string_view suffix;
        bool descending;
        std::array<std::uint8_t, 3> fingers;
      };
      constexpr std::array<std::uint8_t, 3> kRhRootF{1, 3, 5};
      constexpr std::array<std::uint8_t, 3> kRhInv1F{1, 2, 5};
      constexpr std::array<std::uint8_t, 3> kRhInv2F{1, 3, 5};
      constexpr std::array<std::uint8_t, 3> kLhRootF{5, 3, 1};
      constexpr std::array<std::uint8_t, 3> kLhInv1F{5, 3, 1};
      constexpr std::array<std::uint8_t, 3> kLhInv2F{5, 2, 1};
      const bool rh = (clef == Clef::Treble);
      const std::array<InvSlot, 7> arc{{
          {.triad = *c0,
           .suffix = "Root",
           .descending = false,
           .fingers = rh ? kRhRootF : kLhRootF},
          {.triad = *c1, .suffix = "1st", .descending = false, .fingers = rh ? kRhInv1F : kLhInv1F},
          {.triad = *c2, .suffix = "2nd", .descending = false, .fingers = rh ? kRhInv2F : kLhInv2F},
          {.triad = *c3, .suffix = "8va", .descending = false, .fingers = rh ? kRhRootF : kLhRootF},
          {.triad = *c2, .suffix = "2nd", .descending = true, .fingers = rh ? kRhInv2F : kLhInv2F},
          {.triad = *c1, .suffix = "1st", .descending = true, .fingers = rh ? kRhInv1F : kLhInv1F},
          {.triad = *c0, .suffix = "Root", .descending = true, .fingers = rh ? kRhRootF : kLhRootF},
      }};

      const char* hand_tag = (clef == Clef::Treble) ? "RH" : "LH";
      for (const auto& slot : arc) {
        WarmUpStep step{};
        step.station = WarmUpStation::TriadInversions;
        step.beat_duration = 2;
        step.solo_hand_clef = clef;
        step.is_cadence = false;
        step.is_descending = slot.descending;
        for (std::size_t ni = 0; ni < slot.triad.notes.size(); ++ni) {
          const auto& sn = slot.triad.notes[ni];
          if (const auto m = to_midi(sn); m.has_value()) {
            step.notes.push_back(
                WarmUpNote{.spelled = sn, .clef = clef, .midi = *m, .finger = slot.fingers[ni]});
          }
        }
        step.label = (config_.hand == HandSelection::HandsTogether)
                         ? std::format("{} {} {}", hand_tag, sym, slot.suffix)
                         : std::format("{} {}", sym, slot.suffix);
        finalize_expected_midi(step);
        steps_.push_back(std::move(step));
      }
    };

    const int inv_rh_oct = (tonic.letter <= NoteLetter::F) ? 4 : 3;
    constexpr int kInvLhOct = 2;
    if (include_rh) {
      emit_inversion_arc(Clef::Treble, inv_rh_oct);
    }
    if (include_lh) {
      emit_inversion_arc(Clef::Bass, kInvLhOct);
    }
  }

  // Station 4: Voice-Led Primary Cadence (I -> IV6/4 -> V6 -> I)
  if (include_cadence) {
    constexpr std::array<int, 2> kRhOctaves{4, 3};
    constexpr std::array<int, 2> kLhOctaves{3, 2};

    const auto rh_tonic =
        pick_triad_in_midi_range(config_.key, 1, harmonic_dom, kRhOctaves, 57, 68);
    const auto lh_tonic =
        pick_triad_in_midi_range(config_.key, 1, harmonic_dom, kLhOctaves, 40, 51);

    if (rh_tonic.has_value() && lh_tonic.has_value()) {
      const int rh_bot = static_cast<int>(to_midi(rh_tonic->notes[0]).value());
      const int rh_top = static_cast<int>(to_midi(rh_tonic->notes[2]).value());
      const int lh_bot = static_cast<int>(to_midi(lh_tonic->notes[0]).value());
      const int lh_top = static_cast<int>(to_midi(lh_tonic->notes[2]).value());

      const auto rh_iv =
          pick_voice_led_triad(config_.key, 4, Inversion::Second, harmonic_dom, rh_bot, 0);
      const auto rh_v =
          pick_voice_led_triad(config_.key, 5, Inversion::First, harmonic_dom, rh_top, 2);
      const auto lh_iv =
          pick_voice_led_triad(config_.key, 4, Inversion::Second, harmonic_dom, lh_bot, 0);
      const auto lh_v =
          pick_voice_led_triad(config_.key, 5, Inversion::First, harmonic_dom, lh_top, 2);

      if (rh_iv.has_value() && rh_v.has_value() && lh_iv.has_value() && lh_v.has_value()) {
        struct CadenceSlot {
          int degree;
          Triad rh;
          Triad lh;
        };
        const std::array<CadenceSlot, 4> slots{{
            {.degree = 1, .rh = *rh_tonic, .lh = *lh_tonic},
            {.degree = 4, .rh = *rh_iv, .lh = *lh_iv},
            {.degree = 5, .rh = *rh_v, .lh = *lh_v},
            {.degree = 1, .rh = *rh_tonic, .lh = *lh_tonic},
        }};

        for (const auto& slot : slots) {
          WarmUpStep step{};
          step.station = WarmUpStation::VoiceLedCadence;
          step.beat_duration = 2;
          step.is_cadence = true;
          step.is_descending = false;

          if (config_.hand == HandSelection::HandsTogether) {
            int rh_min_midi = 127;
            for (const auto& sn : slot.rh.notes) {
              if (const auto m = to_midi(sn); m.has_value()) {
                rh_min_midi = std::min(rh_min_midi, static_cast<int>(*m));
              }
            }
            if (const auto bass_root =
                    pick_bass_root_below(config_.key, slot.degree, harmonic_dom, rh_min_midi);
                bass_root.has_value()) {
              if (const auto bm = to_midi(*bass_root); bm.has_value()) {
                step.notes.push_back(
                    WarmUpNote{.spelled = *bass_root, .clef = Clef::Bass, .midi = *bm});
              }
            }
            for (const auto& sn : slot.rh.notes) {
              if (const auto m = to_midi(sn); m.has_value()) {
                step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Treble, .midi = *m});
              }
            }
          } else if (config_.hand == HandSelection::RightOnly) {
            for (const auto& sn : slot.rh.notes) {
              if (const auto m = to_midi(sn); m.has_value()) {
                step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Treble, .midi = *m});
              }
            }
          } else {
            for (const auto& sn : slot.lh.notes) {
              if (const auto m = to_midi(sn); m.has_value()) {
                step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Bass, .midi = *m});
              }
            }
          }

          const auto roman = cadence_roman_numeral(config_.key.mode, slot.degree, harmonic_dom);
          const auto& chord_for_sym = (config_.hand == HandSelection::LeftOnly) ? slot.lh : slot.rh;
          step.label = std::format("{} ({})", roman, format_chord_symbol(chord_for_sym));
          finalize_expected_midi(step);
          steps_.push_back(std::move(step));
        }
      }
    }
  }

  // Station 5: Chromatic Scale (1–2 octaves from tonic)
  if (include_chromatic) {
    const SpelledNote rh_tonic{
        .letter = tonic.letter, .accidental = tonic.accidental, .octave = scale_rh_oct};
    const SpelledNote lh_tonic{
        .letter = tonic.letter, .accidental = tonic.accidental, .octave = scale_lh_oct};
    const auto rh_base = to_midi(rh_tonic);
    const auto lh_base = to_midi(lh_tonic);
    if (rh_base.has_value() && lh_base.has_value()) {
      const int total_semis = 12 * config_.octaves;
      for (int i = 0; i <= total_semis; ++i) {
        const auto rm = static_cast<MidiPitch>(static_cast<int>(*rh_base) + i);
        const auto lm = static_cast<MidiPitch>(static_cast<int>(*lh_base) + i);
        emit_parallel_note_step(spell_chromatic_midi(rm, true), spell_chromatic_midi(lm, true),
                                WarmUpStation::ChromaticScale, false, chromatic_finger(rm, true),
                                chromatic_finger(lm, false));
      }
      for (int i = total_semis - 1; i >= 0; --i) {
        const auto rm = static_cast<MidiPitch>(static_cast<int>(*rh_base) + i);
        const auto lm = static_cast<MidiPitch>(static_cast<int>(*lh_base) + i);
        emit_parallel_note_step(spell_chromatic_midi(rm, false), spell_chromatic_midi(lm, false),
                                WarmUpStation::ChromaticScale, true, chromatic_finger(rm, true),
                                chromatic_finger(lm, false));
      }
    }
  }

  // Station 6: Dominant 7th (V7) & Diminished 7th (vii°7) Arpeggios
  if (include_seventh_arp) {
    const auto dom_triad =
        build_diatonic_triad(config_.key, 5, scale_rh_oct, Inversion::Root, true);
    const auto rh_tonic_m = to_midi(SpelledNote{
        .letter = tonic.letter, .accidental = tonic.accidental, .octave = scale_rh_oct});
    const auto lh_tonic_m = to_midi(SpelledNote{
        .letter = tonic.letter, .accidental = tonic.accidental, .octave = scale_lh_oct});
    if (dom_triad.has_value() && rh_tonic_m.has_value() && lh_tonic_m.has_value()) {
      int dom_rh_root = static_cast<int>(to_midi(dom_triad->notes[0]).value_or(*rh_tonic_m + 7));
      while (dom_rh_root > static_cast<int>(*rh_tonic_m) + 7) {
        dom_rh_root -= 12;
      }
      while (dom_rh_root < static_cast<int>(*rh_tonic_m) - 5) {
        dom_rh_root += 12;
      }
      const int dom_lh_root =
          dom_rh_root - (static_cast<int>(*rh_tonic_m) - static_cast<int>(*lh_tonic_m));

      auto emit_seventh_arp = [&](int rh_root, int lh_root, const std::array<int, 4>& intervals,
                                  std::string_view tag) {
        const std::size_t total_asc = static_cast<std::size_t>(4 * config_.octaves + 1);
        std::vector<MidiPitch> rh_seq;
        std::vector<MidiPitch> lh_seq;
        rh_seq.reserve(total_asc);
        lh_seq.reserve(total_asc);
        for (std::size_t k = 0; k < total_asc; ++k) {
          const int oct_off = static_cast<int>(k / 4U) * 12;
          const int iv = intervals[k % 4U];
          rh_seq.push_back(static_cast<MidiPitch>(rh_root + oct_off + iv));
          lh_seq.push_back(static_cast<MidiPitch>(lh_root + oct_off + iv));
        }
        for (std::size_t k = 0; k < total_asc; ++k) {
          const auto r_sn = spell_chromatic_midi(rh_seq[k], true);
          const auto l_sn = spell_chromatic_midi(lh_seq[k], true);
          emit_parallel_note_step(r_sn, l_sn, WarmUpStation::SeventhArpeggios, false,
                                  seventh_arpeggio_finger(true, k, total_asc),
                                  seventh_arpeggio_finger(false, k, total_asc));
          steps_.back().label = std::format("{} {}", tag, steps_.back().label);
        }
        for (std::size_t k = total_asc - 1; k-- > 0;) {
          const auto r_sn = spell_chromatic_midi(rh_seq[k], false);
          const auto l_sn = spell_chromatic_midi(lh_seq[k], false);
          emit_parallel_note_step(r_sn, l_sn, WarmUpStation::SeventhArpeggios, true,
                                  seventh_arpeggio_finger(true, k, total_asc),
                                  seventh_arpeggio_finger(false, k, total_asc));
          steps_.back().label = std::format("{} {}", tag, steps_.back().label);
        }
      };

      emit_seventh_arp(dom_rh_root, dom_lh_root, std::array<int, 4>{0, 4, 7, 10}, "V7");
      emit_seventh_arp(static_cast<int>(*rh_tonic_m) - 1, static_cast<int>(*lh_tonic_m) - 1,
                       std::array<int, 4>{0, 3, 6, 9},
                       "vii\xC2\xB0"
                       "7");
    }
  }

  // Station 7: Contrary Motion Scale (RH ascends & descends; LH descends & ascends)
  if (include_contrary && rh_scale_cache.has_value() && !rh_scale_cache->empty()) {
    const int lh_contra_bot_oct = std::max(1, scale_rh_oct - 1 - config_.octaves);
    const auto lh_contra = build_scale(config_.key, scale_type, lh_contra_bot_oct, config_.octaves);
    if (lh_contra.has_value() && lh_contra->size() == rh_scale_cache->size()) {
      const std::size_t n = rh_scale_cache->size();
      for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lh_i = (n - 1U) - i;
        emit_parallel_note_step((*rh_scale_cache)[i], (*lh_contra)[lh_i],
                                WarmUpStation::ContraryScale, false,
                                scale_finger_at(config_.key, true, config_.octaves, i),
                                scale_finger_at(config_.key, false, config_.octaves, lh_i));
      }
      for (std::size_t i = n - 1; i-- > 0;) {
        const std::size_t lh_i = (n - 1U) - i;
        emit_parallel_note_step((*rh_scale_cache)[i], (*lh_contra)[lh_i],
                                WarmUpStation::ContraryScale, true,
                                scale_finger_at(config_.key, true, config_.octaves, i),
                                scale_finger_at(config_.key, false, config_.octaves, lh_i));
      }
    }
  }

  // Station 8: LH Stride Jumps (3/4 bass root -> mid-register chord -> mid-register chord)
  if (include_stride) {
    constexpr std::array<int, 4> kStrideDegrees{1, 4, 5, 1};
    constexpr std::array<int, 2> kMidOctaves{3, 4};
    for (const int deg : kStrideDegrees) {
      const bool deg_harm = (config_.key.mode == Mode::Minor && deg == 5);
      const auto mid_triad =
          pick_triad_in_midi_range(config_.key, deg, deg_harm, kMidOctaves, 48, 62);
      if (!mid_triad.has_value()) {
        continue;
      }
      const int mid_min = static_cast<int>(to_midi(mid_triad->notes[0]).value_or(52));
      const auto bass_root = pick_bass_root_below(config_.key, deg, deg_harm, mid_min - 5);
      if (!bass_root.has_value()) {
        continue;
      }
      const auto bass_m = to_midi(*bass_root);
      if (!bass_m.has_value()) {
        continue;
      }
      const std::string roman(cadence_roman_numeral(config_.key.mode, deg, deg_harm));
      const std::string chord_sym = format_chord_symbol(*mid_triad);

      // Beat 1: Bass root (plus lower octave in 2-octave mode if >= A0)
      WarmUpStep bass_step{};
      bass_step.station = WarmUpStation::LhStrideJumps;
      bass_step.beat_duration = 1;
      bass_step.solo_hand_clef = Clef::Bass;
      bass_step.is_cadence = false;
      if (config_.octaves == 2 && *bass_m >= 36U) {
        const SpelledNote low_oct{.letter = bass_root->letter,
                                  .accidental = bass_root->accidental,
                                  .octave = bass_root->octave - 1};
        if (const auto lm = to_midi(low_oct); lm.has_value()) {
          bass_step.notes.push_back(
              WarmUpNote{.spelled = low_oct, .clef = Clef::Bass, .midi = *lm, .finger = 5U});
        }
      }
      bass_step.notes.push_back(WarmUpNote{
          .spelled = *bass_root,
          .clef = Clef::Bass,
          .midi = *bass_m,
          .finger = (config_.octaves == 2 && *bass_m >= 36U) ? std::uint8_t{1} : std::uint8_t{5},
      });
      bass_step.label = std::format("{} Bass", roman);
      finalize_expected_midi(bass_step);
      steps_.push_back(std::move(bass_step));

      // Beats 2 & 3: Mid-register triad
      for (int rep = 0; rep < 2; ++rep) {
        WarmUpStep chord_step{};
        chord_step.station = WarmUpStation::LhStrideJumps;
        chord_step.beat_duration = 1;
        chord_step.solo_hand_clef = Clef::Bass;
        chord_step.is_cadence = false;
        constexpr std::array<std::uint8_t, 3> kChordF{5U, 3U, 1U};
        for (std::size_t ni = 0; ni < mid_triad->notes.size(); ++ni) {
          const auto& sn = mid_triad->notes[ni];
          if (const auto m = to_midi(sn); m.has_value()) {
            chord_step.notes.push_back(
                WarmUpNote{.spelled = sn, .clef = Clef::Bass, .midi = *m, .finger = kChordF[ni]});
          }
        }
        chord_step.label = (rep == 0) ? chord_sym : std::string{};
        finalize_expected_midi(chord_step);
        steps_.push_back(std::move(chord_step));
      }
    }
  }

  feedback_.assign(steps_.size(), WarmUpStepFeedback::Pending);
}

} // namespace selfano::core

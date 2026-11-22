#include "core/warmup_engine.hpp"

#include "core/theory.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace selfano::core {

namespace {

[[nodiscard]] std::vector<MidiPitch> normalize_midi_set(std::span<const MidiPitch> notes) {
  std::vector<MidiPitch> out(notes.begin(), notes.end());
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

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
    if (root_midi.has_value() && static_cast<int>(*root_midi) >= min_root_midi &&
        static_cast<int>(*root_midi) <= max_root_midi) {
      return triad;
    }
  }
  return build_diatonic_triad(key, degree, candidate_octaves.front(), Inversion::Root,
                              harmonic_dom);
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

} // namespace

WarmUpEngine::WarmUpEngine(WarmUpConfig config) : config_{config} {
  config_.octaves = std::clamp(config_.octaves, 1, 2);
  rebuild_steps();
}

void WarmUpEngine::configure(WarmUpConfig config) {
  config.octaves = std::clamp(config.octaves, 1, 2);
  config_ = config;
  rebuild_steps();
}

void WarmUpEngine::reset() {
  current_step_index_ = 0;
  std::fill(feedback_.begin(), feedback_.end(), WarmUpStepFeedback::Pending);
  last_played_midi_.clear();
}

WarmUpEvalResult WarmUpEngine::submit_played_notes(std::span<const MidiPitch> played_midi) {
  auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty()) {
    return WarmUpEvalResult{};
  }
  if (is_complete()) {
    return WarmUpEvalResult{
        .status = WarmUpEvalStatus::AlreadyComplete,
        .evaluated_step_index = steps_.size(),
        .expected_midi = {},
        .played_midi = std::move(normalized),
        .exercise_completed = false,
    };
  }

  last_played_midi_ = normalized;
  ++total_attempts_;

  const std::size_t idx = current_step_index_;
  const auto& step = steps_[idx];

  if (normalized == step.expected_midi) {
    ++correct_attempts_;
    feedback_[idx] = WarmUpStepFeedback::Correct;
    ++current_step_index_;
    const bool completed = is_complete();
    if (completed) {
      ++completed_runs_;
    }
    return WarmUpEvalResult{
        .status = WarmUpEvalStatus::Correct,
        .evaluated_step_index = idx,
        .expected_midi = step.expected_midi,
        .played_midi = std::move(normalized),
        .exercise_completed = completed,
    };
  }

  feedback_[idx] = WarmUpStepFeedback::Incorrect;
  return WarmUpEvalResult{
      .status = WarmUpEvalStatus::Incorrect,
      .evaluated_step_index = idx,
      .expected_midi = step.expected_midi,
      .played_midi = std::move(normalized),
      .exercise_completed = false,
  };
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

void WarmUpEngine::rebuild_steps() {
  steps_.clear();
  feedback_.clear();
  current_step_index_ = 0;
  last_played_midi_.clear();

  const bool include_rh =
      (config_.hand == HandSelection::HandsTogether || config_.hand == HandSelection::RightOnly);
  const bool include_lh =
      (config_.hand == HandSelection::HandsTogether || config_.hand == HandSelection::LeftOnly);

  const bool include_scale = (config_.exercise == WarmUpExerciseKind::ScaleAndCadence ||
                              config_.exercise == WarmUpExerciseKind::ScaleOnly);
  const bool include_cadence = (config_.exercise == WarmUpExerciseKind::ScaleAndCadence ||
                                config_.exercise == WarmUpExerciseKind::CadenceOnly);

  if (include_scale) {
    const ScaleType scale_type =
        (config_.key.mode == Mode::Major)
            ? ScaleType::Major
            : ((config_.minor_mode == MinorScaleMode::Harmonic) ? ScaleType::HarmonicMinor
                                                                : ScaleType::NaturalMinor);

    const auto tonic = tonic_of(config_.key);
    int rh_oct = 4;
    int lh_oct = 3;
    if (config_.octaves == 1) {
      rh_oct = 4;
      lh_oct = (tonic.letter <= NoteLetter::E) ? 3 : 2;
    } else {
      rh_oct = (tonic.letter <= NoteLetter::F) ? 4 : 3;
      lh_oct = (tonic.letter <= NoteLetter::G) ? 2 : 1;
    }

    const auto rh_scale = build_scale(config_.key, scale_type, rh_oct, config_.octaves);
    const auto lh_scale = build_scale(config_.key, scale_type, lh_oct, config_.octaves);

    if (rh_scale.has_value() && lh_scale.has_value() && rh_scale->size() == lh_scale->size() &&
        !rh_scale->empty()) {
      const std::size_t n = rh_scale->size();
      auto emit_scale_step = [&](std::size_t i, bool descending) {
        WarmUpStep step{};
        step.is_cadence = false;
        step.is_descending = descending;
        if (include_lh) {
          const auto& sn = (*lh_scale)[i];
          const auto midi = to_midi(sn);
          if (midi.has_value()) {
            step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Bass, .midi = *midi});
          }
        }
        if (include_rh) {
          const auto& sn = (*rh_scale)[i];
          const auto midi = to_midi(sn);
          if (midi.has_value()) {
            step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Treble, .midi = *midi});
          }
        }
        step.label = include_rh ? format_note((*rh_scale)[i]) : format_note((*lh_scale)[i]);
        finalize_expected_midi(step);
        steps_.push_back(std::move(step));
      };

      for (std::size_t i = 0; i < n; ++i) {
        emit_scale_step(i, false);
      }
      for (std::size_t i = n - 1; i-- > 0;) {
        emit_scale_step(i, true);
      }
    }
  }

  if (include_cadence) {
    constexpr std::array<int, 4> kCadenceDegrees{1, 4, 5, 1};
    constexpr std::array<int, 2> kRhOctaves{4, 3};
    constexpr std::array<int, 2> kLhOctaves{3, 2};
    const bool harmonic_dom =
        (config_.key.mode == Mode::Minor && config_.minor_mode == MinorScaleMode::Harmonic);

    for (const int deg : kCadenceDegrees) {
      const auto rh_triad =
          pick_triad_in_midi_range(config_.key, deg, harmonic_dom, kRhOctaves, 57, 68);
      const auto lh_triad =
          pick_triad_in_midi_range(config_.key, deg, harmonic_dom, kLhOctaves, 40, 51);
      if (!rh_triad.has_value() || !lh_triad.has_value()) {
        continue;
      }

      WarmUpStep step{};
      step.is_cadence = true;
      step.is_descending = false;

      if (config_.hand == HandSelection::HandsTogether) {
        if (const auto bass_m = to_midi(lh_triad->notes[0]); bass_m.has_value()) {
          step.notes.push_back(
              WarmUpNote{.spelled = lh_triad->notes[0], .clef = Clef::Bass, .midi = *bass_m});
        }
        for (const auto& sn : rh_triad->notes) {
          if (const auto m = to_midi(sn); m.has_value()) {
            step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Treble, .midi = *m});
          }
        }
      } else if (config_.hand == HandSelection::RightOnly) {
        for (const auto& sn : rh_triad->notes) {
          if (const auto m = to_midi(sn); m.has_value()) {
            step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Treble, .midi = *m});
          }
        }
      } else {
        for (const auto& sn : lh_triad->notes) {
          if (const auto m = to_midi(sn); m.has_value()) {
            step.notes.push_back(WarmUpNote{.spelled = sn, .clef = Clef::Bass, .midi = *m});
          }
        }
      }

      const auto roman = cadence_roman_numeral(config_.key.mode, deg, harmonic_dom);
      const auto& chord_for_sym = (config_.hand == HandSelection::LeftOnly) ? *lh_triad : *rh_triad;
      step.label = std::format("{} ({})", roman, format_chord_symbol(chord_for_sym));
      finalize_expected_midi(step);
      steps_.push_back(std::move(step));
    }
  }

  feedback_.assign(steps_.size(), WarmUpStepFeedback::Pending);
}

} // namespace selfano::core

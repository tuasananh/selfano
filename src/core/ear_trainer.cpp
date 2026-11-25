#include "core/ear_trainer.hpp"

#include "core/theory.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <utility>

namespace selfano::core {

namespace {

struct PoolNote {
  SpelledNote spelled;
  MidiPitch midi;
};

[[nodiscard]] std::vector<PoolNote> build_diatonic_rh_pool(KeySignature key) {
  std::vector<PoolNote> pool;
  for (int step = 24; step <= 44; ++step) {
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
    if (m.has_value() && *m >= 60U && *m <= 79U) {
      pool.push_back(PoolNote{.spelled = sn, .midi = *m});
    }
  }
  return pool;
}

[[nodiscard]] Triad pick_tonic_triad(KeySignature key) noexcept {
  for (const int oct : {4, 3}) {
    const auto t = build_diatonic_triad(key, 1, oct, Inversion::Root, false);
    if (!t.has_value()) {
      continue;
    }
    const auto m = to_midi(t->notes[0]);
    if (m.has_value() && *m >= 57U && *m <= 68U) {
      return *t;
    }
  }
  return build_diatonic_triad(key, 1, 4, Inversion::Root, false).value_or(Triad{});
}

[[nodiscard]] std::size_t melody_length_for_tier(EarDifficultyTier tier) noexcept {
  switch (tier) {
  case EarDifficultyTier::Tier1Steps:
    return 3U;
  case EarDifficultyTier::Tier2Thirds:
    return 4U;
  case EarDifficultyTier::Tier3Fifths:
    return 5U;
  }
  return 3U;
}

[[nodiscard]] int max_jump_for_tier(EarDifficultyTier tier) noexcept {
  switch (tier) {
  case EarDifficultyTier::Tier1Steps:
    return 1;
  case EarDifficultyTier::Tier2Thirds:
    return 2;
  case EarDifficultyTier::Tier3Fifths:
    return 4;
  }
  return 1;
}

} // namespace

EarTrainer::EarTrainer(KeySignature key, std::uint32_t seed, EarTrainerProgress progress)
    : key_{key}, rng_{seed}, progress_{progress} {
  generate_prompt();
}

void EarTrainer::set_key(KeySignature key) {
  key_ = key;
  generate_prompt();
}

void EarTrainer::load_progress(EarTrainerProgress progress) {
  progress_ = progress;
  generate_prompt();
}

void EarTrainer::set_manual_tier_override(bool manual_override, EarDifficultyTier tier) {
  progress_.manual_tier_override = manual_override;
  progress_.tier = tier;
  progress_.consecutive_clean_solves = 0;
  generate_prompt();
}

void EarTrainer::next_prompt() {
  generate_prompt();
}

std::vector<EarPlaybackEvent> EarTrainer::build_playback_events() const {
  std::vector<EarPlaybackEvent> events;
  events.reserve(prompt_.tonic_chord_midi.size() + prompt_.melody_midi.size());

  double chord_offset = 0.0;
  for (const MidiPitch m : prompt_.tonic_chord_midi) {
    events.push_back(EarPlaybackEvent{
        .note = m,
        .velocity = 82,
        .start_offset_ms = chord_offset,
        .duration_ms = 850.0,
    });
    chord_offset += 20.0;
  }

  double mel_offset = 1050.0;
  for (const MidiPitch m : prompt_.melody_midi) {
    events.push_back(EarPlaybackEvent{
        .note = m,
        .velocity = 96,
        .start_offset_ms = mel_offset,
        .duration_ms = 430.0,
    });
    mel_offset += 520.0;
  }
  return events;
}

std::vector<EarPlaybackEvent> EarTrainer::request_replay() {
  ++replay_count_;
  return build_playback_events();
}

EarEvalResult EarTrainer::submit_note(MidiPitch played_midi) {
  if (state_ != EarTrainerState::Listening || matched_count_ >= prompt_.melody_midi.size()) {
    return EarEvalResult{};
  }

  const std::size_t idx = matched_count_;
  const MidiPitch expected = prompt_.melody_midi[idx];
  const int diff = static_cast<int>(expected) - static_cast<int>(played_midi);

  if (played_midi != expected) {
    had_mistake_on_current_ = true;
    progress_.consecutive_clean_solves = 0;
    const int abs_diff = std::abs(diff);
    last_hint_ =
        std::format("Target is {} {} {}", abs_diff, abs_diff == 1 ? "half-step" : "half-steps",
                    diff > 0 ? "higher" : "lower");
    return EarEvalResult{
        .status = EarEvalStatus::WrongNote,
        .note_index = idx,
        .expected_midi = expected,
        .played_midi = played_midi,
        .semitone_delta = diff,
        .hint_text = last_hint_,
        .promoted_tier = false,
    };
  }

  ++matched_count_;
  last_hint_.clear();

  if (matched_count_ < prompt_.melody_midi.size()) {
    return EarEvalResult{
        .status = EarEvalStatus::CorrectNote,
        .note_index = idx,
        .expected_midi = expected,
        .played_midi = played_midi,
        .semitone_delta = 0,
        .hint_text = {},
        .promoted_tier = false,
    };
  }

  state_ = EarTrainerState::Revealed;
  ++melodies_solved_;

  bool promoted = false;
  if (!had_mistake_on_current_) {
    ++progress_.consecutive_clean_solves;
    if (!progress_.manual_tier_override && progress_.consecutive_clean_solves >= 3 &&
        progress_.tier != EarDifficultyTier::Tier3Fifths) {
      progress_.tier = (progress_.tier == EarDifficultyTier::Tier1Steps)
                           ? EarDifficultyTier::Tier2Thirds
                           : EarDifficultyTier::Tier3Fifths;
      progress_.consecutive_clean_solves = 0;
      promoted = true;
    }
  }

  return EarEvalResult{
      .status = EarEvalStatus::SolvedAndRevealed,
      .note_index = idx,
      .expected_midi = expected,
      .played_midi = played_midi,
      .semitone_delta = 0,
      .hint_text = {},
      .promoted_tier = promoted,
  };
}

void EarTrainer::generate_prompt() {
  state_ = EarTrainerState::Listening;
  matched_count_ = 0;
  had_mistake_on_current_ = false;
  last_hint_.clear();

  prompt_ = EarPrompt{};
  prompt_.key = key_;
  prompt_.tier = progress_.tier;

  const Triad tonic = pick_tonic_triad(key_);
  prompt_.tonic_chord_symbol = format_chord_symbol(tonic);
  for (const auto& sn : tonic.notes) {
    prompt_.tonic_chord_notes.push_back(sn);
    prompt_.tonic_chord_midi.push_back(to_midi(sn).value_or(kMiddleCMidi));
  }

  const auto pool = build_diatonic_rh_pool(key_);
  if (pool.empty()) {
    return;
  }

  std::vector<std::size_t> start_indices;
  for (std::size_t i = 0; i < pool.size(); ++i) {
    const std::uint8_t pc = pitch_class(pool[i].spelled);
    for (const auto& tn : tonic.notes) {
      if (pitch_class(tn) == pc) {
        start_indices.push_back(i);
        break;
      }
    }
  }
  if (start_indices.empty()) {
    start_indices.push_back(0);
  }

  std::uniform_int_distribution<std::size_t> start_dist(0, start_indices.size() - 1);
  std::size_t cur_idx = start_indices[start_dist(rng_)];

  const std::size_t target_len = melody_length_for_tier(progress_.tier);
  const int max_jump = max_jump_for_tier(progress_.tier);

  prompt_.melody_notes.push_back(pool[cur_idx].spelled);
  prompt_.melody_midi.push_back(pool[cur_idx].midi);

  for (std::size_t step = 1; step < target_len; ++step) {
    const int lo = std::max(0, static_cast<int>(cur_idx) - max_jump);
    const int hi =
        std::min(static_cast<int>(pool.size()) - 1, static_cast<int>(cur_idx) + max_jump);

    std::vector<std::size_t> candidates;
    for (int idx = lo; idx <= hi; ++idx) {
      if (std::cmp_not_equal(idx, cur_idx)) {
        candidates.push_back(static_cast<std::size_t>(idx));
      }
    }
    if (candidates.empty()) {
      candidates.push_back(cur_idx);
    }

    std::uniform_int_distribution<std::size_t> cand_dist(0, candidates.size() - 1);
    cur_idx = candidates[cand_dist(rng_)];
    prompt_.melody_notes.push_back(pool[cur_idx].spelled);
    prompt_.melody_midi.push_back(pool[cur_idx].midi);
  }
}

} // namespace selfano::core

#include "core/ear_trainer.hpp"

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <random>
#include <set>
#include <span>
#include <utility>
#include <vector>

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

[[nodiscard]] std::set<std::uint8_t> to_pitch_class_set(std::span<const MidiPitch> notes) {
  std::set<std::uint8_t> pcs;
  for (const MidiPitch m : notes) {
    pcs.insert(static_cast<std::uint8_t>(m % 12U));
  }
  return pcs;
}

[[nodiscard]] EarChordEvent make_chord_from_intervals(KeySignature key, MidiPitch root_midi,
                                                      std::span<const int> semitones,
                                                      std::string roman_label,
                                                      std::string chord_symbol) {
  EarChordEvent ev{};
  ev.roman_label = std::move(roman_label);
  ev.chord_symbol = std::move(chord_symbol);
  for (const int st : semitones) {
    const int raw = static_cast<int>(root_midi) + st;
    const auto clamped = static_cast<MidiPitch>(std::clamp(raw, 48, 84));
    ev.midi.push_back(clamped);
    ev.spelled.push_back(spell_midi_in_key(clamped, key).value_or(SpelledNote{}));
  }
  return ev;
}

[[nodiscard]] MidiPitch tonic_midi_at_octave(KeySignature key, int octave) noexcept {
  const auto t = tonic_of(key);
  const SpelledNote sn{.letter = t.letter, .accidental = t.accidental, .octave = octave};
  return to_midi(sn).value_or(kMiddleCMidi);
}

[[nodiscard]] EarChordEvent build_quality_chord(KeySignature key, EarChordQuality q) {
  const MidiPitch tonic4 = tonic_midi_at_octave(key, 4);
  const MidiPitch dom4 = static_cast<MidiPitch>(tonic4 + 7U);
  const MidiPitch lead4 = static_cast<MidiPitch>(tonic4 + 11U);
  const MidiPitch super4 = static_cast<MidiPitch>(tonic4 + 2U);
  const MidiPitch neap4 = static_cast<MidiPitch>(tonic4 + 1U);

  switch (q) {
  case EarChordQuality::Major: {
    static constexpr std::array<int, 3> kInt{0, 4, 7};
    return make_chord_from_intervals(key, tonic4, kInt, "I", "Major");
  }
  case EarChordQuality::Minor: {
    static constexpr std::array<int, 3> kInt{0, 3, 7};
    return make_chord_from_intervals(key, tonic4, kInt, "i", "Minor");
  }
  case EarChordQuality::Dominant7: {
    static constexpr std::array<int, 4> kInt{0, 4, 7, 10};
    return make_chord_from_intervals(key, dom4, kInt, "V7", "Dom 7");
  }
  case EarChordQuality::Diminished7: {
    static constexpr std::array<int, 4> kInt{0, 3, 6, 9};
    return make_chord_from_intervals(key, lead4, kInt,
                                     "vii\xC2\xB0"
                                     "7",
                                     "Dim 7");
  }
  case EarChordQuality::HalfDiminished7: {
    static constexpr std::array<int, 4> kInt{0, 3, 6, 10};
    return make_chord_from_intervals(key, super4, kInt,
                                     "ii\xC3\xB8"
                                     "7",
                                     "m7b5");
  }
  case EarChordQuality::Neapolitan6: {
    static constexpr std::array<int, 3> kInt{4, 7, 12};
    return make_chord_from_intervals(key, neap4, kInt, "bII6", "Neapolitan bII6");
  }
  }
  static constexpr std::array<int, 3> kFallback{0, 4, 7};
  return make_chord_from_intervals(key, tonic4, kFallback, "I", "Major");
}

[[nodiscard]] EarChordEvent build_roman_chord(KeySignature key, std::string_view roman) {
  const MidiPitch tonic3 = tonic_midi_at_octave(key, 3);
  if (roman == "I") {
    static constexpr std::array<int, 3> kInt{0, 4, 7};
    return make_chord_from_intervals(key, tonic3, kInt, "I", "I");
  }
  if (roman == "i") {
    static constexpr std::array<int, 3> kInt{0, 3, 7};
    return make_chord_from_intervals(key, tonic3, kInt, "i", "i");
  }
  if (roman == "IV") {
    static constexpr std::array<int, 3> kInt{0, 4, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 5U), kInt, "IV", "IV");
  }
  if (roman == "iv") {
    static constexpr std::array<int, 3> kInt{0, 3, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 5U), kInt, "iv", "iv");
  }
  if (roman == "V") {
    static constexpr std::array<int, 3> kInt{0, 4, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 7U), kInt, "V", "V");
  }
  if (roman == "V7") {
    static constexpr std::array<int, 4> kInt{0, 4, 7, 10};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 7U), kInt, "V7", "V7");
  }
  if (roman == "V7/V") {
    static constexpr std::array<int, 4> kInt{0, 4, 7, 10};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 2U), kInt, "V7/V",
                                     "V7/V");
  }
  if (roman == "vi") {
    static constexpr std::array<int, 3> kInt{0, 3, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 9U), kInt, "vi", "vi");
  }
  if (roman == "VI") {
    static constexpr std::array<int, 3> kInt{0, 4, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 8U), kInt, "VI", "VI");
  }
  if (roman == "ii") {
    static constexpr std::array<int, 3> kInt{0, 3, 7};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 2U), kInt, "ii", "ii");
  }
  if (roman == "bII6") {
    static constexpr std::array<int, 3> kInt{4, 7, 12};
    return make_chord_from_intervals(key, static_cast<MidiPitch>(tonic3 + 1U), kInt, "bII6",
                                     "bII6");
  }
  static constexpr std::array<int, 3> kFallback{0, 4, 7};
  return make_chord_from_intervals(key, tonic3, kFallback, std::string(roman), std::string(roman));
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

void EarTrainer::set_drill_mode(EarDrillMode mode) {
  if (progress_.drill_mode == mode) {
    return;
  }
  progress_.drill_mode = mode;
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

  if (prompt_.mode == EarDrillMode::Melody) {
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

  double seq_offset = 1050.0;
  const double dur = (prompt_.mode == EarDrillMode::ChordQuality) ? 1100.0 : 800.0;
  const double step = 950.0;
  for (const auto& ch : prompt_.chord_sequence) {
    double voice_off = 0.0;
    for (const MidiPitch m : ch.midi) {
      events.push_back(EarPlaybackEvent{
          .note = m,
          .velocity = 92,
          .start_offset_ms = seq_offset + voice_off,
          .duration_ms = dur,
      });
      voice_off += 12.0;
    }
    seq_offset += step;
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

EarEvalResult EarTrainer::submit_quality_choice(EarChordQuality choice) {
  if (state_ != EarTrainerState::Listening || prompt_.mode != EarDrillMode::ChordQuality) {
    return EarEvalResult{};
  }
  if (choice != prompt_.target_quality) {
    had_mistake_on_current_ = true;
    last_hint_ =
        std::format("Not {} — listen again for the chord color", ear_chord_quality_label(choice));
    return EarEvalResult{
        .status = EarEvalStatus::WrongNote,
        .hint_text = last_hint_,
    };
  }
  state_ = EarTrainerState::Revealed;
  ++melodies_solved_;
  last_hint_.clear();
  return EarEvalResult{.status = EarEvalStatus::SolvedAndRevealed, .hint_text = {}};
}

EarEvalResult EarTrainer::submit_progression_choice(std::size_t choice_index) {
  if (state_ != EarTrainerState::Listening || prompt_.mode != EarDrillMode::Progression ||
      choice_index >= prompt_.progression_choices.size()) {
    return EarEvalResult{};
  }
  if (choice_index != prompt_.correct_choice_index) {
    had_mistake_on_current_ = true;
    last_hint_ = "Listen to the bass motion and cadence again";
    return EarEvalResult{
        .status = EarEvalStatus::WrongNote,
        .hint_text = last_hint_,
    };
  }
  matched_count_ = prompt_.chord_sequence.size();
  state_ = EarTrainerState::Revealed;
  ++melodies_solved_;
  last_hint_.clear();
  return EarEvalResult{.status = EarEvalStatus::SolvedAndRevealed, .hint_text = {}};
}

EarEvalResult EarTrainer::submit_chord(std::span<const MidiPitch> played_notes) {
  if (state_ != EarTrainerState::Listening || played_notes.empty() ||
      prompt_.mode == EarDrillMode::Melody) {
    return EarEvalResult{};
  }

  const auto played_pcs = to_pitch_class_set(played_notes);
  if (prompt_.mode == EarDrillMode::ChordQuality && !prompt_.chord_sequence.empty()) {
    const auto target_pcs = to_pitch_class_set(prompt_.chord_sequence[0].midi);
    if (played_pcs == target_pcs) {
      state_ = EarTrainerState::Revealed;
      ++melodies_solved_;
      last_hint_.clear();
      return EarEvalResult{.status = EarEvalStatus::SolvedAndRevealed, .hint_text = {}};
    }
    had_mistake_on_current_ = true;
    last_hint_ = "Check chord tones or select the quality pill";
    return EarEvalResult{.status = EarEvalStatus::WrongNote, .hint_text = last_hint_};
  }

  if (prompt_.mode == EarDrillMode::Progression && matched_count_ < prompt_.chord_sequence.size()) {
    const auto& target = prompt_.chord_sequence[matched_count_];
    const auto target_pcs = to_pitch_class_set(target.midi);
    const std::uint8_t bass_pc = static_cast<std::uint8_t>(target.midi.front() % 12U);
    const bool matched =
        (played_pcs == target_pcs) || (played_pcs.size() <= 2U && played_pcs.contains(bass_pc));
    if (!matched) {
      had_mistake_on_current_ = true;
      last_hint_ = std::format("Chord {} of {}: check bass/harmony", matched_count_ + 1U,
                               prompt_.chord_sequence.size());
      return EarEvalResult{
          .status = EarEvalStatus::WrongNote,
          .note_index = matched_count_,
          .hint_text = last_hint_,
      };
    }
    const std::size_t idx = matched_count_;
    ++matched_count_;
    last_hint_.clear();
    if (matched_count_ < prompt_.chord_sequence.size()) {
      return EarEvalResult{
          .status = EarEvalStatus::CorrectNote, .note_index = idx, .hint_text = {}};
    }
    state_ = EarTrainerState::Revealed;
    ++melodies_solved_;
    return EarEvalResult{
        .status = EarEvalStatus::SolvedAndRevealed, .note_index = idx, .hint_text = {}};
  }

  return EarEvalResult{};
}

void EarTrainer::generate_prompt() {
  state_ = EarTrainerState::Listening;
  matched_count_ = 0;
  had_mistake_on_current_ = false;
  last_hint_.clear();

  prompt_ = EarPrompt{};
  prompt_.key = key_;
  prompt_.tier = progress_.tier;
  prompt_.mode = progress_.drill_mode;

  const Triad tonic = pick_tonic_triad(key_);
  prompt_.tonic_chord_symbol = format_chord_symbol(tonic);
  for (const auto& sn : tonic.notes) {
    prompt_.tonic_chord_notes.push_back(sn);
    prompt_.tonic_chord_midi.push_back(to_midi(sn).value_or(kMiddleCMidi));
  }

  if (progress_.drill_mode == EarDrillMode::ChordQuality) {
    static constexpr std::array<EarChordQuality, 6> kQualities{
        EarChordQuality::Major,           EarChordQuality::Minor,
        EarChordQuality::Dominant7,       EarChordQuality::Diminished7,
        EarChordQuality::HalfDiminished7, EarChordQuality::Neapolitan6,
    };
    const std::size_t q_idx =
        (static_cast<std::size_t>(rng_()) + static_cast<std::size_t>(prompt_counter_)) %
        kQualities.size();
    ++prompt_counter_;
    prompt_.target_quality = kQualities[q_idx];
    prompt_.chord_sequence.push_back(build_quality_chord(key_, prompt_.target_quality));
    return;
  }

  if (progress_.drill_mode == EarDrillMode::Progression) {
    struct ProgTemplate {
      std::string_view label;
      std::array<std::string_view, 4> romans;
    };
    static constexpr std::array<ProgTemplate, 4> kMajorProgs{{
        {"I -> IV -> V7 -> I", {"I", "IV", "V7", "I"}},
        {"I -> vi -> ii -> V7", {"I", "vi", "ii", "V7"}},
        {"I -> V7/V -> V -> I", {"I", "V7/V", "V", "I"}},
        {"I -> IV -> V -> I", {"I", "IV", "V", "I"}},
    }};
    static constexpr std::array<ProgTemplate, 4> kMinorProgs{{
        {"i -> iv -> V7 -> i", {"i", "iv", "V7", "i"}},
        {"i -> bII6 -> V7 -> i", {"i", "bII6", "V7", "i"}},
        {"i -> VI -> iv -> V7", {"i", "VI", "iv", "V7"}},
        {"i -> iv -> V -> i", {"i", "iv", "V", "i"}},
    }};
    const auto& bank = (key_.mode == Mode::Major) ? kMajorProgs : kMinorProgs;
    std::uniform_int_distribution<std::size_t> dist(0, bank.size() - 1U);
    const std::size_t target_idx = dist(rng_);
    for (std::size_t i = 0; i < bank.size(); ++i) {
      prompt_.progression_choices[i] = std::string(bank[i].label);
    }
    prompt_.correct_choice_index = target_idx;
    for (const auto r : bank[target_idx].romans) {
      prompt_.chord_sequence.push_back(build_roman_chord(key_, r));
    }
    return;
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

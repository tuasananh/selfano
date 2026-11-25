#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace selfano::core {

enum class EarDifficultyTier : std::uint8_t {
  Tier1Steps = 1,
  Tier2Thirds = 2,
  Tier3Fifths = 3,
};

enum class EarTrainerState : std::uint8_t {
  Listening = 0,
  Revealed = 1,
};

enum class EarEvalStatus : std::uint8_t {
  Ignored = 0,
  CorrectNote = 1,
  WrongNote = 2,
  SolvedAndRevealed = 3,
};

struct EarPlaybackEvent {
  MidiPitch note{60};
  std::uint8_t velocity{92};
  double start_offset_ms{0.0};
  double duration_ms{450.0};
};

struct EarTrainerProgress {
  EarDifficultyTier tier{EarDifficultyTier::Tier1Steps};
  int consecutive_clean_solves{0};
  bool manual_tier_override{false};

  bool operator==(const EarTrainerProgress&) const = default;
};

struct EarPrompt {
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  EarDifficultyTier tier{EarDifficultyTier::Tier1Steps};
  std::string tonic_chord_symbol;
  std::vector<SpelledNote> tonic_chord_notes;
  std::vector<MidiPitch> tonic_chord_midi;
  std::vector<SpelledNote> melody_notes;
  std::vector<MidiPitch> melody_midi;
};

struct EarEvalResult {
  EarEvalStatus status{EarEvalStatus::Ignored};
  std::size_t note_index{0};
  MidiPitch expected_midi{60};
  MidiPitch played_midi{60};
  int semitone_delta{0};
  std::string hint_text;
  bool promoted_tier{false};
};

class EarTrainer {
public:
  explicit EarTrainer(KeySignature key = KeySignature{.fifths = 0, .mode = Mode::Major},
                      std::uint32_t seed = 2026U, EarTrainerProgress progress = {});

  void set_key(KeySignature key);
  void load_progress(EarTrainerProgress progress);
  void set_manual_tier_override(bool manual_override, EarDifficultyTier tier);
  void next_prompt();

  [[nodiscard]] std::vector<EarPlaybackEvent> build_playback_events() const;
  [[nodiscard]] std::vector<EarPlaybackEvent> request_replay();
  [[nodiscard]] EarEvalResult submit_note(MidiPitch played_midi);

  [[nodiscard]] KeySignature key() const noexcept { return key_; }
  [[nodiscard]] EarTrainerState state() const noexcept { return state_; }
  [[nodiscard]] const EarPrompt& current_prompt() const noexcept { return prompt_; }
  [[nodiscard]] std::size_t matched_count() const noexcept { return matched_count_; }
  [[nodiscard]] const EarTrainerProgress& progress() const noexcept { return progress_; }
  [[nodiscard]] bool had_mistake_on_current() const noexcept { return had_mistake_on_current_; }
  [[nodiscard]] std::uint32_t melodies_solved() const noexcept { return melodies_solved_; }
  [[nodiscard]] std::uint32_t replay_count() const noexcept { return replay_count_; }
  [[nodiscard]] const std::string& last_hint() const noexcept { return last_hint_; }

private:
  void generate_prompt();

  KeySignature key_{.fifths = 0, .mode = Mode::Major};
  std::mt19937 rng_;
  EarTrainerProgress progress_{};
  EarTrainerState state_{EarTrainerState::Listening};
  EarPrompt prompt_{};
  std::size_t matched_count_{0};
  bool had_mistake_on_current_{false};
  std::uint32_t melodies_solved_{0};
  std::uint32_t replay_count_{0};
  std::string last_hint_;
};

} // namespace selfano::core

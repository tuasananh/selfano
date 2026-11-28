#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

enum class EarDifficultyTier : std::uint8_t {
  Tier1Steps = 1,
  Tier2Thirds = 2,
  Tier3Fifths = 3,
};

enum class EarDrillMode : std::uint8_t {
  Melody = 0,
  ChordQuality = 1,
  Progression = 2,
};

enum class EarChordQuality : std::uint8_t {
  Major = 0,
  Minor = 1,
  Dominant7 = 2,
  Diminished7 = 3,
  HalfDiminished7 = 4,
  Neapolitan6 = 5,
};

[[nodiscard]] constexpr std::string_view ear_chord_quality_label(EarChordQuality q) noexcept {
  switch (q) {
  case EarChordQuality::Major:
    return "Major";
  case EarChordQuality::Minor:
    return "Minor";
  case EarChordQuality::Dominant7:
    return "Dom 7";
  case EarChordQuality::Diminished7:
    return "Dim 7";
  case EarChordQuality::HalfDiminished7:
    return "m7b5";
  case EarChordQuality::Neapolitan6:
    return "Neapolitan bII6";
  }
  return "Major";
}

struct EarChordEvent {
  std::vector<MidiPitch> midi;
  std::vector<SpelledNote> spelled;
  std::string roman_label;
  std::string chord_symbol;
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
  EarDrillMode drill_mode{EarDrillMode::Melody};

  bool operator==(const EarTrainerProgress&) const = default;
};

struct EarPrompt {
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  EarDifficultyTier tier{EarDifficultyTier::Tier1Steps};
  EarDrillMode mode{EarDrillMode::Melody};
  std::string tonic_chord_symbol;
  std::vector<SpelledNote> tonic_chord_notes;
  std::vector<MidiPitch> tonic_chord_midi;
  std::vector<SpelledNote> melody_notes;
  std::vector<MidiPitch> melody_midi;
  EarChordQuality target_quality{EarChordQuality::Major};
  std::vector<EarChordEvent> chord_sequence;
  std::array<std::string, 4> progression_choices{};
  std::size_t correct_choice_index{0};
};

struct EarEvalResult {
  EarEvalStatus status{EarEvalStatus::Ignored};
  std::size_t note_index{0};
  MidiPitch expected_midi{60};
  MidiPitch played_midi{60};
  int semitone_delta{0};
  std::string hint_text{};
  bool promoted_tier{false};
};

class EarTrainer {
public:
  explicit EarTrainer(KeySignature key = KeySignature{.fifths = 0, .mode = Mode::Major},
                      std::uint32_t seed = 2026U, EarTrainerProgress progress = {});

  void set_key(KeySignature key);
  void load_progress(EarTrainerProgress progress);
  void set_drill_mode(EarDrillMode mode);
  void set_manual_tier_override(bool manual_override, EarDifficultyTier tier);
  void next_prompt();

  [[nodiscard]] std::vector<EarPlaybackEvent> build_playback_events() const;
  [[nodiscard]] std::vector<EarPlaybackEvent> request_replay();
  [[nodiscard]] EarEvalResult submit_note(MidiPitch played_midi);
  [[nodiscard]] EarEvalResult submit_quality_choice(EarChordQuality choice);
  [[nodiscard]] EarEvalResult submit_progression_choice(std::size_t choice_index);
  [[nodiscard]] EarEvalResult submit_chord(std::span<const MidiPitch> played_notes);

  [[nodiscard]] KeySignature key() const noexcept { return key_; }
  [[nodiscard]] EarDrillMode drill_mode() const noexcept { return progress_.drill_mode; }
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
  std::uint32_t prompt_counter_{0};
  std::string last_hint_;
};

} // namespace selfano::core

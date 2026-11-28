#pragma once

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"
#include "core/sight_reading_corpus.hpp"
#include "core/weekly_key.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace selfano::core {

enum class EtudeDifficultyLevel : std::uint8_t {
  Level1Foundations = 1,
  Level2EighthsAndWaltz = 2,
  Level3CompoundAndDotted = 3,
  Level4AlbertiAndAccidentals = 4,
  Level5RomanticTextures = 5,
};

enum class EtudeSourceMode : std::uint8_t {
  Procedural = 0,
  HybridClassical = 1,
};

[[nodiscard]] constexpr EtudeDifficultyLevel
suggested_etude_level_for_tier(int curriculum_tier) noexcept {
  if (curriculum_tier <= 1) {
    return EtudeDifficultyLevel::Level1Foundations;
  }
  if (curriculum_tier == 2) {
    return EtudeDifficultyLevel::Level2EighthsAndWaltz;
  }
  if (curriculum_tier <= 4) {
    return EtudeDifficultyLevel::Level3CompoundAndDotted;
  }
  if (curriculum_tier <= 6) {
    return EtudeDifficultyLevel::Level4AlbertiAndAccidentals;
  }
  return EtudeDifficultyLevel::Level5RomanticTextures;
}

enum class EtudeHandMode : std::uint8_t {
  RightHand = 0,
  LeftHand = 1,
  EasyHandsTogether = 2,
};

enum class EtudeTimingMode : std::uint8_t {
  SteadyPulse = 0,
  WaitForNote = 1,
};

enum class EtudeMaxJump : std::uint8_t {
  Steps = 1,
  Thirds = 2,
  Fifths = 4,
  ChordSkips = 7,
};

enum class EtudeDurationBeats : std::uint8_t {
  Quarter = 1,
  Half = 2,
  Whole = 4,
};

enum class EtudePlaybackState : std::uint8_t {
  Stopped = 0,
  CountIn = 1,
  Playing = 2,
  Paused = 3,
};

struct EtudeConfig {
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  MinorScaleMode minor_mode{MinorScaleMode::Harmonic};
  EtudeHandMode hand_mode{EtudeHandMode::EasyHandsTogether};
  EtudeTimingMode timing_mode{EtudeTimingMode::SteadyPulse};
  EtudeMaxJump max_jump{EtudeMaxJump::Thirds};
  EtudeDifficultyLevel difficulty{EtudeDifficultyLevel::Level1Foundations};
  EtudeSourceMode source_mode{EtudeSourceMode::Procedural};
  double bpm{72.0};
  double hit_window_fraction{0.40};
  std::uint32_t seed{1337U};

  constexpr bool operator==(const EtudeConfig&) const = default;
};

struct EtudeVoiceEvent {
  int beat_start{0};
  int start_tick{0};
  EtudeDurationBeats duration{EtudeDurationBeats::Quarter};
  RhythmicValue rhythmic_value{.base = NoteBaseDuration::Quarter};
  bool is_rest{false};
  bool tie_start{false};
  bool tie_stop{false};
  std::vector<SpelledNote> notes;
  std::vector<MidiPitch> midi;
};

struct EtudeMeasure {
  std::uint32_t bar_number{1};
  std::vector<EtudeVoiceEvent> treble_events;
  std::vector<EtudeVoiceEvent> bass_events;
};

enum class EtudeTargetStatus : std::uint8_t {
  Pending = 0,
  Hit = 1,
  Missed = 2,
};

struct EtudeBeatTarget {
  std::size_t bar_index{0};
  int beat_in_bar{0};
  int tick_in_bar{0};
  double system_beat{0.0};
  std::vector<MidiPitch> expected_midi;
  EtudeTargetStatus status{EtudeTargetStatus::Pending};
  bool had_error{false};
  std::vector<MidiPitch> played_midi;
};

struct EtudeSystem {
  std::array<EtudeMeasure, 4> measures{};
  std::vector<EtudeBeatTarget> targets;
  TimeSignature time_signature{.numerator = 4, .denominator = 4};
  std::string source_label{};
};

struct EtudeTickOutput {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool system_promoted{false};
  int newly_missed{0};
};

enum class EtudeInputStatus : std::uint8_t {
  Ignored = 0,
  Hit = 1,
  Missed = 2,
};

struct EtudeInputResult {
  EtudeInputStatus status{EtudeInputStatus::Ignored};
  std::optional<std::size_t> target_index{};
  bool system_promoted{false};
};

class EtudeEngine {
public:
  explicit EtudeEngine(EtudeConfig config = {});

  void configure(EtudeConfig config);
  void set_corpus(const SightReadingCorpus* corpus);
  void regenerate(std::uint32_t new_seed);
  void set_bpm(double bpm) noexcept;
  void set_timing_mode(EtudeTimingMode mode) noexcept;

  EtudeTickOutput start();
  void pause() noexcept;
  void resume() noexcept;
  void stop() noexcept;

  EtudeTickOutput update(double dt_ms);
  [[nodiscard]] EtudeInputResult submit_played_notes(std::span<const MidiPitch> played_midi);

  [[nodiscard]] const EtudeConfig& config() const noexcept { return config_; }
  [[nodiscard]] EtudePlaybackState playback_state() const noexcept { return state_; }
  [[nodiscard]] double playhead_beat() const noexcept { return playhead_beat_; }
  [[nodiscard]] int count_in_beat_number() const noexcept;
  [[nodiscard]] std::size_t wait_target_index() const noexcept { return wait_target_idx_; }
  [[nodiscard]] std::size_t expected_cardinality() const noexcept;

  [[nodiscard]] const EtudeSystem& active_system() const noexcept { return active_system_; }
  [[nodiscard]] const EtudeSystem& preview_system() const noexcept { return preview_system_; }

  [[nodiscard]] std::uint32_t completed_bars() const noexcept { return completed_bars_; }
  [[nodiscard]] std::uint32_t completed_systems() const noexcept { return completed_systems_; }
  [[nodiscard]] std::uint32_t hit_targets() const noexcept { return hit_targets_; }
  [[nodiscard]] std::uint32_t missed_targets() const noexcept { return missed_targets_; }
  [[nodiscard]] double accuracy_ratio() const noexcept;

private:
  void rebuild_systems();
  EtudeSystem generate_system(std::uint32_t first_bar_number);
  void promote_system();

  EtudeConfig config_{};
  std::mt19937 rng_;
  EtudePlaybackState state_{EtudePlaybackState::Stopped};
  double count_in_beats_{0.0};
  double playhead_beat_{0.0};
  std::size_t wait_target_idx_{0};

  EtudeSystem active_system_{};
  EtudeSystem preview_system_{};
  std::uint32_t next_bar_number_{1};
  std::optional<std::size_t> last_melody_pool_idx_{};
  const SightReadingCorpus* corpus_{nullptr};
  std::size_t corpus_cursor_{0};

  std::uint32_t completed_bars_{0};
  std::uint32_t completed_systems_{0};
  std::uint32_t hit_targets_{0};
  std::uint32_t missed_targets_{0};
};

} // namespace selfano::core

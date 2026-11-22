#pragma once

#include "core/chord_symbol.hpp"
#include "core/pitch.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace selfano::core {

struct RepertoirePhrase {
  std::string id;
  std::string piece_name;
  std::string phrase_name;
  std::string progression_text;

  bool operator==(const RepertoirePhrase&) const = default;
};

struct CompanionConfig {
  double rolling_window_ms{1500.0};
  double min_dwell_ms{500.0};

  bool operator==(const CompanionConfig&) const = default;
};

struct CompanionEvent {
  bool bar_advanced{false};
  bool phrase_completed{false};
  std::size_t new_bar_index{0};
  std::uint32_t completed_reps{0};
};

class CompanionMatcher {
public:
  explicit CompanionMatcher(CompanionConfig config = {});

  void set_config(CompanionConfig config) noexcept;
  void set_progression(std::vector<ChordSymbol> chords, double now_ms);
  void reset_phrase(double now_ms);
  void reset_reps() noexcept;

  [[nodiscard]] CompanionEvent push_note_on(MidiPitch midi, double now_ms);
  [[nodiscard]] CompanionEvent push_notes(std::span<const MidiPitch> notes, double now_ms);
  [[nodiscard]] CompanionEvent update(double now_ms);

  [[nodiscard]] const CompanionConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::span<const ChordSymbol> progression() const noexcept { return progression_; }
  [[nodiscard]] std::size_t current_bar_index() const noexcept { return current_bar_index_; }
  [[nodiscard]] std::uint32_t completed_reps() const noexcept { return completed_reps_; }
  [[nodiscard]] std::uint16_t active_window_pc_mask() const noexcept;
  [[nodiscard]] std::uint16_t matched_pc_mask_for_current_bar() const noexcept;
  [[nodiscard]] bool is_current_bar_covered() const noexcept;
  [[nodiscard]] double dwell_progress(double now_ms) const noexcept;

private:
  struct PcObservation {
    std::uint8_t pc{0};
    double timestamp_ms{0.0};
  };

  void prune(double now_ms);
  CompanionEvent try_advance(double now_ms);

  CompanionConfig config_{};
  std::vector<ChordSymbol> progression_;
  std::size_t current_bar_index_{0};
  std::uint32_t completed_reps_{0};
  double bar_entered_ms_{0.0};
  std::vector<PcObservation> observations_;
};

} // namespace selfano::core

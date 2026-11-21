#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace selfano::core {

inline constexpr int kTrebleBottomLineStep = 30; // E4
inline constexpr int kTrebleMiddleLineStep = 34; // B4
inline constexpr int kTrebleTopLineStep = 38;    // F5
inline constexpr int kBassBottomLineStep = 18;   // G2
inline constexpr int kBassMiddleLineStep = 22;   // D3
inline constexpr int kBassTopLineStep = 26;      // A3

inline constexpr float kNoteheadBlackWidthSpaces = 1.18f;
inline constexpr float kSecondOffsetRatio = 0.95f;

enum class StemDirection : std::uint8_t {
  Up = 0,
  Down = 1,
};

[[nodiscard]] constexpr int staff_bottom_step(Clef clef) noexcept {
  return (clef == Clef::Treble) ? kTrebleBottomLineStep : kBassBottomLineStep;
}

[[nodiscard]] constexpr int staff_middle_step(Clef clef) noexcept {
  return (clef == Clef::Treble) ? kTrebleMiddleLineStep : kBassMiddleLineStep;
}

[[nodiscard]] constexpr int staff_top_step(Clef clef) noexcept {
  return (clef == Clef::Treble) ? kTrebleTopLineStep : kBassTopLineStep;
}

[[nodiscard]] constexpr float step_to_y(int step, Clef clef, float bottom_line_y,
                                        float staff_space) noexcept {
  return bottom_line_y - static_cast<float>(step - staff_bottom_step(clef)) * (staff_space * 0.5f);
}

[[nodiscard]] std::vector<int> compute_ledger_line_steps(int step, Clef clef);

class MeasureAccidentalState {
public:
  explicit MeasureAccidentalState(KeySignature key = KeySignature{0, Mode::Major});

  void reset(KeySignature key);
  void reset_measure();

  [[nodiscard]] std::optional<Accidental> resolve_and_record(const SpelledNote& note);

private:
  KeySignature key_{};
  std::vector<std::pair<int, Accidental>> active_overrides_;
};

struct ChordNoteLayoutInput {
  std::size_t source_index{0};
  int step{28};
  std::optional<Accidental> displayed_accidental{};
};

struct NoteheadLayout {
  std::size_t source_index{0};
  int step{28};
  float x_offset_spaces{0.0f};
  std::optional<Accidental> displayed_accidental{};
  int accidental_column{0};
};

struct ChordLayout {
  StemDirection stem_direction{StemDirection::Up};
  std::vector<NoteheadLayout> noteheads;
  std::vector<int> ledger_line_steps;
  float min_notehead_x_spaces{0.0f};
  float max_notehead_x_spaces{kNoteheadBlackWidthSpaces};
  int stem_start_step{28};
  int stem_end_step{35};
};

[[nodiscard]] ChordLayout layout_chord(std::span<const ChordNoteLayoutInput> notes, Clef clef,
                                       float notehead_width_spaces = kNoteheadBlackWidthSpaces);

} // namespace selfano::core

#pragma once

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
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
  explicit MeasureAccidentalState(KeySignature key = KeySignature{.fifths = 0,
                                                                  .mode = Mode::Major});

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

[[nodiscard]] ChordLayout
layout_chord(std::span<const ChordNoteLayoutInput> notes, Clef clef,
             float notehead_width_spaces = kNoteheadBlackWidthSpaces,
             std::optional<StemDirection> forced_stem_direction = std::nullopt);

[[nodiscard]] float
resolve_two_voice_collision(const ChordLayout& voice0_up, const ChordLayout& voice1_down,
                            float notehead_width_spaces = kNoteheadBlackWidthSpaces) noexcept;

struct BeamEventInput {
  std::size_t event_index{0};
  int start_tick{0};
  int duration_ticks{240};
  NoteBaseDuration base_duration{NoteBaseDuration::Eighth};
  bool dotted{false};
  bool is_rest{false};
  float x_spaces{0.0f};
  int min_step{28};
  int max_step{28};
  std::optional<TupletRatio> tuplet{};
};

struct SecondaryBeamSegment {
  float x0_spaces{0.0f};
  float x1_spaces{0.0f};
  float y0_spaces{0.0f};
  float y1_spaces{0.0f};
  bool is_hook{false};
  std::uint8_t level{2}; // 2 = 16th/32nd, 3 = 32nd
};

struct BeamGroupLayout {
  std::vector<std::size_t> event_indices;
  StemDirection stem_direction{StemDirection::Up};
  float start_x_spaces{0.0f};
  float end_x_spaces{0.0f};
  float start_y_spaces{0.0f};
  float end_y_spaces{0.0f};
  std::vector<float> stem_tip_y_spaces;
  std::vector<SecondaryBeamSegment> secondary_beams;
};

struct TupletBracketLayout {
  float start_x_spaces{0.0f};
  float end_x_spaces{0.0f};
  float y_spaces{0.0f};
  bool above{true};
  std::uint8_t actual{3};
  bool draw_bracket{false};
};

[[nodiscard]] std::vector<BeamGroupLayout>
compute_beam_groups(std::span<const BeamEventInput> events, TimeSignature meter, Clef clef,
                    float notehead_width_spaces = kNoteheadBlackWidthSpaces,
                    std::optional<StemDirection> forced_stem_direction = std::nullopt);

[[nodiscard]] std::vector<TupletBracketLayout>
compute_tuplet_brackets(std::span<const BeamEventInput> events,
                        std::span<const BeamGroupLayout> beam_groups, Clef clef,
                        float notehead_width_spaces = kNoteheadBlackWidthSpaces);

enum class TieSpanKind : std::uint8_t {
  Complete = 0,
  OutgoingHalf = 1,
  IncomingHalf = 2,
};

enum class TieDirection : std::uint8_t {
  Above = 0,
  Below = 1,
};

struct TieEndpointRef {
  std::size_t measure_index{0};
  std::size_t event_index{0};
  std::size_t note_index{0};
  MidiPitch midi{60};
  int step{28};
  float x_spaces{0.0f};
  StemDirection stem_direction{StemDirection::Up};
  std::size_t chord_note_count{1};
  std::size_t index_in_chord{0};
  bool tie_start{false};
  bool tie_stop{false};
};

struct TieSpan {
  TieSpanKind kind{TieSpanKind::Complete};
  TieDirection direction{TieDirection::Above};
  int start_step{28};
  int end_step{28};
  float start_x_spaces{0.0f};
  float end_x_spaces{0.0f};
  std::size_t start_measure_index{0};
  std::size_t end_measure_index{0};
};

[[nodiscard]] std::vector<TieSpan> resolve_tie_spans(std::span<const TieEndpointRef> system_notes);

[[nodiscard]] constexpr int apply_ottava_display_step(int sounding_step,
                                                      int octave_shift) noexcept {
  return sounding_step - octave_shift * 7;
}

[[nodiscard]] Clef active_clef_at(std::span<const ClefChange> clef_changes, Clef initial_clef,
                                  bool treble_staff, int tick) noexcept;

[[nodiscard]] int active_ottava_shift_at(std::span<const OttavaSpan> ottavas, std::size_t bar_idx,
                                         int tick, bool treble) noexcept;

} // namespace selfano::core

#pragma once

#include "core/key_signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace selfano::core {

enum class MinorScaleMode : std::uint8_t {
  Harmonic = 0,
  Natural = 1,
};

enum class HandSelection : std::uint8_t {
  HandsTogether = 0,
  RightOnly = 1,
  LeftOnly = 2,
};

inline constexpr std::array<KeySignature, 24> kWeeklyKeyCycle{{
    {.fifths = 0, .mode = Mode::Major},  // C Major
    {.fifths = 0, .mode = Mode::Minor},  // A Minor
    {.fifths = 1, .mode = Mode::Major},  // G Major
    {.fifths = 1, .mode = Mode::Minor},  // E Minor
    {.fifths = 2, .mode = Mode::Major},  // D Major
    {.fifths = 2, .mode = Mode::Minor},  // B Minor
    {.fifths = -1, .mode = Mode::Major}, // F Major
    {.fifths = -1, .mode = Mode::Minor}, // D Minor
    {.fifths = 3, .mode = Mode::Major},  // A Major
    {.fifths = 3, .mode = Mode::Minor},  // F# Minor
    {.fifths = -2, .mode = Mode::Major}, // Bb Major
    {.fifths = -2, .mode = Mode::Minor}, // G Minor
    {.fifths = -3, .mode = Mode::Major}, // Eb Major
    {.fifths = -3, .mode = Mode::Minor}, // C Minor
    {.fifths = 4, .mode = Mode::Major},  // E Major
    {.fifths = 4, .mode = Mode::Minor},  // C# Minor
    {.fifths = -4, .mode = Mode::Major}, // Ab Major
    {.fifths = -4, .mode = Mode::Minor}, // F Minor
    {.fifths = 5, .mode = Mode::Major},  // B Major
    {.fifths = 5, .mode = Mode::Minor},  // G# Minor
    {.fifths = -5, .mode = Mode::Major}, // Db Major
    {.fifths = -5, .mode = Mode::Minor}, // Bb Minor
    {.fifths = 6, .mode = Mode::Major},  // F# Major
    {.fifths = 6, .mode = Mode::Minor},  // D# Minor
}};

[[nodiscard]] constexpr KeySignature weekly_key_at(std::size_t index) noexcept {
  return kWeeklyKeyCycle[index % kWeeklyKeyCycle.size()];
}

[[nodiscard]] std::optional<std::size_t> find_weekly_key_index(KeySignature key) noexcept;

struct WeeklyKeyState {
  std::size_t key_index{0};
  std::int64_t week_start_epoch_day{0};
  bool manual_override{false};

  constexpr bool operator==(const WeeklyKeyState&) const = default;
};

[[nodiscard]] constexpr WeeklyKeyState sync_weekly_key(WeeklyKeyState state,
                                                       std::int64_t today_epoch_day) noexcept {
  state.key_index %= kWeeklyKeyCycle.size();
  if (state.week_start_epoch_day <= 0) {
    state.week_start_epoch_day = today_epoch_day;
    return state;
  }
  if (!state.manual_override && today_epoch_day > state.week_start_epoch_day) {
    const std::int64_t elapsed_weeks = (today_epoch_day - state.week_start_epoch_day) / 7;
    if (elapsed_weeks > 0) {
      const std::size_t advance = static_cast<std::size_t>(elapsed_weeks) % kWeeklyKeyCycle.size();
      state.key_index = (state.key_index + advance) % kWeeklyKeyCycle.size();
      state.week_start_epoch_day += elapsed_weeks * 7;
    }
  }
  return state;
}

} // namespace selfano::core

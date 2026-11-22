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
    {0, Mode::Major},  // C Major
    {0, Mode::Minor},  // A Minor
    {1, Mode::Major},  // G Major
    {1, Mode::Minor},  // E Minor
    {2, Mode::Major},  // D Major
    {2, Mode::Minor},  // B Minor
    {-1, Mode::Major}, // F Major
    {-1, Mode::Minor}, // D Minor
    {3, Mode::Major},  // A Major
    {3, Mode::Minor},  // F# Minor
    {-2, Mode::Major}, // Bb Major
    {-2, Mode::Minor}, // G Minor
    {-3, Mode::Major}, // Eb Major
    {-3, Mode::Minor}, // C Minor
    {4, Mode::Major},  // E Major
    {4, Mode::Minor},  // C# Minor
    {-4, Mode::Major}, // Ab Major
    {-4, Mode::Minor}, // F Minor
    {5, Mode::Major},  // B Major
    {5, Mode::Minor},  // G# Minor
    {-5, Mode::Major}, // Db Major
    {-5, Mode::Minor}, // Bb Minor
    {6, Mode::Major},  // F# Major
    {6, Mode::Minor},  // D# Minor
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

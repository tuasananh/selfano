#include "core/weekly_key.hpp"

#include "core/key_signature.hpp"

#include <cstddef>
#include <optional>

namespace selfano::core {

std::optional<std::size_t> find_weekly_key_index(KeySignature key) noexcept {
  for (std::size_t i = 0; i < kWeeklyKeyCycle.size(); ++i) {
    if (kWeeklyKeyCycle[i] == key) {
      return i;
    }
  }
  return std::nullopt;
}

} // namespace selfano::core

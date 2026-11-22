#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

inline constexpr double kDefaultEaseFactor = 2.5;
inline constexpr double kMinEaseFactor = 1.3;
inline constexpr double kMaxEaseFactor = 3.0;

enum class SrsRating : std::uint8_t {
  Again = 0,
  Hard = 1,
  Good = 2,
};

struct SrsCard {
  std::string id;
  std::string piece_name;
  std::string section_name;
  std::string chord_roadmap_hint;
  int interval_days{0};
  double ease_factor{kDefaultEaseFactor};
  int repetitions{0};
  std::int64_t due_epoch_day{0};

  bool operator==(const SrsCard&) const = default;
};

[[nodiscard]] SrsCard review_card(SrsCard card, SrsRating rating,
                                  std::int64_t today_epoch_day) noexcept;

[[nodiscard]] int preview_next_interval_days(const SrsCard& card, SrsRating rating,
                                             std::int64_t today_epoch_day) noexcept;

class SrsDeck {
public:
  std::string add_card(SrsCard card);
  bool update_card(const SrsCard& card);
  bool remove_card(std::string_view id);

  [[nodiscard]] const SrsCard* find_card(std::string_view id) const noexcept;
  [[nodiscard]] std::span<const SrsCard> cards() const noexcept { return cards_; }
  void set_cards(std::vector<SrsCard> cards);

  [[nodiscard]] std::vector<SrsCard> due_cards(std::int64_t today_epoch_day) const;
  [[nodiscard]] std::size_t due_count(std::int64_t today_epoch_day) const noexcept;

  std::optional<SrsCard> review_card_by_id(std::string_view id, SrsRating rating,
                                           std::int64_t today_epoch_day);

  [[nodiscard]] std::uint32_t session_reviews() const noexcept { return session_reviews_; }

private:
  std::vector<SrsCard> cards_;
  std::uint64_t next_auto_id_{1};
  std::uint32_t session_reviews_{0};
};

} // namespace selfano::core

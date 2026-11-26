#include "core/srs_deck.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

SrsCard review_card(SrsCard card, SrsRating rating, std::int64_t today_epoch_day) noexcept {
  card.ease_factor = std::clamp(card.ease_factor, kMinEaseFactor, kMaxEaseFactor);

  switch (rating) {
  case SrsRating::Again:
    card.repetitions = 0;
    card.interval_days = 1;
    card.ease_factor = std::max(kMinEaseFactor, card.ease_factor - 0.20);
    break;
  case SrsRating::Hard:
    card.ease_factor = std::max(kMinEaseFactor, card.ease_factor - 0.15);
    if (card.repetitions <= 0) {
      card.interval_days = 1;
    } else if (card.repetitions == 1) {
      card.interval_days = 3;
    } else {
      card.interval_days =
          std::max(card.interval_days + 1,
                   static_cast<int>(std::lround(static_cast<double>(card.interval_days) * 1.2)));
    }
    ++card.repetitions;
    break;
  case SrsRating::Good:
    if (card.repetitions <= 0) {
      card.interval_days = 1;
    } else if (card.repetitions == 1) {
      card.interval_days = 6;
    } else {
      card.interval_days =
          std::max(card.interval_days + 1,
                   static_cast<int>(
                       std::lround(static_cast<double>(card.interval_days) * card.ease_factor)));
    }
    ++card.repetitions;
    card.ease_factor = std::min(kMaxEaseFactor, std::max(kMinEaseFactor, card.ease_factor + 0.05));
    break;
  }

  card.due_epoch_day = today_epoch_day + static_cast<std::int64_t>(card.interval_days);
  return card;
}

int preview_next_interval_days(const SrsCard& card, SrsRating rating,
                               std::int64_t today_epoch_day) noexcept {
  return review_card(card, rating, today_epoch_day).interval_days;
}

std::string SrsDeck::add_card(SrsCard card) {
  if (card.id.empty()) {
    std::string candidate;
    do {
      candidate = "card-" + std::to_string(next_auto_id_++);
    } while (find_card(candidate) != nullptr);
    card.id = std::move(candidate);
  }
  const std::string assigned_id = card.id;
  cards_.push_back(std::move(card));
  return assigned_id;
}

bool SrsDeck::update_card(const SrsCard& card) {
  for (auto& existing : cards_) {
    if (existing.id == card.id) {
      existing = card;
      return true;
    }
  }
  return false;
}

bool SrsDeck::remove_card(std::string_view id) {
  const std::size_t before = cards_.size();
  std::erase_if(cards_, [id](const SrsCard& c) { return c.id == id; });
  return cards_.size() < before;
}

const SrsCard* SrsDeck::find_card(std::string_view id) const noexcept {
  for (const auto& c : cards_) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

void SrsDeck::set_cards(std::vector<SrsCard> cards) {
  cards_ = std::move(cards);
  next_auto_id_ = std::max(next_auto_id_, static_cast<std::uint64_t>(cards_.size()) + 1U);
}

std::vector<SrsCard> SrsDeck::due_cards(std::int64_t today_epoch_day) const {
  std::vector<SrsCard> out;
  for (const auto& c : cards_) {
    if (c.due_epoch_day <= today_epoch_day) {
      out.push_back(c);
    }
  }
  std::ranges::sort(out, [](const SrsCard& a, const SrsCard& b) {
    if (a.due_epoch_day != b.due_epoch_day) {
      return a.due_epoch_day < b.due_epoch_day;
    }
    return a.id < b.id;
  });
  return out;
}

std::size_t SrsDeck::due_count(std::int64_t today_epoch_day) const noexcept {
  std::size_t count = 0;
  for (const auto& c : cards_) {
    if (c.due_epoch_day <= today_epoch_day) {
      ++count;
    }
  }
  return count;
}

std::optional<SrsCard> SrsDeck::review_card_by_id(std::string_view id, SrsRating rating,
                                                  std::int64_t today_epoch_day) {
  for (auto& c : cards_) {
    if (c.id == id) {
      c = review_card(c, rating, today_epoch_day);
      ++session_reviews_;
      return c;
    }
  }
  return std::nullopt;
}

} // namespace selfano::core

#pragma once

#include "core/srs_deck.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace selfano::ui {

class SrsView {
public:
  SrsView();

  [[nodiscard]] const core::SrsDeck& deck() const noexcept { return deck_; }
  [[nodiscard]] core::SrsDeck& mutable_deck() noexcept { return deck_; }

  void draw(std::int64_t today_epoch_day);

private:
  void clear_form();

  core::SrsDeck deck_;
  bool peek_chord_hint_{false};
  std::string editing_card_id_{};
  std::array<char, 128> piece_buf_{};
  std::array<char, 128> section_buf_{};
  std::array<char, 256> hint_buf_{};
  std::string status_banner_{"Recall due repertoire sections from memory without sheet music"};
};

} // namespace selfano::ui

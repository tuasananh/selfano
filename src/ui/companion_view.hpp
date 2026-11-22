#pragma once

#include "core/companion_matcher.hpp"
#include "core/pitch.hpp"

#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

struct ImFont;

namespace selfano::ui {

class CompanionView {
public:
  CompanionView();

  void on_note_on(core::MidiPitch midi, double now_ms, bool& out_chime);
  void update(double now_ms, double dt_ms, bool& out_chime);
  void reset_phrase(double now_ms);

  void set_phrases(std::vector<core::RepertoirePhrase> phrases, double now_ms);
  void apply_preferences(double min_dwell_ms, bool chime_enabled) noexcept;
  [[nodiscard]] bool is_mid_phrase() const noexcept { return matcher_.current_bar_index() > 0; }
  [[nodiscard]] const std::vector<core::RepertoirePhrase>& phrases() const noexcept {
    return phrases_;
  }
  [[nodiscard]] const core::CompanionMatcher& matcher() const noexcept { return matcher_; }
  [[nodiscard]] bool chime_enabled() const noexcept { return chime_enabled_; }
  void set_chime_enabled(bool enabled) noexcept { chime_enabled_ = enabled; }

  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  void draw(ImFont* ui_font, double now_ms, bool& out_chime);

private:
  void load_editor_from_selected(double now_ms);
  void apply_editor_changes(double now_ms);

  std::vector<core::RepertoirePhrase> phrases_;
  std::size_t selected_index_{0};
  core::CompanionMatcher matcher_;

  std::array<char, 128> piece_buf_{};
  std::array<char, 128> phrase_buf_{};
  std::array<char, 256> prog_buf_{};
  std::string parse_error_msg_{};
  std::string status_banner_{"Keep eyes on your sheet music - play each bar's harmony naturally"};

  bool chime_enabled_{true};
  bool timer_running_{false};
  double chunk_remaining_sec_{600.0};
  std::uint64_t next_phrase_id_{100};
  double last_rep_flash_ms_{-10000.0};
};

} // namespace selfano::ui

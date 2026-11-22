#pragma once

#include "core/etude_engine.hpp"
#include "core/key_signature.hpp"
#include "core/weekly_key.hpp"

#include "midi/chord_grouper.hpp"

#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <vector>

struct ImFont;

namespace selfano::ui {

class EtudeView {
public:
  EtudeView();

  void set_key(core::KeySignature key, core::MinorScaleMode minor_mode);
  void apply_preferences(bool metronome_enabled, bool light_paper, double bpm,
                         core::EtudeHandMode hand_mode);
  void stop() noexcept { engine_.stop(); }
  [[nodiscard]] bool is_mid_bar() const noexcept;
  void on_played_chord(const midi::PlayedChord& chord);
  void toggle_play_pause(bool& out_click, bool& out_accent);
  void update(double dt_ms, bool& out_click, bool& out_accent);
  [[nodiscard]] double adjust_bpm(double delta);
  void new_passage();
  [[nodiscard]] core::EtudeTimingMode toggle_timing_mode();
  [[nodiscard]] bool toggle_metronome() noexcept {
    metronome_enabled_ = !metronome_enabled_;
    return metronome_enabled_;
  }

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  [[nodiscard]] const core::EtudeEngine& engine() const noexcept { return engine_; }
  [[nodiscard]] bool metronome_enabled() const noexcept { return metronome_enabled_; }
  void set_metronome_enabled(bool enabled) noexcept { metronome_enabled_ = enabled; }

  void draw(ImFont* smufl_font, ImFont* ui_font, bool& out_click, bool& out_accent);

private:
  core::EtudeEngine engine_;
  bool metronome_enabled_{true};
  bool light_paper_theme_{true};
};

} // namespace selfano::ui

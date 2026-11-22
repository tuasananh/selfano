#pragma once

#include "core/key_signature.hpp"
#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <vector>

struct ImFont;

namespace selfano::ui {

class WarmUpView {
public:
  WarmUpView();

  void set_weekly_key(core::KeySignature key, core::MinorScaleMode minor_mode);
  [[nodiscard]] core::KeySignature key() const noexcept { return engine_.config().key; }
  [[nodiscard]] core::MinorScaleMode minor_mode() const noexcept {
    return engine_.config().minor_mode;
  }

  void on_played_chord(const midi::PlayedChord& chord);
  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] std::vector<midi::ScheduledMidiNote>
  build_current_step_preview(double now_ms) const;
  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  [[nodiscard]] const core::WarmUpEngine& engine() const noexcept { return engine_; }
  void set_light_paper_theme(bool light) noexcept { light_paper_theme_ = light; }
  [[nodiscard]] bool light_paper_theme() const noexcept { return light_paper_theme_; }

  // Returns true if key or minor_mode changed via UI controls.
  bool draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
            std::vector<midi::ScheduledMidiNote>& out_playback_notes);

private:
  core::WarmUpEngine engine_;
  bool light_paper_theme_{true};
  std::string status_banner_{"Play the highlighted step on MIDI IN, Virtual Piano, or QWERTY"};
  NoteFeedback last_feedback_{NoteFeedback::Neutral};
};

} // namespace selfano::ui

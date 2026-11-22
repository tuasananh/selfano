#pragma once

#include "core/flashcard_engine.hpp"
#include "core/key_signature.hpp"
#include "core/weekly_key.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

struct ImFont;

namespace selfano::ui {

class FlashcardView {
public:
  FlashcardView();

  void set_key(core::KeySignature key, core::MinorScaleMode minor_mode, double now_ms);
  void on_played_chord(const midi::PlayedChord& chord, double now_ms);
  void update(double now_ms);

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] std::vector<midi::ScheduledMidiNote>
  build_current_prompt_preview(double now_ms) const;
  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  [[nodiscard]] const core::FlashcardEngine& engine() const noexcept { return engine_; }
  [[nodiscard]] core::FlashcardEngine& mutable_engine() noexcept { return engine_; }

  void draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
            std::vector<midi::ScheduledMidiNote>& out_playback_notes);

private:
  core::FlashcardEngine engine_;
  bool light_paper_theme_{true};
  NoteFeedback last_feedback_{NoteFeedback::Neutral};
  std::string status_banner_{"Play the displayed landmark, interval, or triad shape"};
};

} // namespace selfano::ui

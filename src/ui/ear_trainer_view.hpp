#pragma once

#include "core/ear_trainer.hpp"
#include "core/key_signature.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct ImFont;

namespace selfano::ui {

class EarTrainerView {
public:
  EarTrainerView();

  void set_key(core::KeySignature key);
  void load_progress(core::EarTrainerProgress progress);
  void on_played_chord(const midi::PlayedChord& chord);
  void trigger_replay(double now_ms, std::vector<midi::ScheduledMidiNote>& out_playback);
  void new_melody(double now_ms, std::vector<midi::ScheduledMidiNote>& out_playback);

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept { return 1; }
  [[nodiscard]] const core::EarTrainer& engine() const noexcept { return engine_; }
  [[nodiscard]] const core::EarTrainerProgress& progress() const noexcept {
    return engine_.progress();
  }
  void set_light_paper_theme(bool light) noexcept { light_paper_theme_ = light; }

  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  void draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
            std::vector<midi::ScheduledMidiNote>& out_playback);

private:
  void append_playback(std::span<const core::EarPlaybackEvent> events, double now_ms,
                       std::vector<midi::ScheduledMidiNote>& out_playback) const;

  core::EarTrainer engine_;
  bool light_paper_theme_{true};
  std::optional<core::MidiPitch> last_guess_midi_{};
  bool last_guess_correct_{false};
  std::string status_banner_{"Click Play / Replay (Space / CC67) to hear the tonic chord + melody"};
};

} // namespace selfano::ui

#pragma once

#include "core/key_signature.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_event.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/midi_device_bar.hpp"
#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct ImFont;

namespace selfano::ui {

// Interactive Phase 2 live MIDI & Virtual Piano evaluation sandbox.
class Phase2Sandbox {
public:
  Phase2Sandbox();

  // Feeds completed chords from hardware MIDI, Virtual Piano, or QWERTY into the active prompt.
  void on_played_chord(const midi::PlayedChord& chord, double now_ms);
  void on_soft_pedal(double now_ms);
  void reset_feedback();
  void release_virtual_piano(double now_ms, std::vector<midi::MidiNoteEvent>& out_events);

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] std::vector<midi::ScheduledMidiNote> build_prompt_preview(double now_ms) const;

  void draw(ImFont* smufl_font, ImFont* ui_font, const std::array<bool, 128>& held_notes,
            VirtualPianoMode piano_mode, double now_ms,
            std::vector<midi::MidiNoteEvent>& out_virtual_events,
            std::vector<midi::ScheduledMidiNote>& out_playback_notes);

private:
  struct Prompt {
    std::string title;
    core::KeySignature key{};
    std::vector<std::uint8_t> target_midi;
  };

  void select_prompt(std::size_t idx);

  std::vector<Prompt> prompts_;
  std::size_t prompt_index_{0};
  bool light_paper_theme_{true};
  bool auto_advance_{true};

  NoteFeedback last_feedback_{NoteFeedback::Neutral};
  std::vector<std::uint8_t> last_played_notes_;
  std::string feedback_summary_{
      "Play the highlighted notes using MIDI IN, Virtual Piano, or QWERTY"};
  double matched_at_ms_{-1.0};
  int correct_streak_{0};
  int soft_pedal_count_{0};

  VirtualPiano virtual_piano_;
};

} // namespace selfano::ui

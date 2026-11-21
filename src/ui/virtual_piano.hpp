#pragma once

#include "midi/midi_event.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace selfano::ui {

enum class VirtualPianoMode : std::uint8_t {
  Auto = 0,        // 88 keys when width >= 880 px, else 4 octaves (C2..C6)
  Full88 = 1,      // A0 (21) .. C8 (108)
  Compact4Oct = 2, // C2 (36) .. C6 (84)
};

struct VirtualPianoHighlights {
  std::array<bool, 128> held{};
  std::array<bool, 128> target{};
  std::array<bool, 128> correct{};
  std::array<bool, 128> incorrect{};
};

// Responsive interactive ImDrawList piano keyboard with mouse click/drag glissando support.
class VirtualPiano {
public:
  static constexpr float kAutoFull88MinWidth = 880.0f;

  // Draws the keyboard and appends any mouse-generated NoteOn/NoteOff events into out_events.
  void draw(const char* id, VirtualPianoMode mode, const VirtualPianoHighlights& highlights,
            double now_ms, std::vector<midi::MidiNoteEvent>& out_events, float height = 112.0f);

  void release_mouse_key(double now_ms, std::vector<midi::MidiNoteEvent>& out_events);

  [[nodiscard]] bool is_showing_88_keys() const noexcept { return last_was_88_keys_; }

private:
  std::optional<std::uint8_t> mouse_held_note_{};
  bool last_was_88_keys_{false};
};

} // namespace selfano::ui

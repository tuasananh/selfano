#include "core/pitch.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace selfano::core {

std::string format_note(const SpelledNote& note) {
  return std::format("{}{}{}", letter_char(note.letter), accidental_ascii(note.accidental),
                     note.octave);
}

std::string format_pitch_class(NoteLetter letter, Accidental accidental) {
  return std::format("{}{}", letter_char(letter), accidental_ascii(accidental));
}

std::vector<MidiPitch> normalize_midi_set(std::span<const MidiPitch> notes) {
  std::vector<MidiPitch> out(notes.begin(), notes.end());
  std::ranges::sort(out);
  out.erase(std::ranges::unique(out).begin(), out.end());
  return out;
}

} // namespace selfano::core

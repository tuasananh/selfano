#include "core/pitch.hpp"

#include <format>

namespace selfano::core {

std::string format_note(const SpelledNote& note) {
  return std::format("{}{}{}", letter_char(note.letter), accidental_ascii(note.accidental),
                     note.octave);
}

std::string format_pitch_class(NoteLetter letter, Accidental accidental) {
  return std::format("{}{}", letter_char(letter), accidental_ascii(accidental));
}

} // namespace selfano::core

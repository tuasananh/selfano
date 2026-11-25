#include "core/theory.hpp"

#include <format>

namespace selfano::core {

namespace {

[[nodiscard]] constexpr SpelledNote note_from_diatonic_step(int step, Accidental acc) noexcept {
  int octave = step / 7;
  int letter_idx = step % 7;
  if (letter_idx < 0) {
    letter_idx += 7;
    octave -= 1;
  }
  return SpelledNote{
      .letter = static_cast<NoteLetter>(letter_idx), .accidental = acc, .octave = octave};
}

[[nodiscard]] constexpr std::expected<Triad, std::string_view>
apply_inversion(NoteLetter root_letter, Accidental root_acc, TriadQuality quality,
                const std::array<SpelledNote, 3>& root_notes, Inversion inversion) noexcept {
  std::array<SpelledNote, 3> out = root_notes;
  switch (inversion) {
  case Inversion::Root:
    break;
  case Inversion::First:
    out = {
        root_notes[1],
        root_notes[2],
        SpelledNote{.letter = root_notes[0].letter,
                    .accidental = root_notes[0].accidental,
                    .octave = root_notes[0].octave + 1},
    };
    break;
  case Inversion::Second:
    out = {
        root_notes[2],
        SpelledNote{.letter = root_notes[0].letter,
                    .accidental = root_notes[0].accidental,
                    .octave = root_notes[0].octave + 1},
        SpelledNote{.letter = root_notes[1].letter,
                    .accidental = root_notes[1].accidental,
                    .octave = root_notes[1].octave + 1},
    };
    break;
  }

  for (const auto& n : out) {
    if (const auto midi = to_midi(n); !midi.has_value()) {
      return std::unexpected(midi.error());
    }
  }

  return Triad{
      .root_letter = root_letter,
      .root_accidental = root_acc,
      .quality = quality,
      .inversion = inversion,
      .notes = out,
  };
}

} // namespace

std::expected<std::vector<SpelledNote>, std::string_view>
build_scale(KeySignature key, int start_octave, int octaves) {
  const ScaleType default_type =
      (key.mode == Mode::Major) ? ScaleType::Major : ScaleType::NaturalMinor;
  return build_scale(key, default_type, start_octave, octaves);
}

std::expected<std::vector<SpelledNote>, std::string_view>
build_scale(KeySignature key, ScaleType scale_type, int start_octave, int octaves) {
  if (key.fifths < -7 || key.fifths > 7) {
    return std::unexpected("Key signature fifths must be in range [-7, 7]");
  }
  if (octaves < 1 || octaves > 4) {
    return std::unexpected("Octave count must be in range [1, 4]");
  }

  const Mode effective_mode = (scale_type == ScaleType::Major) ? Mode::Major : Mode::Minor;
  const KeySignature effective_key{.fifths = key.fifths, .mode = effective_mode};
  const auto tonic = tonic_of(effective_key);
  const int base_step = diatonic_step(tonic.letter, start_octave);
  const int total_notes = octaves * 7 + 1;

  std::vector<SpelledNote> scale;
  scale.reserve(static_cast<std::size_t>(total_notes));

  for (int i = 0; i < total_notes; ++i) {
    const int step = base_step + i;
    const int letter_idx = ((step % 7) + 7) % 7;
    const auto letter = static_cast<NoteLetter>(letter_idx);
    int acc_val = static_cast<int>(key_accidental_for(effective_key, letter));
    if (scale_type == ScaleType::HarmonicMinor && (i % 7) == 6) {
      acc_val += 1;
    }
    if (acc_val < -2 || acc_val > 2) {
      return std::unexpected("Scale degree requires triple accidental");
    }
    const auto note = note_from_diatonic_step(step, static_cast<Accidental>(acc_val));
    if (const auto midi = to_midi(note); !midi.has_value()) {
      return std::unexpected(midi.error());
    }
    scale.push_back(note);
  }

  return scale;
}

std::expected<Triad, std::string_view> build_triad(const SpelledNote& root, TriadQuality quality,
                                                   Inversion inversion) noexcept {
  Interval third_iv = Interval::MajorThird;
  int fifth_semitones = 7;

  switch (quality) {
  case TriadQuality::Major:
    third_iv = Interval::MajorThird;
    fifth_semitones = 7;
    break;
  case TriadQuality::Minor:
    third_iv = Interval::MinorThird;
    fifth_semitones = 7;
    break;
  case TriadQuality::Diminished:
    third_iv = Interval::MinorThird;
    fifth_semitones = 6;
    break;
  case TriadQuality::Augmented:
    third_iv = Interval::MajorThird;
    fifth_semitones = 8;
    break;
  }

  const auto n0_midi = to_midi(root);
  if (!n0_midi.has_value()) {
    return std::unexpected(n0_midi.error());
  }
  const auto n1 = transpose_diatonic(root, third_iv, 1);
  if (!n1.has_value()) {
    return std::unexpected(n1.error());
  }
  const auto n2 = transpose_diatonic_steps(root, 4, fifth_semitones);
  if (!n2.has_value()) {
    return std::unexpected(n2.error());
  }

  return apply_inversion(root.letter, root.accidental, quality, {root, *n1, *n2}, inversion);
}

std::expected<Triad, std::string_view> build_diatonic_triad(KeySignature key, int degree_1_to_7,
                                                            int root_octave, Inversion inversion,
                                                            bool harmonic_minor_dominant) noexcept {
  if (key.fifths < -7 || key.fifths > 7) {
    return std::unexpected("Key signature fifths must be in range [-7, 7]");
  }
  if (degree_1_to_7 < 1 || degree_1_to_7 > 7) {
    return std::unexpected("Scale degree must be in range [1, 7]");
  }

  const auto tonic = tonic_of(key);
  const int degree_idx = degree_1_to_7 - 1;
  const int root_letter_idx = (static_cast<int>(tonic.letter) + degree_idx) % 7;
  const int root_step = diatonic_step(static_cast<NoteLetter>(root_letter_idx), root_octave);
  const int leading_letter_idx = (static_cast<int>(tonic.letter) + 6) % 7;
  const bool raise_leading = (key.mode == Mode::Minor) && harmonic_minor_dominant &&
                             (degree_1_to_7 == 5 || degree_1_to_7 == 7);

  struct SpelledMidi {
    SpelledNote note;
    MidiPitch midi;
  };
  auto spell_step = [&](int step) -> std::expected<SpelledMidi, std::string_view> {
    const int l_idx = ((step % 7) + 7) % 7;
    const auto letter = static_cast<NoteLetter>(l_idx);
    int acc_val = static_cast<int>(key_accidental_for(key, letter));
    if (raise_leading && l_idx == leading_letter_idx) {
      acc_val += 1;
    }
    if (acc_val < -2 || acc_val > 2) {
      return std::unexpected("Triad note requires triple accidental");
    }
    const auto note = note_from_diatonic_step(step, static_cast<Accidental>(acc_val));
    const auto midi = to_midi(note);
    if (!midi.has_value()) {
      return std::unexpected(midi.error());
    }
    return SpelledMidi{.note = note, .midi = *midi};
  };

  const auto n0 = spell_step(root_step);
  const auto n1 = spell_step(root_step + 2);
  const auto n2 = spell_step(root_step + 4);
  if (!n0.has_value()) {
    return std::unexpected(n0.error());
  }
  if (!n1.has_value()) {
    return std::unexpected(n1.error());
  }
  if (!n2.has_value()) {
    return std::unexpected(n2.error());
  }

  const int m0 = static_cast<int>(n0->midi);
  const int m1 = static_cast<int>(n1->midi);
  const int m2 = static_cast<int>(n2->midi);

  TriadQuality quality = TriadQuality::Major;
  const int third_span = m1 - m0;
  const int fifth_span = m2 - m0;
  if (third_span == 4 && fifth_span == 7) {
    quality = TriadQuality::Major;
  } else if (third_span == 3 && fifth_span == 7) {
    quality = TriadQuality::Minor;
  } else if (third_span == 3 && fifth_span == 6) {
    quality = TriadQuality::Diminished;
  } else if (third_span == 4 && fifth_span == 8) {
    quality = TriadQuality::Augmented;
  } else {
    return std::unexpected("Unsupported diatonic triad interval structure");
  }

  return apply_inversion(n0->note.letter, n0->note.accidental, quality,
                         {n0->note, n1->note, n2->note}, inversion);
}

std::string format_chord_symbol(NoteLetter root, Accidental accidental, TriadQuality quality) {
  std::string_view suffix = "";
  switch (quality) {
  case TriadQuality::Major:
    suffix = "";
    break;
  case TriadQuality::Minor:
    suffix = "m";
    break;
  case TriadQuality::Diminished:
    suffix = "dim";
    break;
  case TriadQuality::Augmented:
    suffix = "aug";
    break;
  }
  return std::format("{}{}", format_pitch_class(root, accidental), suffix);
}

std::string format_chord_symbol(const Triad& triad) {
  return format_chord_symbol(triad.root_letter, triad.root_accidental, triad.quality);
}

} // namespace selfano::core

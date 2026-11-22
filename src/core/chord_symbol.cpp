#include "core/chord_symbol.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>

namespace selfano::core {

namespace {

[[nodiscard]] std::string_view trim_ascii(std::string_view sv) noexcept {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return sv;
}

[[nodiscard]] std::optional<NoteLetter> parse_note_letter(char ch) noexcept {
  switch (ch) {
  case 'C':
    return NoteLetter::C;
  case 'D':
    return NoteLetter::D;
  case 'E':
    return NoteLetter::E;
  case 'F':
    return NoteLetter::F;
  case 'G':
    return NoteLetter::G;
  case 'A':
    return NoteLetter::A;
  case 'B':
    return NoteLetter::B;
  default:
    return std::nullopt;
  }
}

struct ParsedPitch {
  NoteLetter letter;
  Accidental accidental;
  std::uint8_t pc;
  std::size_t consumed;
};

[[nodiscard]] std::expected<ParsedPitch, ParseError> parse_pitch_prefix(std::string_view sv,
                                                                        std::size_t base_offset) {
  if (sv.empty()) {
    return std::unexpected(ParseError{"Expected chord root note", base_offset});
  }
  const auto letter = parse_note_letter(sv[0]);
  if (!letter.has_value()) {
    return std::unexpected(ParseError{"Invalid root note letter (expected A-G)", base_offset});
  }
  Accidental acc = Accidental::Natural;
  std::size_t consumed = 1;
  if (sv.size() >= 2) {
    if (sv[1] == '#') {
      acc = Accidental::Sharp;
      consumed = 2;
    } else if (sv[1] == 'b') {
      acc = Accidental::Flat;
      consumed = 2;
    }
  }
  const std::uint8_t pc = pitch_class(SpelledNote{*letter, acc, 4});
  return ParsedPitch{.letter = *letter, .accidental = acc, .pc = pc, .consumed = consumed};
}

[[nodiscard]] constexpr std::uint16_t build_mask(std::uint8_t root_pc,
                                                 std::span<const int> intervals) noexcept {
  std::uint16_t mask = 0;
  for (const int iv : intervals) {
    const std::uint8_t pc = static_cast<std::uint8_t>((static_cast<int>(root_pc) + iv) % 12);
    mask = static_cast<std::uint16_t>(mask | static_cast<std::uint16_t>(1U << pc));
  }
  return mask;
}

struct QualitySpec {
  std::string_view suffix;
  ChordQuality quality;
  std::array<int, 5> intervals;
  std::size_t count;
};

inline constexpr std::array<QualitySpec, 28> kQualitySpecs{{
    {"", ChordQuality::Major, {0, 4, 7, 0, 0}, 3},
    {"maj", ChordQuality::Major, {0, 4, 7, 0, 0}, 3},
    {"m", ChordQuality::Minor, {0, 3, 7, 0, 0}, 3},
    {"min", ChordQuality::Minor, {0, 3, 7, 0, 0}, 3},
    {"-", ChordQuality::Minor, {0, 3, 7, 0, 0}, 3},
    {"dim", ChordQuality::Diminished, {0, 3, 6, 0, 0}, 3},
    {"o", ChordQuality::Diminished, {0, 3, 6, 0, 0}, 3},
    {"dim7", ChordQuality::Diminished7, {0, 3, 6, 9, 0}, 4},
    {"o7", ChordQuality::Diminished7, {0, 3, 6, 9, 0}, 4},
    {"m7b5", ChordQuality::HalfDiminished7, {0, 3, 6, 10, 0}, 4},
    {"aug", ChordQuality::Augmented, {0, 4, 8, 0, 0}, 3},
    {"+", ChordQuality::Augmented, {0, 4, 8, 0, 0}, 3},
    {"sus2", ChordQuality::Sus2, {0, 2, 7, 0, 0}, 3},
    {"sus4", ChordQuality::Sus4, {0, 5, 7, 0, 0}, 3},
    {"sus", ChordQuality::Sus4, {0, 5, 7, 0, 0}, 3},
    {"7", ChordQuality::Dominant7, {0, 4, 7, 10, 0}, 4},
    {"maj7", ChordQuality::Major7, {0, 4, 7, 11, 0}, 4},
    {"M7", ChordQuality::Major7, {0, 4, 7, 11, 0}, 4},
    {"m7", ChordQuality::Minor7, {0, 3, 7, 10, 0}, 4},
    {"min7", ChordQuality::Minor7, {0, 3, 7, 10, 0}, 4},
    {"-7", ChordQuality::Minor7, {0, 3, 7, 10, 0}, 4},
    {"mMaj7", ChordQuality::MinorMajor7, {0, 3, 7, 11, 0}, 4},
    {"mM7", ChordQuality::MinorMajor7, {0, 3, 7, 11, 0}, 4},
    {"6", ChordQuality::Major6, {0, 4, 7, 9, 0}, 4},
    {"m6", ChordQuality::Minor6, {0, 3, 7, 9, 0}, 4},
    {"add9", ChordQuality::Add9, {0, 4, 7, 2, 0}, 4},
    {"madd9", ChordQuality::MinorAdd9, {0, 3, 7, 2, 0}, 4},
    {"7sus4", ChordQuality::Dominant7Sus4, {0, 5, 7, 10, 0}, 4},
}};

inline constexpr std::array<QualitySpec, 5> kExtendedQualitySpecs{{
    {"9", ChordQuality::Dominant9, {0, 4, 7, 10, 2}, 5},
    {"maj9", ChordQuality::Major9, {0, 4, 7, 11, 2}, 5},
    {"M9", ChordQuality::Major9, {0, 4, 7, 11, 2}, 5},
    {"m9", ChordQuality::Minor9, {0, 3, 7, 10, 2}, 5},
    {"5", ChordQuality::Power5, {0, 7, 0, 0, 0}, 2},
}};

[[nodiscard]] const QualitySpec* find_quality(std::string_view suffix) noexcept {
  for (const auto& spec : kQualitySpecs) {
    if (spec.suffix == suffix) {
      return &spec;
    }
  }
  for (const auto& spec : kExtendedQualitySpecs) {
    if (spec.suffix == suffix) {
      return &spec;
    }
  }
  return nullptr;
}

} // namespace

std::expected<ChordSymbol, ParseError> parse_chord_symbol(std::string_view text) {
  const std::string_view trimmed = trim_ascii(text);
  if (trimmed.empty()) {
    return std::unexpected(ParseError{"Chord symbol cannot be empty", 0});
  }

  const std::size_t slash_pos = trimmed.find('/');
  const std::string_view main_part =
      (slash_pos == std::string_view::npos) ? trimmed : trimmed.substr(0, slash_pos);

  const auto root = parse_pitch_prefix(main_part, 0);
  if (!root.has_value()) {
    return std::unexpected(root.error());
  }

  const std::string_view quality_str = main_part.substr(root->consumed);
  const QualitySpec* spec = find_quality(quality_str);
  if (spec == nullptr) {
    return std::unexpected(ParseError{"Unsupported chord quality suffix", root->consumed});
  }

  std::uint16_t mask =
      build_mask(root->pc, std::span<const int>(spec->intervals.data(), spec->count));

  std::optional<std::uint8_t> bass_pc;
  if (slash_pos != std::string_view::npos) {
    const std::string_view bass_part = trimmed.substr(slash_pos + 1);
    if (bass_part.empty()) {
      return std::unexpected(ParseError{"Missing bass note after '/'", slash_pos + 1});
    }
    const auto bass = parse_pitch_prefix(bass_part, slash_pos + 1);
    if (!bass.has_value()) {
      return std::unexpected(bass.error());
    }
    if (bass->consumed != bass_part.size()) {
      return std::unexpected(ParseError{"Unexpected trailing characters in slash bass",
                                        slash_pos + 1 + bass->consumed});
    }
    bass_pc = bass->pc;
    mask = static_cast<std::uint16_t>(mask | static_cast<std::uint16_t>(1U << bass->pc));
  }

  return ChordSymbol{
      .canonical_text = std::string(trimmed),
      .root_letter = root->letter,
      .root_accidental = root->accidental,
      .root_pc = root->pc,
      .bass_pc = bass_pc,
      .quality = spec->quality,
      .pitch_class_mask = mask,
  };
}

std::expected<std::vector<ChordSymbol>, ParseError> parse_chord_progression(std::string_view text) {
  std::vector<ChordSymbol> result;
  std::size_t i = 0;

  auto is_sep_char = [&](std::size_t pos) -> std::size_t {
    if (pos >= text.size()) {
      return 0;
    }
    if (std::isspace(static_cast<unsigned char>(text[pos])) != 0 || text[pos] == ',' ||
        text[pos] == '|') {
      return 1;
    }
    if (text[pos] == '-' && (pos + 1) < text.size() && text[pos + 1] == '>') {
      return 2;
    }
    return 0;
  };

  while (i < text.size()) {
    while (i < text.size()) {
      const std::size_t sep_len = is_sep_char(i);
      if (sep_len == 0) {
        break;
      }
      i += sep_len;
    }
    if (i >= text.size()) {
      break;
    }
    const std::size_t start = i;
    while (i < text.size() && is_sep_char(i) == 0) {
      ++i;
    }
    const std::string_view token = text.substr(start, i - start);
    const auto parsed = parse_chord_symbol(token);
    if (!parsed.has_value()) {
      return std::unexpected(ParseError{parsed.error().message, start + parsed.error().position});
    }
    result.push_back(*parsed);
  }

  if (result.empty()) {
    return std::unexpected(ParseError{"Chord progression cannot be empty", 0});
  }
  return result;
}

std::string format_chord_progression(std::span<const ChordSymbol> chords) {
  std::string out;
  for (std::size_t i = 0; i < chords.size(); ++i) {
    if (i > 0) {
      out += " -> ";
    }
    out += chords[i].canonical_text;
  }
  return out;
}

} // namespace selfano::core

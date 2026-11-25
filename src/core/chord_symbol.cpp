#include "core/chord_symbol.hpp"

#include <array>
#include <cctype>

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
    return std::unexpected(
        ParseError{.message = "Expected chord root note", .position = base_offset});
  }
  const auto letter = parse_note_letter(sv[0]);
  if (!letter.has_value()) {
    return std::unexpected(
        ParseError{.message = "Invalid root note letter (expected A-G)", .position = base_offset});
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
  const std::uint8_t pc =
      pitch_class(SpelledNote{.letter = *letter, .accidental = acc, .octave = 4});
  return ParsedPitch{.letter = *letter, .accidental = acc, .pc = pc, .consumed = consumed};
}

[[nodiscard]] constexpr std::uint16_t build_mask(std::uint8_t root_pc,
                                                 std::span<const int> intervals) noexcept {
  std::uint16_t mask = 0;
  for (const int iv : intervals) {
    const auto pc = static_cast<std::uint8_t>((static_cast<int>(root_pc) + iv) % 12);
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
    {.suffix = "", .quality = ChordQuality::Major, .intervals = {0, 4, 7, 0, 0}, .count = 3},
    {.suffix = "maj", .quality = ChordQuality::Major, .intervals = {0, 4, 7, 0, 0}, .count = 3},
    {.suffix = "m", .quality = ChordQuality::Minor, .intervals = {0, 3, 7, 0, 0}, .count = 3},
    {.suffix = "min", .quality = ChordQuality::Minor, .intervals = {0, 3, 7, 0, 0}, .count = 3},
    {.suffix = "-", .quality = ChordQuality::Minor, .intervals = {0, 3, 7, 0, 0}, .count = 3},
    {.suffix = "dim",
     .quality = ChordQuality::Diminished,
     .intervals = {0, 3, 6, 0, 0},
     .count = 3},
    {.suffix = "o", .quality = ChordQuality::Diminished, .intervals = {0, 3, 6, 0, 0}, .count = 3},
    {.suffix = "dim7",
     .quality = ChordQuality::Diminished7,
     .intervals = {0, 3, 6, 9, 0},
     .count = 4},
    {.suffix = "o7",
     .quality = ChordQuality::Diminished7,
     .intervals = {0, 3, 6, 9, 0},
     .count = 4},
    {.suffix = "m7b5",
     .quality = ChordQuality::HalfDiminished7,
     .intervals = {0, 3, 6, 10, 0},
     .count = 4},
    {.suffix = "aug", .quality = ChordQuality::Augmented, .intervals = {0, 4, 8, 0, 0}, .count = 3},
    {.suffix = "+", .quality = ChordQuality::Augmented, .intervals = {0, 4, 8, 0, 0}, .count = 3},
    {.suffix = "sus2", .quality = ChordQuality::Sus2, .intervals = {0, 2, 7, 0, 0}, .count = 3},
    {.suffix = "sus4", .quality = ChordQuality::Sus4, .intervals = {0, 5, 7, 0, 0}, .count = 3},
    {.suffix = "sus", .quality = ChordQuality::Sus4, .intervals = {0, 5, 7, 0, 0}, .count = 3},
    {.suffix = "7", .quality = ChordQuality::Dominant7, .intervals = {0, 4, 7, 10, 0}, .count = 4},
    {.suffix = "maj7", .quality = ChordQuality::Major7, .intervals = {0, 4, 7, 11, 0}, .count = 4},
    {.suffix = "M7", .quality = ChordQuality::Major7, .intervals = {0, 4, 7, 11, 0}, .count = 4},
    {.suffix = "m7", .quality = ChordQuality::Minor7, .intervals = {0, 3, 7, 10, 0}, .count = 4},
    {.suffix = "min7", .quality = ChordQuality::Minor7, .intervals = {0, 3, 7, 10, 0}, .count = 4},
    {.suffix = "-7", .quality = ChordQuality::Minor7, .intervals = {0, 3, 7, 10, 0}, .count = 4},
    {.suffix = "mMaj7",
     .quality = ChordQuality::MinorMajor7,
     .intervals = {0, 3, 7, 11, 0},
     .count = 4},
    {.suffix = "mM7",
     .quality = ChordQuality::MinorMajor7,
     .intervals = {0, 3, 7, 11, 0},
     .count = 4},
    {.suffix = "6", .quality = ChordQuality::Major6, .intervals = {0, 4, 7, 9, 0}, .count = 4},
    {.suffix = "m6", .quality = ChordQuality::Minor6, .intervals = {0, 3, 7, 9, 0}, .count = 4},
    {.suffix = "add9", .quality = ChordQuality::Add9, .intervals = {0, 4, 7, 2, 0}, .count = 4},
    {.suffix = "madd9",
     .quality = ChordQuality::MinorAdd9,
     .intervals = {0, 3, 7, 2, 0},
     .count = 4},
    {.suffix = "7sus4",
     .quality = ChordQuality::Dominant7Sus4,
     .intervals = {0, 5, 7, 10, 0},
     .count = 4},
}};

inline constexpr std::array<QualitySpec, 5> kExtendedQualitySpecs{{
    {.suffix = "9", .quality = ChordQuality::Dominant9, .intervals = {0, 4, 7, 10, 2}, .count = 5},
    {.suffix = "maj9", .quality = ChordQuality::Major9, .intervals = {0, 4, 7, 11, 2}, .count = 5},
    {.suffix = "M9", .quality = ChordQuality::Major9, .intervals = {0, 4, 7, 11, 2}, .count = 5},
    {.suffix = "m9", .quality = ChordQuality::Minor9, .intervals = {0, 3, 7, 10, 2}, .count = 5},
    {.suffix = "5", .quality = ChordQuality::Power5, .intervals = {0, 7, 0, 0, 0}, .count = 2},
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
    return std::unexpected(ParseError{.message = "Chord symbol cannot be empty", .position = 0});
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
    return std::unexpected(
        ParseError{.message = "Unsupported chord quality suffix", .position = root->consumed});
  }

  std::uint16_t mask =
      build_mask(root->pc, std::span<const int>(spec->intervals.data(), spec->count));

  std::optional<std::uint8_t> bass_pc;
  if (slash_pos != std::string_view::npos) {
    const std::string_view bass_part = trimmed.substr(slash_pos + 1);
    if (bass_part.empty()) {
      return std::unexpected(
          ParseError{.message = "Missing bass note after '/'", .position = slash_pos + 1});
    }
    const auto bass = parse_pitch_prefix(bass_part, slash_pos + 1);
    if (!bass.has_value()) {
      return std::unexpected(bass.error());
    }
    if (bass->consumed != bass_part.size()) {
      return std::unexpected(ParseError{.message = "Unexpected trailing characters in slash bass",
                                        .position = slash_pos + 1 + bass->consumed});
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
      return std::unexpected(ParseError{.message = parsed.error().message,
                                        .position = start + parsed.error().position});
    }
    result.push_back(*parsed);
  }

  if (result.empty()) {
    return std::unexpected(
        ParseError{.message = "Chord progression cannot be empty", .position = 0});
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

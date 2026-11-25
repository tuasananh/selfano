#include "core/theory_glossary.hpp"

#include "core/chord_symbol.hpp"
#include "core/theory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

[[nodiscard]] std::string_view trim_view(std::string_view sv) noexcept {
  while (!sv.empty() &&
         (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\n' || sv.front() == '\r')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() &&
         (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\n' || sv.back() == '\r')) {
    sv.remove_suffix(1);
  }
  return sv;
}

[[nodiscard]] std::string_view function_to_label(HarmonicFunction fn) noexcept {
  switch (fn) {
  case HarmonicFunction::Tonic:
    return "Tonic";
  case HarmonicFunction::Predominant:
    return "Predominant";
  case HarmonicFunction::Dominant:
    return "Dominant";
  case HarmonicFunction::Chromatic:
    return "Chromatic";
  }
  return "Tonic";
}

[[nodiscard]] SpelledNote spell_pc_from_root(NoteLetter root_letter, int letter_offset,
                                             std::uint8_t target_pc) noexcept {
  const int l_idx = (static_cast<int>(root_letter) + letter_offset) % 7;
  const auto letter = static_cast<NoteLetter>(l_idx);
  const int nat_pc = natural_semitone(letter);
  int diff = static_cast<int>(target_pc) - nat_pc;
  while (diff > 6) {
    diff -= 12;
  }
  while (diff < -6) {
    diff += 12;
  }
  diff = std::clamp(diff, -2, 2);
  return SpelledNote{.letter = letter, .accidental = static_cast<Accidental>(diff), .octave = 4};
}

[[nodiscard]] std::string spell_chord_tones(const ChordSymbol& sym) {
  struct ToneSpec {
    int letter_offset;
    int semitones;
  };
  std::vector<ToneSpec> specs;
  switch (sym.quality) {
  case ChordQuality::Major:
    specs = {{0, 0}, {2, 4}, {4, 7}};
    break;
  case ChordQuality::Minor:
    specs = {{0, 0}, {2, 3}, {4, 7}};
    break;
  case ChordQuality::Diminished:
    specs = {{0, 0}, {2, 3}, {4, 6}};
    break;
  case ChordQuality::Diminished7:
    specs = {{0, 0}, {2, 3}, {4, 6}, {6, 9}};
    break;
  case ChordQuality::HalfDiminished7:
    specs = {{0, 0}, {2, 3}, {4, 6}, {6, 10}};
    break;
  case ChordQuality::Augmented:
    specs = {{0, 0}, {2, 4}, {4, 8}};
    break;
  case ChordQuality::Sus2:
    specs = {{0, 0}, {1, 2}, {4, 7}};
    break;
  case ChordQuality::Sus4:
    specs = {{0, 0}, {3, 5}, {4, 7}};
    break;
  case ChordQuality::Dominant7:
    specs = {{0, 0}, {2, 4}, {4, 7}, {6, 10}};
    break;
  case ChordQuality::Major7:
    specs = {{0, 0}, {2, 4}, {4, 7}, {6, 11}};
    break;
  case ChordQuality::Minor7:
    specs = {{0, 0}, {2, 3}, {4, 7}, {6, 10}};
    break;
  case ChordQuality::MinorMajor7:
    specs = {{0, 0}, {2, 3}, {4, 7}, {6, 11}};
    break;
  case ChordQuality::Major6:
    specs = {{0, 0}, {2, 4}, {4, 7}, {5, 9}};
    break;
  case ChordQuality::Minor6:
    specs = {{0, 0}, {2, 3}, {4, 7}, {5, 9}};
    break;
  case ChordQuality::Add9:
    specs = {{0, 0}, {2, 4}, {4, 7}, {1, 2}};
    break;
  case ChordQuality::MinorAdd9:
    specs = {{0, 0}, {2, 3}, {4, 7}, {1, 2}};
    break;
  case ChordQuality::Dominant7Sus4:
    specs = {{0, 0}, {3, 5}, {4, 7}, {6, 10}};
    break;
  case ChordQuality::Dominant9:
    specs = {{0, 0}, {2, 4}, {4, 7}, {6, 10}, {1, 2}};
    break;
  case ChordQuality::Major9:
    specs = {{0, 0}, {2, 4}, {4, 7}, {6, 11}, {1, 2}};
    break;
  case ChordQuality::Minor9:
    specs = {{0, 0}, {2, 3}, {4, 7}, {6, 10}, {1, 2}};
    break;
  case ChordQuality::Power5:
    specs = {{0, 0}, {4, 7}};
    break;
  }

  std::string out;
  for (std::size_t i = 0; i < specs.size(); ++i) {
    if (i > 0U) {
      out += " - ";
    }
    const auto pc =
        static_cast<std::uint8_t>((static_cast<int>(sym.root_pc) + specs[i].semitones) % 12);
    const auto sp = spell_pc_from_root(sym.root_letter, specs[i].letter_offset, pc);
    out += format_pitch_class(sp.letter, sp.accidental);
  }

  if (sym.bass_pc.has_value() && *sym.bass_pc != sym.root_pc) {
    const std::size_t slash = sym.canonical_text.find('/');
    if (slash != std::string::npos && slash + 1U < sym.canonical_text.size()) {
      out += std::format(" (bass: {})", sym.canonical_text.substr(slash + 1U));
    }
  }
  return out;
}

[[nodiscard]] bool starts_with_roman_letter(std::string_view sv) noexcept {
  if (sv.empty()) {
    return false;
  }
  return sv[0] == 'I' || sv[0] == 'V' || sv[0] == 'i' || sv[0] == 'v';
}

struct ParsedRoman {
  std::string_view primary;
  std::string_view core;
  std::string_view figure;
  std::string_view secondary_target;
};

[[nodiscard]] ParsedRoman parse_roman_token(std::string_view roman) noexcept {
  ParsedRoman res{};
  std::string_view p = roman;
  const std::size_t last_slash = roman.rfind('/');
  if (last_slash != std::string_view::npos && last_slash + 1U < roman.size()) {
    const std::string_view tail = roman.substr(last_slash + 1U);
    if (starts_with_roman_letter(tail)) {
      res.secondary_target = tail;
      p = roman.substr(0, last_slash);
    }
  }
  res.primary = p;

  constexpr std::array<std::string_view, 7> kFigures{
      "6/4", "6/5", "4/3", "4/2", "o7", "7", "6",
  };
  for (const auto fig : kFigures) {
    if (p.ends_with(fig) && p.size() > fig.size()) {
      res.figure = fig;
      res.core = p.substr(0, p.size() - fig.size());
      return res;
    }
  }
  res.core = p;
  return res;
}

[[nodiscard]] int roman_degree(std::string_view core) noexcept {
  if (core == "I" || core == "i") {
    return 1;
  }
  if (core == "II" || core == "ii" || core == "iio" || core == "bII") {
    return 2;
  }
  if (core == "III" || core == "iii") {
    return 3;
  }
  if (core == "IV" || core == "iv") {
    return 4;
  }
  if (core == "V" || core == "v") {
    return 5;
  }
  if (core == "VI" || core == "vi") {
    return 6;
  }
  if (core == "VII" || core == "vii" || core == "viio") {
    return 7;
  }
  return 0;
}

[[nodiscard]] std::string degree_ordinal_name(int degree, std::string_view core) {
  switch (degree) {
  case 1:
    return "Tonic (I)";
  case 2:
    if (core == "bII") {
      return "Neapolitan (bII)";
    }
    return "Supertonic (ii)";
  case 3:
    return "Mediant (III)";
  case 4:
    return "Subdominant (IV)";
  case 5:
    return "Dominant (V)";
  case 6:
    return "Submediant (VI)";
  case 7:
    return "Leading-Tone (vii)";
  default:
    return "Scale Harmony";
  }
}

[[nodiscard]] std::string figure_name_prefix(std::string_view fig) {
  if (fig == "6") {
    return "First-Inversion ";
  }
  if (fig == "6/4") {
    return "Second-Inversion ";
  }
  if (fig == "6/5") {
    return "First-Inversion ";
  }
  if (fig == "4/3") {
    return "Second-Inversion ";
  }
  if (fig == "4/2") {
    return "Third-Inversion ";
  }
  return "";
}

[[nodiscard]] std::string explain_figure_sentence(std::string_view fig) {
  if (fig == "6") {
    return "The '6' means 1st inversion: the 3rd of the chord sits in the bass, creating a "
           "smoother, lighter bass line.";
  }
  if (fig == "6/4") {
    return "The '6/4' means 2nd inversion: the 5th of the chord is in the bass, adding unstable "
           "upward lift.";
  }
  if (fig == "6/5") {
    return "The '6/5' marks a 7th chord in 1st inversion, placing the 3rd in the bass so it steps "
           "up cleanly to the next chord.";
  }
  if (fig == "4/3") {
    return "The '4/3' marks a 7th chord in 2nd inversion (5th in the bass), ideal for gentle "
           "passing motion.";
  }
  if (fig == "4/2") {
    return "The '4/2' puts the chord's tense 7th directly in the bass, which wants to resolve down "
           "by step to a 1st-inversion chord.";
  }
  if (fig == "o7") {
    return "The 'o7' denotes a symmetrical diminished seventh chord whose tight minor-3rd stacks "
           "pull intensely inward to resolve.";
  }
  if (fig == "7") {
    return "Adding the 7th sharpens harmonic tension and gives the bass and upper voices a strong "
           "gravitational pull.";
  }
  return "";
}

[[nodiscard]] std::string diatonic_symbol(KeySignature key, int degree, bool add_dominant_seven) {
  const auto triad = build_diatonic_triad(key, degree, 4, Inversion::Root, true);
  if (!triad.has_value()) {
    return degree == 5 ? "G" : "C";
  }
  std::string sym = format_chord_symbol(*triad);
  if (add_dominant_seven && triad->quality == TriadQuality::Major) {
    sym += "7";
  }
  return sym;
}

[[nodiscard]] std::string join_chords_caption(std::span<const std::string> seq) {
  std::string out;
  for (std::size_t i = 0; i < seq.size(); ++i) {
    if (i > 0U) {
      out += " -> ";
    }
    out += seq[i];
  }
  return out;
}

[[nodiscard]] std::vector<MidiPitch> voice_chord_rh(const ChordSymbol& sym,
                                                    std::optional<double> prev_centroid) {
  std::vector<std::uint8_t> pcs;
  for (int pc = 0; pc < 12; ++pc) {
    if (sym.contains_pitch_class(static_cast<std::uint8_t>(pc))) {
      pcs.push_back(static_cast<std::uint8_t>(pc));
    }
  }
  if (pcs.empty()) {
    return {60, 64, 67};
  }

  std::vector<MidiPitch> candidates;
  for (int m = 57; m <= 76; ++m) {
    if (sym.contains_midi(static_cast<MidiPitch>(m))) {
      candidates.push_back(static_cast<MidiPitch>(m));
    }
  }
  if (candidates.size() <= pcs.size()) {
    if (!candidates.empty()) {
      return candidates;
    }
    return {60, 64, 67};
  }

  const std::size_t target_size = std::min<std::size_t>(pcs.size(), 4U);
  const double goal_centroid = prev_centroid.value_or(65.0);
  std::vector<MidiPitch> best;
  double best_score = std::numeric_limits<double>::infinity();

  for (std::size_t start = 0; start + target_size <= candidates.size(); ++start) {
    std::vector<MidiPitch> slice;
    std::uint16_t covered = 0;
    double sum = 0.0;
    for (std::size_t k = 0; k < target_size; ++k) {
      const MidiPitch m = candidates[start + k];
      slice.push_back(m);
      covered = static_cast<std::uint16_t>(covered | static_cast<std::uint16_t>(1U << (m % 12U)));
      sum += static_cast<double>(m);
    }
    const int span = static_cast<int>(slice.back()) - static_cast<int>(slice.front());
    if (span > 12) {
      continue;
    }
    int covered_count = 0;
    for (const auto pc : pcs) {
      if ((covered & static_cast<std::uint16_t>(1U << pc)) != 0U) {
        ++covered_count;
      }
    }
    if (covered_count < static_cast<int>(std::min<std::size_t>(pcs.size(), 3U))) {
      continue;
    }
    const double centroid = sum / static_cast<double>(slice.size());
    const double missing_penalty =
        static_cast<double>(pcs.size() - static_cast<std::size_t>(covered_count)) * 12.0;
    const double score = std::abs(centroid - goal_centroid) + missing_penalty;
    if (score < best_score) {
      best_score = score;
      best = std::move(slice);
    }
  }

  if (!best.empty()) {
    return best;
  }
  return std::vector<MidiPitch>(
      candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(
                                                   std::min<std::size_t>(3U, candidates.size())));
}

} // namespace

std::vector<LessonPlaybackNote>
build_chord_sequence_preview(std::span<const std::string> chord_symbols, KeySignature key,
                             double chord_duration_ms) {
  (void)key;
  std::vector<LessonPlaybackNote> out;
  double cur_start = 0.0;
  std::optional<double> prev_centroid;
  const double step_ms = std::clamp(chord_duration_ms, 300.0, 2000.0);

  for (const auto& text : chord_symbols) {
    const auto parsed = parse_chord_symbol(text);
    if (!parsed.has_value()) {
      continue;
    }
    const auto& sym = *parsed;
    const std::uint8_t bass_pc = sym.bass_pc.value_or(sym.root_pc);
    const int bass_val = 43 + ((static_cast<int>(bass_pc) - 7 + 12) % 12); // G2..F#3
    const auto bass_midi = static_cast<MidiPitch>(std::clamp(bass_val, 40, 55));

    const auto rh = voice_chord_rh(sym, prev_centroid);
    double sum = 0.0;
    for (const auto m : rh) {
      sum += static_cast<double>(m);
    }
    if (!rh.empty()) {
      prev_centroid = sum / static_cast<double>(rh.size());
    }

    const double base_dur = std::max(180.0, step_ms * 0.88);
    out.push_back(LessonPlaybackNote{
        .midi = bass_midi,
        .velocity = 76,
        .start_ms = cur_start,
        .duration_ms = base_dur,
        .channel = 0,
    });
    for (std::size_t i = 0; i < rh.size(); ++i) {
      const double roll_ms = static_cast<double>(i + 1U) * 14.0;
      out.push_back(LessonPlaybackNote{
          .midi = rh[i],
          .velocity = static_cast<std::uint8_t>(std::min(92, 78 + static_cast<int>(i * 2U))),
          .start_ms = cur_start + roll_ms,
          .duration_ms = std::max(120.0, base_dur - roll_ms),
          .channel = 0,
      });
    }
    cur_start += step_ms;
  }
  return out;
}

HarmonyExplanation explain_harmony(std::string_view roman_in, std::string_view chord_in,
                                   KeySignature key) {
  const std::string_view roman = trim_view(roman_in);
  const std::string_view chord_sv = trim_view(chord_in);

  HarmonyExplanation out{};
  out.roman_numeral = std::string(roman);
  out.chord_symbol = std::string(chord_sv);

  const auto parsed_chord =
      chord_sv.empty()
          ? std::expected<ChordSymbol, ParseError>{std::unexpected(ParseError{"empty", 0})}
          : parse_chord_symbol(chord_sv);
  if (parsed_chord.has_value()) {
    out.spelled_notes = spell_chord_tones(*parsed_chord);
  }

  const std::string tonic_sym = diatonic_symbol(key, 1, false);
  const std::string dom7_sym = diatonic_symbol(key, 5, true);
  const std::string dom_sym = diatonic_symbol(key, 5, false);

  const ParsedRoman pr = parse_roman_token(roman);
  std::vector<std::string> preview_seq;
  if (!out.chord_symbol.empty()) {
    preview_seq.push_back(out.chord_symbol);
  }

  // 1. Neapolitan (bII6 / bII)
  if (pr.core == "bII") {
    out.function = HarmonicFunction::Chromatic;
    out.title = (pr.figure == "6") ? "Neapolitan Sixth (bII6)" : "Neapolitan Chord (bII)";
    out.explanation =
        "This dark major chord sits just a half-step above tonic with its 3rd in the bass, "
        "borrowing the lowered 2nd degree for tragic Romantic color. In classical voice-leading it "
        "drives strongly toward the dominant seventh (" +
        dom7_sym + ") before resolving home to " + tonic_sym + ".";
    preview_seq.push_back(dom7_sym);
    preview_seq.push_back(tonic_sym);
  } else if (!pr.secondary_target.empty()) {
    // 2. Secondary / applied chords (V7/V, viio7/V, V4/2/iv, ii6/5/V, etc.)
    out.function = HarmonicFunction::Chromatic;
    const ParsedRoman target_pr = parse_roman_token(pr.secondary_target);
    const int target_deg = std::max(1, roman_degree(target_pr.core));
    const std::string target_sym = diatonic_symbol(key, target_deg, false);
    const std::string fig_desc = explain_figure_sentence(pr.figure);

    if (pr.core == "viio" || pr.core == "vii") {
      out.title = std::format("Secondary Leading-Tone of {} ({})", pr.secondary_target, roman);
      out.explanation = std::format(
          "Instead of pointing to the home key, this borrowed chord acts as the leading-tone "
          "harmony "
          "for {} ({}).{} It resolves directly into {} to spotlight that chord temporarily.",
          pr.secondary_target, target_sym, fig_desc.empty() ? "" : (" " + fig_desc), target_sym);
    } else if (pr.core == "V" || pr.core == "v") {
      out.title = std::format("Secondary Dominant of {} ({})", pr.secondary_target, roman);
      out.explanation = std::format(
          "This chord borrows the dominant seventh that normally belongs to {}, using an outside "
          "accidental to pull the ear toward {} ({}).{} Listen for how cleanly it lands on {}.",
          pr.secondary_target, pr.secondary_target, target_sym,
          fig_desc.empty() ? "" : (" " + fig_desc), target_sym);
    } else {
      out.title = std::format("Applied Harmony to {} ({})", pr.secondary_target, roman);
      out.explanation = std::format(
          "This applied chord prepares {} ({}) as if it were a temporary tonic.{} Follow how it "
          "connects into {} and back toward home cadence.",
          pr.secondary_target, target_sym, fig_desc.empty() ? "" : (" " + fig_desc), target_sym);
    }

    if (out.chord_symbol != target_sym) {
      preview_seq.push_back(target_sym);
    }
    if (target_deg == 5 && target_sym != tonic_sym) {
      preview_seq.push_back(tonic_sym);
    }
  } else if ((pr.core == "I" || pr.core == "i") && pr.figure == "6/4") {
    // 3. Cadential 6/4
    out.function = HarmonicFunction::Dominant;
    out.title = (pr.core == "i") ? "Minor Cadential Six-Four (i6/4)" : "Cadential Six-Four (I6/4)";
    out.explanation = std::format(
        "Even though it uses the notes of the tonic chord ({}), placing the 5th in the bass turns "
        "it into an expressive dominant suspension. Both upper voices step down into {} to "
        "complete "
        "the cadence to {}.",
        out.chord_symbol.empty() ? tonic_sym : out.chord_symbol, dom7_sym, tonic_sym);
    preview_seq.push_back(dom7_sym);
    preview_seq.push_back(tonic_sym);
  } else if ((pr.core == "IV" || pr.core == "iv") && pr.figure == "6/4") {
    // 4. Pedal 6/4
    out.function = HarmonicFunction::Predominant;
    out.title = (pr.core == "iv") ? "Minor Pedal Six-Four (iv6/4)" : "Pedal Six-Four (IV6/4)";
    out.explanation = std::format(
        "Here the 5th of IV (the home tonic note!) stays anchored in the bass while the upper "
        "notes step gently upward and right back down. It prolongs {} like a warm sigh.",
        tonic_sym);
    preview_seq.push_back(tonic_sym);
  } else {
    // 5. Diatonic harmonies + figured-bass inversions
    const int deg = roman_degree(pr.core);
    if (deg == 5 || deg == 7) {
      out.function = HarmonicFunction::Dominant;
    } else if (deg == 2 || deg == 4) {
      out.function = HarmonicFunction::Predominant;
    } else {
      out.function = HarmonicFunction::Tonic;
    }

    const std::string fig_prefix = figure_name_prefix(pr.figure);
    const std::string deg_title = degree_ordinal_name(deg, pr.core);
    const bool is_seventh = (pr.figure == "7" || pr.figure == "6/5" || pr.figure == "4/3" ||
                             pr.figure == "4/2" || pr.figure == "o7");
    if (deg == 0) {
      out.title = roman.empty() ? out.chord_symbol : std::format("Harmony ({})", roman);
    } else {
      out.title =
          std::format("{}{}{} ({})", fig_prefix, deg_title, is_seventh ? " Seventh" : "", roman);
    }

    const std::string fig_sent = explain_figure_sentence(pr.figure);
    if (out.function == HarmonicFunction::Dominant) {
      out.explanation = std::format(
          "This dominant harmony builds bright forward tension using the key's leading tone.{} "
          "Resolve it cleanly home to {}.",
          fig_sent.empty() ? "" : (" " + fig_sent), tonic_sym);
      if (out.chord_symbol != tonic_sym) {
        preview_seq.push_back(tonic_sym);
      }
    } else if (out.function == HarmonicFunction::Predominant) {
      out.explanation = std::format(
          "This predominant harmony moves away from tonic to prepare the dominant.{} Notice how "
          "smoothly it leads into {} and then {}.",
          fig_sent.empty() ? "" : (" " + fig_sent), dom7_sym, tonic_sym);
      preview_seq.push_back(dom7_sym);
      preview_seq.push_back(tonic_sym);
    } else if (deg == 1) {
      out.explanation = std::format(
          "This is the tonic chord—home base in {}.{} Rest your phrasing here or step out toward "
          "the dominant ({}).",
          format_key_name(key), fig_sent.empty() ? "" : (" " + fig_sent), dom_sym);
      if (pr.figure == "6") {
        preview_seq.push_back(diatonic_symbol(key, 4, false));
        preview_seq.push_back(tonic_sym);
      }
    } else if (deg == 3) {
      out.explanation = std::format(
          "The mediant (III) brightens the key by sharing two notes with tonic while shifting to "
          "the relative major/minor color.{} It bridges smoothly into predominant harmonies.",
          fig_sent.empty() ? "" : (" " + fig_sent));
      preview_seq.push_back(diatonic_symbol(key, 6, false));
      preview_seq.push_back(tonic_sym);
    } else {
      out.explanation = std::format(
          "The submediant shares two tones with tonic for a deceptive or tender color.{} From here "
          "a progression typically moves through {} to {}.",
          fig_sent.empty() ? "" : (" " + fig_sent), dom7_sym, tonic_sym);
      preview_seq.push_back(dom7_sym);
      preview_seq.push_back(tonic_sym);
    }
  }

  out.function_label = std::string(function_to_label(out.function));
  if (preview_seq.empty() && parsed_chord.has_value()) {
    preview_seq.push_back(parsed_chord->canonical_text);
  }
  out.resolution_caption = join_chords_caption(preview_seq);
  out.preview_notes = build_chord_sequence_preview(preview_seq, key, 760.0);
  if (out.spelled_notes.empty() && !out.chord_symbol.empty()) {
    out.spelled_notes = out.chord_symbol;
  }
  return out;
}

} // namespace selfano::core

#include "core/lesson_score.hpp"

#include "core/chord_symbol.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

inline constexpr std::array<RhythmicValue, 17> kQuantizeRhythmicValuesDesc{{
    {.base = NoteBaseDuration::Whole, .dotted = true, .dots = 2},
    {.base = NoteBaseDuration::Whole, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Whole, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Half, .dotted = true, .dots = 2},
    {.base = NoteBaseDuration::Half, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Half, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 2},
    {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Quarter, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Eighth, .dotted = true, .dots = 2},
    {.base = NoteBaseDuration::Eighth, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Eighth, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Sixteenth, .dotted = true, .dots = 2},
    {.base = NoteBaseDuration::Sixteenth, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Sixteenth, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::ThirtySecond, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::ThirtySecond, .dotted = false, .dots = 0},
}};

inline constexpr std::array<RhythmicValue, 12> kRhythmicValuesDesc{{
    {.base = NoteBaseDuration::Whole, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Whole, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Half, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Half, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Quarter, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Quarter, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Eighth, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Eighth, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::Sixteenth, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::Sixteenth, .dotted = false, .dots = 0},
    {.base = NoteBaseDuration::ThirtySecond, .dotted = true, .dots = 1},
    {.base = NoteBaseDuration::ThirtySecond, .dotted = false, .dots = 0},
}};

void validate_staff_events(std::uint32_t bar_number, std::string_view staff_name,
                           std::span<const LessonEvent> events, int measure_ticks,
                           std::vector<MidiPitch>& open_ties, std::vector<std::string>& errors) {
  std::map<std::uint8_t, std::vector<std::pair<int, int>>> voice_spans;
  for (std::size_t idx = 0; idx < events.size(); ++idx) {
    const auto& ev = events[idx];
    if (ev.tuplet.has_value() && !ev.tuplet->is_valid()) {
      errors.push_back(std::format("Bar {} {} event {} has invalid tuplet ratio {}:{}", bar_number,
                                   staff_name, idx, ev.tuplet->actual, ev.tuplet->normal));
    }
    const int dur = ev.event_ticks();
    if (dur <= 0 || ev.start_tick < 0 || ev.start_tick + dur > measure_ticks) {
      errors.push_back(std::format("Bar {} {} event {} tick span [{}, {}) exceeds measure [0, {})",
                                   bar_number, staff_name, idx, ev.start_tick, ev.start_tick + dur,
                                   measure_ticks));
    }
    voice_spans[ev.voice].emplace_back(ev.start_tick, ev.start_tick + dur);

    if (ev.is_rest && !ev.notes.empty()) {
      errors.push_back(std::format("Bar {} {} rest event {} must have empty notes", bar_number,
                                   staff_name, idx));
    }
    if (!ev.is_rest && ev.notes.empty()) {
      errors.push_back(
          std::format("Bar {} {} non-rest event {} has no notes", bar_number, staff_name, idx));
    }

    for (const auto& n : ev.notes) {
      if (n.midi < kPianoLowestMidi || n.midi > kPianoHighestMidi) {
        errors.push_back(std::format("Bar {} {} MIDI {} outside piano range [21, 108]", bar_number,
                                     staff_name, n.midi));
      }
      const auto expected_midi = to_midi(n.spelled);
      if (!expected_midi.has_value() || *expected_midi != n.midi) {
        errors.push_back(std::format("Bar {} {} spelled note {} does not match MIDI {}", bar_number,
                                     staff_name, format_note(n.spelled), n.midi));
      }
      if (n.finger > 5U) {
        errors.push_back(std::format("Bar {} {} fingering {} must be in 0..5", bar_number,
                                     staff_name, n.finger));
      }
    }
  }

  for (auto& [voice, spans] : voice_spans) {
    std::ranges::sort(spans);
    int sum = 0;
    int prev_end = 0;
    for (const auto& [s, e] : spans) {
      if (s < prev_end) {
        errors.push_back(std::format("Bar {} {} voice {} has overlapping events", bar_number,
                                     staff_name, voice));
      }
      sum += (e - s);
      prev_end = std::max(prev_end, e);
    }
    if (voice == 0U && sum != measure_ticks) {
      errors.push_back(std::format("Bar {} {} voice 0 tick sum {} != measure_ticks {}", bar_number,
                                   staff_name, sum, measure_ticks));
    }
    if (voice > 0U && sum > measure_ticks) {
      errors.push_back(std::format("Bar {} {} voice {} tick sum {} exceeds measure_ticks {}",
                                   bar_number, staff_name, voice, sum, measure_ticks));
    }
  }

  if (!voice_spans.contains(0U)) {
    errors.push_back(std::format("Bar {} {} missing primary voice 0", bar_number, staff_name));
  }

  // Tie verification in chronological order per staff
  std::vector<std::size_t> order(events.size());
  for (std::size_t i = 0; i < events.size(); ++i) {
    order[i] = i;
  }
  std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
    if (events[a].start_tick != events[b].start_tick) {
      return events[a].start_tick < events[b].start_tick;
    }
    return events[a].voice < events[b].voice;
  });

  for (const std::size_t idx : order) {
    const auto& ev = events[idx];
    for (const auto& n : ev.notes) {
      if (n.tie_stop) {
        const auto it = std::ranges::find(open_ties, n.midi);
        if (it == open_ties.end()) {
          errors.push_back(std::format("Bar {} {} has orphan tie_stop on MIDI {}", bar_number,
                                       staff_name, n.midi));
        } else {
          open_ties.erase(it);
        }
      }
    }
    for (const auto& n : ev.notes) {
      if (n.tie_start) {
        open_ties.push_back(n.midi);
      }
    }
  }
}

} // namespace

std::expected<SpelledNote, std::string> parse_spelled_note(std::string_view text) {
  if (text.size() < 2U) {
    return std::unexpected(std::format("Note string too short: '{}'", text));
  }

  NoteLetter letter{};
  switch (static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])))) {
  case 'C':
    letter = NoteLetter::C;
    break;
  case 'D':
    letter = NoteLetter::D;
    break;
  case 'E':
    letter = NoteLetter::E;
    break;
  case 'F':
    letter = NoteLetter::F;
    break;
  case 'G':
    letter = NoteLetter::G;
    break;
  case 'A':
    letter = NoteLetter::A;
    break;
  case 'B':
    letter = NoteLetter::B;
    break;
  default:
    return std::unexpected(std::format("Invalid note letter in '{}'", text));
  }

  std::size_t pos = 1U;
  Accidental acc = Accidental::Natural;
  if (text.substr(pos).starts_with("##") || text.substr(pos).starts_with('x')) {
    acc = Accidental::DoubleSharp;
    pos += text.substr(pos).starts_with("##") ? 2U : 1U;
  } else if (text.substr(pos).starts_with("bb")) {
    acc = Accidental::DoubleFlat;
    pos += 2U;
  } else if (pos < text.size() && text[pos] == '#') {
    acc = Accidental::Sharp;
    pos += 1U;
  } else if (pos < text.size() && text[pos] == 'b') {
    acc = Accidental::Flat;
    pos += 1U;
  }

  if (pos >= text.size()) {
    return std::unexpected(std::format("Missing octave in '{}'", text));
  }

  int octave = 0;
  const char* begin = text.data() + pos;
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, octave);
  if (ec != std::errc{} || ptr != end || octave < -1 || octave > 9) {
    return std::unexpected(std::format("Invalid octave in '{}'", text));
  }

  return SpelledNote{.letter = letter, .accidental = acc, .octave = octave};
}

std::optional<RhythmicValue> quantize_ticks_to_rhythmic_value(int ticks) noexcept {
  for (const auto& rv : kQuantizeRhythmicValuesDesc) {
    if (rv.ticks() == ticks) {
      return rv;
    }
  }
  return std::nullopt;
}

std::vector<RhythmicValue> split_ticks_to_values(int total_ticks) {
  std::vector<RhythmicValue> out;
  int rem = total_ticks;
  while (rem >= 60) {
    bool matched = false;
    for (const auto& rv : kRhythmicValuesDesc) {
      if (rv.ticks() <= rem) {
        out.push_back(rv);
        rem -= rv.ticks();
        matched = true;
        break;
      }
    }
    if (!matched) {
      break;
    }
  }
  return out;
}

std::expected<void, std::vector<std::string>> validate_lesson_score(const LessonScore& score) {
  std::vector<std::string> errors;
  if (score.meta.id.empty()) {
    errors.emplace_back("LessonMetadata id must not be empty");
  }
  if (score.meta.title.empty()) {
    errors.emplace_back("LessonMetadata title must not be empty");
  }
  if (score.meta.key.fifths < -7 || score.meta.key.fifths > 7) {
    errors.emplace_back("LessonMetadata key fifths must be in [-7, 7]");
  }
  if (!score.meta.time_signature.is_valid()) {
    errors.emplace_back("LessonMetadata time_signature is invalid");
  }
  if (score.meta.target_bpm <= 0.0 || score.meta.target_bpm > 300.0) {
    errors.emplace_back("LessonMetadata target_bpm must be in (0, 300]");
  }
  if (score.measures.empty()) {
    errors.emplace_back("LessonScore must contain at least one measure");
    return std::unexpected(std::move(errors));
  }

  TimeSignature cur_meter = score.meta.time_signature;
  std::vector<MidiPitch> treble_open_ties;
  std::vector<MidiPitch> bass_open_ties;
  std::vector<int> bar_ticks;
  bar_ticks.reserve(score.measures.size());

  for (const auto& m : score.measures) {
    if (m.key_change.has_value() && (m.key_change->fifths < -7 || m.key_change->fifths > 7)) {
      errors.push_back(std::format("Bar {} invalid key_change fifths", m.bar_number));
    }
    if (m.meter_change.has_value()) {
      if (!m.meter_change->is_valid()) {
        errors.push_back(std::format("Bar {} invalid meter_change", m.bar_number));
      } else {
        cur_meter = *m.meter_change;
      }
    }
    const int full_mticks = cur_meter.measure_ticks();
    if (m.pickup_ticks.has_value() && (*m.pickup_ticks <= 0 || *m.pickup_ticks >= full_mticks)) {
      errors.push_back(std::format("Bar {} pickup_ticks {} must be in (0, {})", m.bar_number,
                                   *m.pickup_ticks, full_mticks));
    }
    const int mticks =
        (m.pickup_ticks.has_value() && *m.pickup_ticks > 0 && *m.pickup_ticks < full_mticks)
            ? *m.pickup_ticks
            : full_mticks;
    bar_ticks.push_back(mticks);
    validate_staff_events(m.bar_number, "treble", m.treble_events, mticks, treble_open_ties,
                          errors);
    validate_staff_events(m.bar_number, "bass", m.bass_events, mticks, bass_open_ties, errors);

    for (const auto& ha : m.harmony) {
      if (ha.start_tick < 0 || ha.start_tick >= mticks) {
        errors.push_back(
            std::format("Bar {} harmony start_tick {} out of range", m.bar_number, ha.start_tick));
      }
      if (!ha.chord_symbol.empty()) {
        const auto parsed = parse_chord_symbol(ha.chord_symbol);
        if (!parsed.has_value()) {
          errors.push_back(std::format("Bar {} invalid chord_symbol '{}': {}", m.bar_number,
                                       ha.chord_symbol, parsed.error().message));
        }
      }
    }

    for (const auto& dyn : m.dynamics) {
      if (dyn.start_tick < 0 || dyn.start_tick >= mticks) {
        errors.push_back(std::format("Bar {} dynamic start_tick {} out of [0, {})", m.bar_number,
                                     dyn.start_tick, mticks));
      }
    }
    for (const auto& hp : m.hairpins) {
      if (hp.start_tick < 0 || hp.end_tick <= hp.start_tick || hp.end_tick > mticks) {
        errors.push_back(std::format("Bar {} hairpin span [{}, {}) out of [0, {}]", m.bar_number,
                                     hp.start_tick, hp.end_tick, mticks));
      }
    }
  }

  for (const MidiPitch p : treble_open_ties) {
    errors.push_back(std::format("Dangling treble tie_start on MIDI {}", p));
  }
  for (const MidiPitch p : bass_open_ties) {
    errors.push_back(std::format("Dangling bass tie_start on MIDI {}", p));
  }

  for (std::size_t si = 0; si < score.slurs.size(); ++si) {
    const auto& sl = score.slurs[si];
    if (sl.start_bar >= score.measures.size() || sl.end_bar >= score.measures.size() ||
        sl.start_bar > sl.end_bar || (sl.start_bar == sl.end_bar && sl.start_tick >= sl.end_tick) ||
        sl.start_tick < 0 || sl.start_tick > bar_ticks[sl.start_bar] || sl.end_tick < 0 ||
        sl.end_tick > bar_ticks[sl.end_bar]) {
      errors.push_back(std::format("Slur {} has invalid span ({}:{} -> {}:{})", si, sl.start_bar,
                                   sl.start_tick, sl.end_bar, sl.end_tick));
    }
  }
  for (std::size_t pi = 0; pi < score.pedals.size(); ++pi) {
    const auto& pd = score.pedals[pi];
    if (pd.start_bar >= score.measures.size() || pd.end_bar >= score.measures.size() ||
        pd.start_bar > pd.end_bar || (pd.start_bar == pd.end_bar && pd.start_tick >= pd.end_tick) ||
        pd.start_tick < 0 || pd.start_tick > bar_ticks[pd.start_bar] || pd.end_tick < 0 ||
        pd.end_tick > bar_ticks[pd.end_bar]) {
      errors.push_back(std::format("Pedal {} has invalid span ({}:{} -> {}:{})", pi, pd.start_bar,
                                   pd.start_tick, pd.end_bar, pd.end_tick));
    }
  }

  for (std::size_t p = 0; p < score.phrases.size(); ++p) {
    const auto& ph = score.phrases[p];
    if (ph.label.empty()) {
      errors.push_back(std::format("Phrase {} label must not be empty", p));
    }
    if (ph.start_bar > ph.end_bar || ph.end_bar >= score.measures.size()) {
      errors.push_back(std::format("Phrase '{}' has invalid bar range [{}, {}]", ph.label,
                                   ph.start_bar, ph.end_bar));
    }
  }

  if (!errors.empty()) {
    return std::unexpected(std::move(errors));
  }
  return {};
}

std::vector<LessonPhrase> auto_partition_phrases(std::size_t bar_count) {
  std::vector<LessonPhrase> out;
  if (bar_count == 0U) {
    return out;
  }
  const std::size_t chunk = (bar_count <= 24U) ? 4U : 8U;
  std::size_t start = 0U;
  std::size_t idx = 0U;
  while (start < bar_count) {
    std::size_t end = std::min(bar_count - 1U, start + chunk - 1U);
    if (bar_count - (end + 1U) == 1U) {
      end = bar_count - 1U;
    }
    const char letter = static_cast<char>('A' + static_cast<int>(idx % 26U));
    out.push_back(LessonPhrase{
        .label = std::format("Phrase {} (Bars {}-{})", letter, start + 1U, end + 1U),
        .start_bar = start,
        .end_bar = end,
    });
    start = end + 1U;
    ++idx;
  }
  return out;
}

std::vector<LessonPhrase> effective_phrases(const LessonScore& score) {
  if (!score.phrases.empty()) {
    return score.phrases;
  }
  return auto_partition_phrases(score.measures.size());
}

} // namespace selfano::core

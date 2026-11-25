#include "core/lesson_score.hpp"

#include "core/chord_symbol.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <map>
#include <nlohmann/json.hpp>

namespace selfano::core {

namespace {

using json = nlohmann::json;

inline constexpr std::array<RhythmicValue, 10> kRhythmicValuesDesc{{
    {.base = NoteBaseDuration::Whole, .dotted = true},
    {.base = NoteBaseDuration::Whole, .dotted = false},
    {.base = NoteBaseDuration::Half, .dotted = true},
    {.base = NoteBaseDuration::Half, .dotted = false},
    {.base = NoteBaseDuration::Quarter, .dotted = true},
    {.base = NoteBaseDuration::Quarter, .dotted = false},
    {.base = NoteBaseDuration::Eighth, .dotted = true},
    {.base = NoteBaseDuration::Eighth, .dotted = false},
    {.base = NoteBaseDuration::Sixteenth, .dotted = true},
    {.base = NoteBaseDuration::Sixteenth, .dotted = false},
}};

[[nodiscard]] std::string_view duration_to_string(NoteBaseDuration base) noexcept {
  switch (base) {
  case NoteBaseDuration::Sixteenth:
    return "16th";
  case NoteBaseDuration::Eighth:
    return "8th";
  case NoteBaseDuration::Quarter:
    return "quarter";
  case NoteBaseDuration::Half:
    return "half";
  case NoteBaseDuration::Whole:
    return "whole";
  }
  return "quarter";
}

[[nodiscard]] std::optional<NoteBaseDuration> string_to_duration(std::string_view s) noexcept {
  if (s == "16th" || s == "sixteenth") {
    return NoteBaseDuration::Sixteenth;
  }
  if (s == "8th" || s == "eighth") {
    return NoteBaseDuration::Eighth;
  }
  if (s == "quarter" || s == "4th") {
    return NoteBaseDuration::Quarter;
  }
  if (s == "half" || s == "2nd") {
    return NoteBaseDuration::Half;
  }
  if (s == "whole" || s == "1st") {
    return NoteBaseDuration::Whole;
  }
  return std::nullopt;
}

[[nodiscard]] std::expected<KeySignature, std::string> parse_key_json(const json& obj) {
  if (!obj.is_object()) {
    return std::unexpected("Key signature must be an object");
  }
  if (!obj.contains("fifths") || !obj.at("fifths").is_number_integer()) {
    return std::unexpected("Key signature missing integer 'fifths'");
  }
  const int fifths = obj.at("fifths").get<int>();
  Mode mode = Mode::Major;
  if (obj.contains("mode")) {
    const auto& mv = obj.at("mode");
    if (mv.is_string()) {
      const auto s = mv.get<std::string>();
      if (s == "minor" || s == "Minor") {
        mode = Mode::Minor;
      } else if (s == "major" || s == "Major") {
        mode = Mode::Major;
      } else {
        return std::unexpected(std::format("Invalid key mode: '{}'", s));
      }
    } else if (mv.is_number_integer()) {
      mode = (mv.get<int>() == 1) ? Mode::Minor : Mode::Major;
    } else {
      return std::unexpected("Key signature 'mode' must be string or integer");
    }
  }
  const auto res = make_key_signature(fifths, mode);
  if (!res.has_value()) {
    return std::unexpected(std::string(res.error()));
  }
  return *res;
}

[[nodiscard]] std::expected<TimeSignature, std::string> parse_meter_json(const json& obj) {
  if (!obj.is_object()) {
    return std::unexpected("Time signature must be an object");
  }
  if (!obj.contains("numerator") || !obj.at("numerator").is_number_integer() ||
      !obj.contains("denominator") || !obj.at("denominator").is_number_integer()) {
    return std::unexpected("Time signature requires integer 'numerator' and 'denominator'");
  }
  const int num = obj.at("numerator").get<int>();
  const int den = obj.at("denominator").get<int>();
  if (num <= 0 || num > 32 || (den != 2 && den != 4 && den != 8 && den != 16)) {
    return std::unexpected(std::format("Invalid time signature {}/{}", num, den));
  }
  return TimeSignature{.numerator = static_cast<std::uint8_t>(num),
                       .denominator = static_cast<std::uint8_t>(den)};
}

[[nodiscard]] json serialize_event(const LessonEvent& ev) {
  json notes_arr = json::array();
  for (const auto& n : ev.notes) {
    json nj{
        {"spell", format_note(n.spelled)},
        {"midi", n.midi},
    };
    if (n.finger > 0) {
      nj["finger"] = n.finger;
    }
    if (n.tie_start) {
      nj["tie_start"] = true;
    }
    if (n.tie_stop) {
      nj["tie_stop"] = true;
    }
    notes_arr.push_back(std::move(nj));
  }
  json ej{
      {"start_tick", ev.start_tick},
      {"dur", duration_to_string(ev.duration.base)},
      {"dotted", ev.duration.dotted},
      {"is_rest", ev.is_rest},
      {"voice", ev.voice},
      {"notes", std::move(notes_arr)},
  };
  return ej;
}

[[nodiscard]] std::expected<LessonEvent, std::string> parse_event_json(const json& obj) {
  if (!obj.is_object()) {
    return std::unexpected("Lesson event must be an object");
  }
  LessonEvent ev{};
  if (!obj.contains("start_tick") || !obj.at("start_tick").is_number_integer()) {
    return std::unexpected("Lesson event missing integer 'start_tick'");
  }
  ev.start_tick = obj.at("start_tick").get<int>();

  if (!obj.contains("dur") || !obj.at("dur").is_string()) {
    return std::unexpected("Lesson event missing string 'dur'");
  }
  std::string dur_str = obj.at("dur").get<std::string>();
  bool shorthand_dot = false;
  if (!dur_str.empty() && dur_str.back() == '.') {
    shorthand_dot = true;
    dur_str.pop_back();
  }
  const auto base_opt = string_to_duration(dur_str);
  if (!base_opt.has_value()) {
    return std::unexpected(std::format("Unknown rhythmic duration '{}'", dur_str));
  }
  ev.duration.base = *base_opt;
  ev.duration.dotted = shorthand_dot;

  if (obj.contains("dotted")) {
    if (!obj.at("dotted").is_boolean()) {
      return std::unexpected("Event 'dotted' must be a boolean");
    }
    ev.duration.dotted = obj.at("dotted").get<bool>();
  }
  if (obj.contains("is_rest")) {
    if (!obj.at("is_rest").is_boolean()) {
      return std::unexpected("Event 'is_rest' must be a boolean");
    }
    ev.is_rest = obj.at("is_rest").get<bool>();
  }
  if (obj.contains("voice")) {
    if (!obj.at("voice").is_number_integer()) {
      return std::unexpected("Event 'voice' must be an integer");
    }
    const int v = obj.at("voice").get<int>();
    if (v < 0 || v > 15) {
      return std::unexpected("Event 'voice' out of range");
    }
    ev.voice = static_cast<std::uint8_t>(v);
  }
  if (obj.contains("notes")) {
    const auto& arr = obj.at("notes");
    if (!arr.is_array()) {
      return std::unexpected("Event 'notes' must be an array");
    }
    for (const auto& nobj : arr) {
      if (!nobj.is_object() || !nobj.contains("spell") || !nobj.at("spell").is_string()) {
        return std::unexpected("Note must be an object with string 'spell'");
      }
      const auto spelled = parse_spelled_note(nobj.at("spell").get<std::string>());
      if (!spelled.has_value()) {
        return std::unexpected(spelled.error());
      }
      const auto derived_midi = to_midi(*spelled);
      if (!derived_midi.has_value()) {
        return std::unexpected(std::string(derived_midi.error()));
      }
      LessonNote ln{};
      ln.spelled = *spelled;
      ln.midi = *derived_midi;
      if (nobj.contains("midi")) {
        if (!nobj.at("midi").is_number_integer()) {
          return std::unexpected("Note 'midi' must be an integer");
        }
        const int m = nobj.at("midi").get<int>();
        if (m < 0 || m > 127) {
          return std::unexpected("Note 'midi' out of range [0, 127]");
        }
        ln.midi = static_cast<MidiPitch>(m);
      }
      if (nobj.contains("finger")) {
        if (!nobj.at("finger").is_number_integer()) {
          return std::unexpected("Note 'finger' must be an integer");
        }
        const int f = nobj.at("finger").get<int>();
        if (f < 0 || f > 255) {
          return std::unexpected("Note 'finger' out of range");
        }
        ln.finger = static_cast<std::uint8_t>(f);
      }
      if (nobj.contains("tie_start")) {
        if (!nobj.at("tie_start").is_boolean()) {
          return std::unexpected("Note 'tie_start' must be boolean");
        }
        ln.tie_start = nobj.at("tie_start").get<bool>();
      }
      if (nobj.contains("tie_stop")) {
        if (!nobj.at("tie_stop").is_boolean()) {
          return std::unexpected("Note 'tie_stop' must be boolean");
        }
        ln.tie_stop = nobj.at("tie_stop").get<bool>();
      }
      ev.notes.push_back(ln);
    }
  }
  return ev;
}

void validate_staff_events(std::uint32_t bar_number, std::string_view staff_name,
                           std::span<const LessonEvent> events, int measure_ticks,
                           std::vector<MidiPitch>& open_ties, std::vector<std::string>& errors) {
  std::map<std::uint8_t, std::vector<std::pair<int, int>>> voice_spans;
  for (std::size_t idx = 0; idx < events.size(); ++idx) {
    const auto& ev = events[idx];
    const int dur = ev.duration.ticks();
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
  for (const auto& rv : kRhythmicValuesDesc) {
    if (rv.ticks() == ticks) {
      return rv;
    }
  }
  return std::nullopt;
}

std::vector<RhythmicValue> split_ticks_to_values(int total_ticks) {
  std::vector<RhythmicValue> out;
  int rem = total_ticks;
  while (rem >= 120) {
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
    const int mticks = cur_meter.measure_ticks();
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
  }

  for (const MidiPitch p : treble_open_ties) {
    errors.push_back(std::format("Dangling treble tie_start on MIDI {}", p));
  }
  for (const MidiPitch p : bass_open_ties) {
    errors.push_back(std::format("Dangling bass tie_start on MIDI {}", p));
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

std::string lesson_score_to_json(const LessonScore& score) {
  json j;
  j["meta"] = {
      {"id", score.meta.id},
      {"tier", score.meta.tier},
      {"order_in_tier", score.meta.order_in_tier},
      {"title", score.meta.title},
      {"composer_source", score.meta.composer_source},
      {"key",
       {{"fifths", score.meta.key.fifths},
        {"mode", score.meta.key.mode == Mode::Minor ? "minor" : "major"}}},
      {"time_signature",
       {{"numerator", score.meta.time_signature.numerator},
        {"denominator", score.meta.time_signature.denominator}}},
      {"target_bpm", score.meta.target_bpm},
      {"theory",
       {{"summary", score.meta.theory.summary},
        {"pattern_tip", score.meta.theory.pattern_tip},
        {"technique_tip", score.meta.theory.technique_tip},
        {"ballade_link", score.meta.theory.ballade_link}}},
      {"is_imported", score.meta.is_imported},
      {"source_filename", score.meta.source_filename},
  };

  json measures_arr = json::array();
  for (const auto& m : score.measures) {
    json mj;
    mj["bar_number"] = m.bar_number;
    if (m.key_change.has_value()) {
      mj["key_change"] = {
          {"fifths", m.key_change->fifths},
          {"mode", m.key_change->mode == Mode::Minor ? "minor" : "major"},
      };
    }
    if (m.meter_change.has_value()) {
      mj["meter_change"] = {
          {"numerator", m.meter_change->numerator},
          {"denominator", m.meter_change->denominator},
      };
    }
    json treble_arr = json::array();
    for (const auto& ev : m.treble_events) {
      treble_arr.push_back(serialize_event(ev));
    }
    json bass_arr = json::array();
    for (const auto& ev : m.bass_events) {
      bass_arr.push_back(serialize_event(ev));
    }
    json harm_arr = json::array();
    for (const auto& h : m.harmony) {
      harm_arr.push_back({
          {"start_tick", h.start_tick},
          {"chord_symbol", h.chord_symbol},
          {"roman_numeral", h.roman_numeral},
      });
    }
    mj["treble_events"] = std::move(treble_arr);
    mj["bass_events"] = std::move(bass_arr);
    mj["harmony"] = std::move(harm_arr);
    measures_arr.push_back(std::move(mj));
  }
  j["measures"] = std::move(measures_arr);

  json phrases_arr = json::array();
  for (const auto& ph : score.phrases) {
    phrases_arr.push_back({
        {"label", ph.label},
        {"start_bar", ph.start_bar},
        {"end_bar", ph.end_bar},
    });
  }
  j["phrases"] = std::move(phrases_arr);

  return j.dump(2);
}

std::expected<LessonScore, std::string> lesson_score_from_json(std::string_view json_text) {
  const json root = json::parse(json_text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    return std::unexpected("Malformed LessonScore JSON");
  }

  if (!root.contains("meta") || !root.at("meta").is_object()) {
    return std::unexpected("LessonScore missing 'meta' object");
  }
  const auto& meta = root.at("meta");
  LessonScore out{};

  if (!meta.contains("id") || !meta.at("id").is_string() || !meta.contains("title") ||
      !meta.at("title").is_string()) {
    return std::unexpected("LessonMetadata missing 'id' or 'title'");
  }
  out.meta.id = meta.at("id").get<std::string>();
  out.meta.title = meta.at("title").get<std::string>();
  if (meta.contains("tier") && meta.at("tier").is_number_integer()) {
    out.meta.tier = static_cast<std::uint8_t>(std::clamp(meta.at("tier").get<int>(), 0, 255));
  }
  if (meta.contains("order_in_tier") && meta.at("order_in_tier").is_number_integer()) {
    out.meta.order_in_tier =
        static_cast<std::uint8_t>(std::clamp(meta.at("order_in_tier").get<int>(), 0, 255));
  }
  if (meta.contains("composer_source") && meta.at("composer_source").is_string()) {
    out.meta.composer_source = meta.at("composer_source").get<std::string>();
  }
  if (meta.contains("key")) {
    const auto key = parse_key_json(meta.at("key"));
    if (!key.has_value()) {
      return std::unexpected(key.error());
    }
    out.meta.key = *key;
  }
  if (meta.contains("time_signature")) {
    const auto ts = parse_meter_json(meta.at("time_signature"));
    if (!ts.has_value()) {
      return std::unexpected(ts.error());
    }
    out.meta.time_signature = *ts;
  }
  if (meta.contains("target_bpm") && meta.at("target_bpm").is_number()) {
    out.meta.target_bpm = meta.at("target_bpm").get<double>();
  }
  if (meta.contains("is_imported") && meta.at("is_imported").is_boolean()) {
    out.meta.is_imported = meta.at("is_imported").get<bool>();
  }
  if (meta.contains("source_filename") && meta.at("source_filename").is_string()) {
    out.meta.source_filename = meta.at("source_filename").get<std::string>();
  }
  if (meta.contains("theory") && meta.at("theory").is_object()) {
    const auto& th = meta.at("theory");
    if (th.contains("summary") && th.at("summary").is_string()) {
      out.meta.theory.summary = th.at("summary").get<std::string>();
    }
    if (th.contains("pattern_tip") && th.at("pattern_tip").is_string()) {
      out.meta.theory.pattern_tip = th.at("pattern_tip").get<std::string>();
    }
    if (th.contains("technique_tip") && th.at("technique_tip").is_string()) {
      out.meta.theory.technique_tip = th.at("technique_tip").get<std::string>();
    }
    if (th.contains("ballade_link") && th.at("ballade_link").is_string()) {
      out.meta.theory.ballade_link = th.at("ballade_link").get<std::string>();
    }
  }

  if (!root.contains("measures") || !root.at("measures").is_array()) {
    return std::unexpected("LessonScore missing 'measures' array");
  }
  std::uint32_t fallback_bar = 1;
  for (const auto& mj : root.at("measures")) {
    if (!mj.is_object()) {
      return std::unexpected("Measure entry must be an object");
    }
    LessonMeasure m{};
    m.bar_number = mj.contains("bar_number") && mj.at("bar_number").is_number_integer()
                       ? mj.at("bar_number").get<std::uint32_t>()
                       : fallback_bar;
    ++fallback_bar;

    if (mj.contains("key_change") && !mj.at("key_change").is_null()) {
      const auto kc = parse_key_json(mj.at("key_change"));
      if (!kc.has_value()) {
        return std::unexpected(kc.error());
      }
      m.key_change = *kc;
    }
    if (mj.contains("meter_change") && !mj.at("meter_change").is_null()) {
      const auto mc = parse_meter_json(mj.at("meter_change"));
      if (!mc.has_value()) {
        return std::unexpected(mc.error());
      }
      m.meter_change = *mc;
    }

    if (mj.contains("treble_events")) {
      if (!mj.at("treble_events").is_array()) {
        return std::unexpected("Measure 'treble_events' must be an array");
      }
      for (const auto& ej : mj.at("treble_events")) {
        const auto ev = parse_event_json(ej);
        if (!ev.has_value()) {
          return std::unexpected(ev.error());
        }
        m.treble_events.push_back(*ev);
      }
    }
    if (mj.contains("bass_events")) {
      if (!mj.at("bass_events").is_array()) {
        return std::unexpected("Measure 'bass_events' must be an array");
      }
      for (const auto& ej : mj.at("bass_events")) {
        const auto ev = parse_event_json(ej);
        if (!ev.has_value()) {
          return std::unexpected(ev.error());
        }
        m.bass_events.push_back(*ev);
      }
    }
    if (mj.contains("harmony")) {
      if (!mj.at("harmony").is_array()) {
        return std::unexpected("Measure 'harmony' must be an array");
      }
      for (const auto& hj : mj.at("harmony")) {
        if (!hj.is_object()) {
          return std::unexpected("Harmony entry must be an object");
        }
        HarmonicAnnotation ha{};
        if (hj.contains("start_tick") && hj.at("start_tick").is_number_integer()) {
          ha.start_tick = hj.at("start_tick").get<int>();
        }
        if (hj.contains("chord_symbol") && hj.at("chord_symbol").is_string()) {
          ha.chord_symbol = hj.at("chord_symbol").get<std::string>();
        }
        if (hj.contains("roman_numeral") && hj.at("roman_numeral").is_string()) {
          ha.roman_numeral = hj.at("roman_numeral").get<std::string>();
        }
        m.harmony.push_back(std::move(ha));
      }
    }
    out.measures.push_back(std::move(m));
  }

  if (root.contains("phrases") && root.at("phrases").is_array()) {
    for (const auto& pj : root.at("phrases")) {
      if (!pj.is_object()) {
        return std::unexpected("Phrase entry must be an object");
      }
      LessonPhrase ph{};
      if (pj.contains("label") && pj.at("label").is_string()) {
        ph.label = pj.at("label").get<std::string>();
      }
      if (pj.contains("start_bar") && pj.at("start_bar").is_number_unsigned()) {
        ph.start_bar = pj.at("start_bar").get<std::size_t>();
      }
      if (pj.contains("end_bar") && pj.at("end_bar").is_number_unsigned()) {
        ph.end_bar = pj.at("end_bar").get<std::size_t>();
      }
      out.phrases.push_back(std::move(ph));
    }
  }

  return out;
}

} // namespace selfano::core

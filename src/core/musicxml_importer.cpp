#include "core/musicxml_importer.hpp"

#include "core/chord_symbol.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool iends_with(std::string_view text, std::string_view suffix) noexcept {
  if (text.size() < suffix.size()) {
    return false;
  }
  return iequals(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] std::string trim_copy(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return std::string(sv);
}

[[nodiscard]] std::optional<int> parse_int_sv(std::string_view sv) noexcept {
  const std::string t = trim_copy(sv);
  if (t.empty()) {
    return std::nullopt;
  }
  int val = 0;
  const char* begin = t.data();
  const char* end = t.data() + t.size();
  const auto [ptr, ec] = std::from_chars(begin, end, val);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return val;
}

[[nodiscard]] std::optional<double> parse_double_sv(std::string_view sv) noexcept {
  const std::string t = trim_copy(sv);
  if (t.empty()) {
    return std::nullopt;
  }

  std::size_t idx = 0;
  bool negative = false;
  if (t[idx] == '-' || t[idx] == '+') {
    negative = (t[idx] == '-');
    ++idx;
  }

  double value = 0.0;
  std::size_t digit_count = 0;
  while (idx < t.size() && std::isdigit(static_cast<unsigned char>(t[idx])) != 0) {
    value = value * 10.0 + static_cast<double>(t[idx] - '0');
    ++idx;
    ++digit_count;
  }

  if (idx < t.size() && t[idx] == '.') {
    ++idx;
    double frac_scale = 0.1;
    while (idx < t.size() && std::isdigit(static_cast<unsigned char>(t[idx])) != 0) {
      value += static_cast<double>(t[idx] - '0') * frac_scale;
      frac_scale *= 0.1;
      ++idx;
      ++digit_count;
    }
  }

  if (digit_count == 0) {
    return std::nullopt;
  }

  if (idx < t.size() && (t[idx] == 'e' || t[idx] == 'E')) {
    ++idx;
    bool exp_negative = false;
    if (idx < t.size() && (t[idx] == '-' || t[idx] == '+')) {
      exp_negative = (t[idx] == '-');
      ++idx;
    }
    std::size_t exp_digits = 0;
    int exp_val = 0;
    while (idx < t.size() && std::isdigit(static_cast<unsigned char>(t[idx])) != 0) {
      if (exp_val < 10000) {
        exp_val = exp_val * 10 + (t[idx] - '0');
      }
      ++idx;
      ++exp_digits;
    }
    if (exp_digits == 0) {
      return std::nullopt;
    }
    if (exp_negative) {
      exp_val = -exp_val;
    }
    if (value != 0.0) {
      value *= std::pow(10.0, static_cast<double>(exp_val));
    }
  }

  if (idx != t.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return negative ? -value : value;
}

[[nodiscard]] std::optional<NoteLetter> parse_step_letter(std::string_view s) noexcept {
  const std::string t = trim_copy(s);
  if (t.empty()) {
    return std::nullopt;
  }
  switch (static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])))) {
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

[[nodiscard]] Accidental alter_to_accidental(int alter) noexcept {
  switch (std::clamp(alter, -2, 2)) {
  case -2:
    return Accidental::DoubleFlat;
  case -1:
    return Accidental::Flat;
  case 0:
    return Accidental::Natural;
  case 1:
    return Accidental::Sharp;
  case 2:
    return Accidental::DoubleSharp;
  default:
    return Accidental::Natural;
  }
}

[[nodiscard]] std::string format_pitch_for_chord(std::string_view step, int alter) {
  std::string out = trim_copy(step);
  if (out.empty()) {
    return {};
  }
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  if (alter > 0) {
    out.push_back('#');
  } else if (alter < 0) {
    out.push_back('b');
  }
  return out;
}

[[nodiscard]] std::string_view map_harmony_kind_suffix(std::string_view kind) noexcept {
  if (kind.empty() || kind == "major") {
    return "";
  }
  if (kind == "minor") {
    return "m";
  }
  if (kind == "dominant") {
    return "7";
  }
  if (kind == "major-seventh") {
    return "maj7";
  }
  if (kind == "minor-seventh") {
    return "m7";
  }
  if (kind == "diminished") {
    return "dim";
  }
  if (kind == "diminished-seventh") {
    return "dim7";
  }
  if (kind == "half-diminished") {
    return "m7b5";
  }
  if (kind == "augmented") {
    return "aug";
  }
  if (kind == "suspended-second") {
    return "sus2";
  }
  if (kind == "suspended-fourth") {
    return "sus4";
  }
  if (kind == "major-sixth") {
    return "6";
  }
  if (kind == "minor-sixth") {
    return "m6";
  }
  if (kind == "major-ninth") {
    return "maj9";
  }
  if (kind == "minor-ninth") {
    return "m9";
  }
  if (kind == "dominant-ninth") {
    return "9";
  }
  if (kind == "major-minor") {
    return "mMaj7";
  }
  if (kind == "power") {
    return "5";
  }
  return "";
}

[[nodiscard]] std::optional<RhythmicValue> parse_musicxml_type(std::string_view type_text,
                                                               bool dotted) noexcept {
  const std::string t = trim_copy(type_text);
  if (t == "16th") {
    return RhythmicValue{.base = NoteBaseDuration::Sixteenth, .dotted = dotted};
  }
  if (t == "eighth") {
    return RhythmicValue{.base = NoteBaseDuration::Eighth, .dotted = dotted};
  }
  if (t == "quarter") {
    return RhythmicValue{.base = NoteBaseDuration::Quarter, .dotted = dotted};
  }
  if (t == "half") {
    return RhythmicValue{.base = NoteBaseDuration::Half, .dotted = dotted};
  }
  if (t == "whole") {
    return RhythmicValue{.base = NoteBaseDuration::Whole, .dotted = dotted};
  }
  return std::nullopt;
}

[[nodiscard]] int quantize_raw_ticks(int raw_ticks) noexcept {
  const int clamped = std::max(120, raw_ticks);
  const int rounded = ((clamped + 60) / 120) * 120;
  return std::max(120, rounded);
}

struct RawEvent {
  int start_tick{0};
  int duration_ticks{480};
  bool is_rest{false};
  std::uint8_t voice{0};
  std::vector<LessonNote> notes;
};

void normalize_staff_events(std::vector<RawEvent>& raw_events, int measure_ticks,
                            std::vector<LessonEvent>& out_events) {
  out_events.clear();
  std::map<std::uint8_t, std::vector<RawEvent>> by_voice;
  for (auto& ev : raw_events) {
    if (ev.start_tick < 0 || ev.start_tick >= measure_ticks || ev.duration_ticks <= 0) {
      continue;
    }
    ev.duration_ticks = std::min(ev.duration_ticks, measure_ticks - ev.start_tick);
    if (ev.duration_ticks < 120) {
      continue;
    }
    by_voice[ev.voice].push_back(std::move(ev));
  }

  auto emit_split_event = [&](int start, int dur, bool is_rest, std::uint8_t voice,
                              const std::vector<LessonNote>& notes) {
    const auto vals = split_ticks_to_values(dur);
    int cur = start;
    for (std::size_t vi = 0; vi < vals.size(); ++vi) {
      LessonEvent lev{};
      lev.start_tick = cur;
      lev.duration = vals[vi];
      lev.is_rest = is_rest;
      lev.voice = voice;
      if (!is_rest) {
        lev.notes = notes;
        for (auto& n : lev.notes) {
          if (vi > 0U) {
            n.tie_stop = true;
          }
          if (vi + 1U < vals.size()) {
            n.tie_start = true;
          }
        }
      }
      out_events.push_back(std::move(lev));
      cur += vals[vi].ticks();
    }
  };

  auto emit_rest_gap = [&](int start, int gap_ticks, std::uint8_t voice) {
    if (gap_ticks < 120) {
      return;
    }
    int cur = start;
    for (const auto& rv : split_ticks_to_values(gap_ticks)) {
      out_events.push_back(LessonEvent{
          .start_tick = cur,
          .duration = rv,
          .is_rest = true,
          .voice = voice,
          .notes = {},
      });
      cur += rv.ticks();
    }
  };

  // Primary voice 0 must cover [0, measure_ticks) cleanly without overlaps
  auto& v0 = by_voice[0U];
  std::ranges::sort(v0, [](const RawEvent& a, const RawEvent& b) {
    if (a.start_tick != b.start_tick) {
      return a.start_tick < b.start_tick;
    }
    return (!a.is_rest && b.is_rest);
  });

  int cursor = 0;
  for (const auto& ev : v0) {
    int st = std::max(cursor, ev.start_tick);
    if (st >= measure_ticks) {
      break;
    }
    if (st > cursor) {
      const int aligned_gap = ((st - cursor) / 120) * 120;
      if (aligned_gap >= 120) {
        emit_rest_gap(cursor, aligned_gap, 0U);
        cursor += aligned_gap;
      }
      st = cursor;
    }
    int dur = quantize_raw_ticks(std::min(ev.duration_ticks, measure_ticks - st));
    dur = std::min(dur, ((measure_ticks - st) / 120) * 120);
    if (dur < 120) {
      continue;
    }
    emit_split_event(st, dur, ev.is_rest, 0U, ev.notes);
    cursor = st + dur;
  }
  if (cursor < measure_ticks) {
    emit_rest_gap(cursor, measure_ticks - cursor, 0U);
  }

  // Secondary voices (> 0): keep non-overlapping within [0, measure_ticks)
  for (auto& [voice_id, vec] : by_voice) {
    if (voice_id == 0U) {
      continue;
    }
    std::ranges::sort(
        vec, [](const RawEvent& a, const RawEvent& b) { return a.start_tick < b.start_tick; });
    int v_cursor = 0;
    for (const auto& ev : vec) {
      if (ev.is_rest) {
        continue;
      }
      const int st = std::max(v_cursor, ev.start_tick);
      if (st >= measure_ticks) {
        break;
      }
      const int dur =
          std::min(quantize_raw_ticks(ev.duration_ticks), ((measure_ticks - st) / 120) * 120);
      if (dur < 120) {
        continue;
      }
      emit_split_event(st, dur, false, voice_id, ev.notes);
      v_cursor = st + dur;
    }
  }
}

void repair_staff_ties(std::vector<LessonMeasure>& measures, bool treble,
                       std::vector<std::string>& warnings) {
  std::vector<MidiPitch> open_ties;
  bool repaired = false;

  for (auto& m : measures) {
    auto& events = treble ? m.treble_events : m.bass_events;
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
      auto& ev = events[idx];
      for (auto& n : ev.notes) {
        if (n.tie_stop) {
          const auto it = std::ranges::find(open_ties, n.midi);
          if (it == open_ties.end()) {
            n.tie_stop = false;
            repaired = true;
          } else {
            open_ties.erase(it);
          }
        }
      }
      for (auto& n : ev.notes) {
        if (n.tie_start) {
          open_ties.push_back(n.midi);
        }
      }
    }
  }

  if (!open_ties.empty()) {
    repaired = true;
    for (auto it_m = measures.rbegin(); it_m != measures.rend() && !open_ties.empty(); ++it_m) {
      auto& events = treble ? it_m->treble_events : it_m->bass_events;
      for (auto it_e = events.rbegin(); it_e != events.rend() && !open_ties.empty(); ++it_e) {
        for (auto& n : it_e->notes) {
          if (n.tie_start) {
            const auto found = std::ranges::find(open_ties, n.midi);
            if (found != open_ties.end()) {
              n.tie_start = false;
              open_ties.erase(found);
            }
          }
        }
      }
    }
  }

  if (repaired) {
    warnings.push_back(std::string(treble ? "Treble" : "Bass") +
                       ": normalized incomplete tie spans");
  }
}

[[nodiscard]] bool part_looks_grand_staff(pugi::xml_node part, std::string_view part_name) {
  for (pugi::xml_node meas = part.child("measure"); meas; meas = meas.next_sibling("measure")) {
    const pugi::xml_node attr = meas.child("attributes");
    if (attr) {
      if (const auto staves = parse_int_sv(attr.child_value("staves"));
          staves.has_value() && *staves >= 2) {
        return true;
      }
    }
    for (pugi::xml_node note = meas.child("note"); note; note = note.next_sibling("note")) {
      if (const auto st = parse_int_sv(note.child_value("staff")); st.has_value() && *st >= 2) {
        return true;
      }
    }
  }
  std::string lower(part_name);
  std::ranges::transform(lower, lower.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lower.find("piano") != std::string::npos || lower.find("keyboard") != std::string::npos ||
         lower.find("klavier") != std::string::npos;
}

[[nodiscard]] std::string first_clef_sign(pugi::xml_node part) {
  for (pugi::xml_node meas = part.child("measure"); meas; meas = meas.next_sibling("measure")) {
    const pugi::xml_node attr = meas.child("attributes");
    if (attr) {
      const pugi::xml_node clef = attr.child("clef");
      if (clef) {
        return trim_copy(clef.child_value("sign"));
      }
    }
  }
  return {};
}

struct MeasureParseContext {
  LessonScore& score;
  KeySignature cur_key{.fifths = 0, .mode = Mode::Major};
  TimeSignature cur_meter{.numerator = 4, .denominator = 4};
  bool initial_key_set{false};
  bool initial_meter_set{false};
  bool found_tempo{false};
  std::size_t skipped_grace_notes{0};
  std::size_t skipped_out_of_range{0};
  std::size_t total_measures{0};
  std::vector<std::size_t> phrase_break_bars{};
};

struct MeasureVoiceState {
  int cursor_ticks{0};
  int measure_ticks{kTicksPerQuarter * 4};
  int active_div{1};
  bool force_bass_staff{false};
  std::map<std::string, std::uint8_t> treble_voice_map;
  std::map<std::string, std::uint8_t> bass_voice_map;
  std::map<std::pair<int, std::uint8_t>, std::size_t> last_event_idx;
  std::vector<RawEvent>* treble_raw{nullptr};
  std::vector<RawEvent>* bass_raw{nullptr};
  std::vector<HarmonicAnnotation>* harmony_out{nullptr};
};

[[nodiscard]] std::uint8_t resolve_voice(std::map<std::string, std::uint8_t>& vmap,
                                         const std::string& vstr) {
  const std::string key = vstr.empty() ? std::string("1") : vstr;
  if (const auto it = vmap.find(key); it != vmap.end()) {
    return it->second;
  }
  const auto next_id = static_cast<std::uint8_t>(std::min<std::size_t>(15U, vmap.size()));
  vmap.emplace(key, next_id);
  return next_id;
}

void parse_attributes_node(pugi::xml_node attr, bool allow_header_updates, std::size_t bar_idx,
                           int& active_div, int& measure_ticks, MeasureParseContext& ctx) {
  if (!attr) {
    return;
  }
  if (const auto d = parse_int_sv(attr.child_value("divisions")); d.has_value() && *d > 0) {
    active_div = *d;
  }
  if (!allow_header_updates) {
    return;
  }
  if (const pugi::xml_node key_node = attr.child("key")) {
    if (const auto f = parse_int_sv(key_node.child_value("fifths")); f.has_value()) {
      const std::string mode_str = trim_copy(key_node.child_value("mode"));
      const Mode mode = iequals(mode_str, "minor") ? Mode::Minor : Mode::Major;
      if (const auto ks = make_key_signature(std::clamp(*f, -7, 7), mode); ks.has_value()) {
        ctx.cur_key = *ks;
        if (!ctx.initial_key_set && bar_idx == 0U) {
          ctx.score.meta.key = ctx.cur_key;
          ctx.initial_key_set = true;
        }
      }
    }
  }
  if (const pugi::xml_node time_node = attr.child("time")) {
    const auto beats = parse_int_sv(time_node.child_value("beats"));
    const auto btype = parse_int_sv(time_node.child_value("beat-type"));
    if (beats.has_value() && btype.has_value()) {
      TimeSignature ts{.numerator = static_cast<std::uint8_t>(std::clamp(*beats, 1, 16)),
                       .denominator = static_cast<std::uint8_t>(std::clamp(*btype, 2, 16))};
      if (ts.is_valid()) {
        ctx.cur_meter = ts;
        measure_ticks = ts.measure_ticks();
        if (!ctx.initial_meter_set && bar_idx == 0U) {
          ctx.score.meta.time_signature = ts;
          ctx.initial_meter_set = true;
        }
      }
    }
  }
}

void parse_harmony_node(pugi::xml_node child, int cursor_ticks, int measure_ticks,
                        std::vector<HarmonicAnnotation>& harmony_out) {
  const pugi::xml_node root_n = child.child("root");
  const std::string r_step = format_pitch_for_chord(
      root_n.child_value("root-step"), parse_int_sv(root_n.child_value("root-alter")).value_or(0));
  if (r_step.empty()) {
    return;
  }
  const std::string kind = trim_copy(child.child_value("kind"));
  std::string sym = r_step;
  sym += map_harmony_kind_suffix(kind);
  if (const pugi::xml_node bass_n = child.child("bass")) {
    const std::string b_step =
        format_pitch_for_chord(bass_n.child_value("bass-step"),
                               parse_int_sv(bass_n.child_value("bass-alter")).value_or(0));
    if (!b_step.empty()) {
      sym += "/" + b_step;
    }
  }
  if (parse_chord_symbol(sym).has_value()) {
    const int htick = std::clamp(cursor_ticks, 0, std::max(0, measure_ticks - 1));
    harmony_out.push_back(HarmonicAnnotation{
        .start_tick = htick,
        .chord_symbol = sym,
        .roman_numeral = {},
    });
  }
}

void parse_note_node(pugi::xml_node child, MeasureVoiceState& vs, MeasureParseContext& ctx) {
  if (child.child("grace")) {
    ++ctx.skipped_grace_notes;
    return;
  }

  int dur_ticks = 480;
  if (const auto raw_d = parse_int_sv(child.child_value("duration"));
      raw_d.has_value() && *raw_d > 0) {
    dur_ticks = quantize_raw_ticks(static_cast<int>(
        std::llround(static_cast<double>(*raw_d) * kTicksPerQuarter / vs.active_div)));
  } else {
    const bool has_dot = static_cast<bool>(child.child("dot"));
    if (const auto rv = parse_musicxml_type(child.child_value("type"), has_dot); rv.has_value()) {
      dur_ticks = rv->ticks();
    }
  }

  const bool is_chord = static_cast<bool>(child.child("chord"));
  const bool is_rest = static_cast<bool>(child.child("rest"));
  const std::string voice_str = trim_copy(child.child_value("voice"));
  const auto staff_attr = parse_int_sv(child.child_value("staff"));

  if (is_rest) {
    const int staff_id = vs.force_bass_staff ? 2 : staff_attr.value_or(1);
    auto& vec = (staff_id == 2) ? *vs.bass_raw : *vs.treble_raw;
    const std::uint8_t v =
        resolve_voice((staff_id == 2) ? vs.bass_voice_map : vs.treble_voice_map, voice_str);
    if (!is_chord) {
      vec.push_back(RawEvent{
          .start_tick = vs.cursor_ticks,
          .duration_ticks = dur_ticks,
          .is_rest = true,
          .voice = v,
          .notes = {},
      });
      vs.last_event_idx[{staff_id, v}] = vec.size() - 1U;
      vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
    }
    return;
  }

  const pugi::xml_node pitch_n = child.child("pitch");
  const auto letter = parse_step_letter(pitch_n.child_value("step"));
  const auto octave = parse_int_sv(pitch_n.child_value("octave"));
  if (!letter.has_value() || !octave.has_value()) {
    if (!is_chord) {
      vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
    }
    return;
  }

  const int alter = parse_int_sv(pitch_n.child_value("alter")).value_or(0);
  const SpelledNote sp{
      .letter = *letter, .accidental = alter_to_accidental(alter), .octave = *octave};
  const auto midi_opt = to_midi(sp);
  if (!midi_opt.has_value() || *midi_opt < kPianoLowestMidi || *midi_opt > kPianoHighestMidi) {
    ++ctx.skipped_out_of_range;
    if (!is_chord) {
      vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
    }
    return;
  }

  LessonNote ln{};
  ln.spelled = sp;
  ln.midi = *midi_opt;

  if (const auto f_opt =
          parse_int_sv(child.child("notations").child("technical").child_value("fingering"));
      f_opt.has_value() && *f_opt >= 1 && *f_opt <= 5) {
    ln.finger = static_cast<std::uint8_t>(*f_opt);
  }

  auto apply_tie_attr = [&](pugi::xml_node t) {
    const std::string_view tt = t.attribute("type").as_string();
    if (tt == "start") {
      ln.tie_start = true;
    } else if (tt == "stop") {
      ln.tie_stop = true;
    }
  };
  for (pugi::xml_node t : child.children("tie")) {
    apply_tie_attr(t);
  }
  for (pugi::xml_node t : child.child("notations").children("tied")) {
    apply_tie_attr(t);
  }

  int staff_id = 1;
  if (vs.force_bass_staff) {
    staff_id = 2;
  } else if (staff_attr.has_value()) {
    staff_id = (*staff_attr == 2) ? 2 : 1;
  } else {
    staff_id = (ln.midi >= 60U) ? 1 : 2;
  }

  auto& vec = (staff_id == 2) ? *vs.bass_raw : *vs.treble_raw;
  const std::uint8_t v =
      resolve_voice((staff_id == 2) ? vs.bass_voice_map : vs.treble_voice_map, voice_str);

  if (is_chord && vs.last_event_idx.contains({staff_id, v})) {
    vec[vs.last_event_idx[{staff_id, v}]].notes.push_back(ln);
  } else {
    const int ev_start = (is_chord && !vec.empty()) ? vec.back().start_tick : vs.cursor_ticks;
    vec.push_back(RawEvent{
        .start_tick = ev_start,
        .duration_ticks = dur_ticks,
        .is_rest = false,
        .voice = v,
        .notes = {ln},
    });
    vs.last_event_idx[{staff_id, v}] = vec.size() - 1U;
    if (!is_chord) {
      vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
    }
  }
}

void parse_measure_nodes(pugi::xml_node meas, int& active_div, bool force_bass_staff,
                         bool allow_header_updates, int measure_ticks, std::size_t bar_idx,
                         MeasureParseContext& ctx, std::vector<RawEvent>& treble_raw,
                         std::vector<RawEvent>& bass_raw,
                         std::vector<HarmonicAnnotation>& harmony_out) {
  parse_attributes_node(meas.child("attributes"), allow_header_updates, bar_idx, active_div,
                        measure_ticks, ctx);

  MeasureVoiceState vs{};
  vs.measure_ticks = measure_ticks;
  vs.active_div = active_div;
  vs.force_bass_staff = force_bass_staff;
  vs.treble_raw = &treble_raw;
  vs.bass_raw = &bass_raw;
  vs.harmony_out = &harmony_out;

  for (pugi::xml_node child : meas.children()) {
    const std::string_view tag = child.name();
    if (tag == "direction") {
      if (!ctx.found_tempo) {
        if (const pugi::xml_node snd = child.child("sound")) {
          if (const auto t = parse_double_sv(snd.attribute("tempo").as_string());
              t.has_value() && *t > 0.0) {
            ctx.score.meta.target_bpm = std::clamp(*t, 36.0, 220.0);
            ctx.found_tempo = true;
          }
        }
        if (!ctx.found_tempo) {
          if (const pugi::xml_node met = child.child("direction-type").child("metronome")) {
            if (const auto t = parse_double_sv(met.child_value("per-minute"));
                t.has_value() && *t > 0.0) {
              ctx.score.meta.target_bpm = std::clamp(*t, 36.0, 220.0);
              ctx.found_tempo = true;
            }
          }
        }
      }
      if (child.child("direction-type").child("rehearsal") && bar_idx > 0U) {
        ctx.phrase_break_bars.push_back(bar_idx);
      }
    } else if (tag == "barline") {
      const std::string bstyle = trim_copy(child.child_value("bar-style"));
      if ((bstyle == "light-light" || bstyle == "light-heavy") &&
          bar_idx + 1U < ctx.total_measures) {
        ctx.phrase_break_bars.push_back(bar_idx + 1U);
      }
    } else if (tag == "backup") {
      if (const auto raw_d = parse_int_sv(child.child_value("duration")); raw_d.has_value()) {
        const int dt = static_cast<int>(
            std::llround(static_cast<double>(*raw_d) * kTicksPerQuarter / vs.active_div));
        vs.cursor_ticks = std::max(0, vs.cursor_ticks - dt);
      }
    } else if (tag == "forward") {
      if (const auto raw_d = parse_int_sv(child.child_value("duration")); raw_d.has_value()) {
        const int dt = static_cast<int>(
            std::llround(static_cast<double>(*raw_d) * kTicksPerQuarter / vs.active_div));
        vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dt);
      }
    } else if (tag == "harmony") {
      parse_harmony_node(child, vs.cursor_ticks, vs.measure_ticks, harmony_out);
    } else if (tag == "note") {
      parse_note_node(child, vs, ctx);
    }
  }
}

void build_phrases_from_breaks(std::vector<std::size_t>& phrase_break_bars, LessonScore& score) {
  std::ranges::sort(phrase_break_bars);
  phrase_break_bars.erase(std::ranges::unique(phrase_break_bars).begin(), phrase_break_bars.end());
  if (!phrase_break_bars.empty() && score.measures.size() >= 4U) {
    std::vector<std::size_t> starts{0U};
    for (const std::size_t b : phrase_break_bars) {
      if (b > starts.back() && b < score.measures.size()) {
        starts.push_back(b);
      }
    }
    if (starts.size() >= 2U) {
      for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::size_t sb = starts[i];
        const std::size_t eb =
            (i + 1U < starts.size()) ? (starts[i + 1U] - 1U) : (score.measures.size() - 1U);
        const char letter = static_cast<char>('A' + static_cast<int>(i % 26U));
        score.phrases.push_back(LessonPhrase{
            .label = std::format("Phrase {} (Bars {}-{})", letter, sb + 1U, eb + 1U),
            .start_bar = sb,
            .end_bar = eb,
        });
      }
    }
  }
  if (score.phrases.empty()) {
    score.phrases = auto_partition_phrases(score.measures.size());
  }
}

} // namespace

std::string generate_imported_lesson_id(std::string_view title, std::size_t measure_count,
                                        std::string_view content_fingerprint) {
  std::uint64_t h = 14695981039346656037ULL;
  auto mix = [&](std::string_view sv) {
    for (const char ch : sv) {
      h ^= static_cast<std::uint8_t>(ch);
      h *= 1099511628211ULL;
    }
    h ^= 0xFFU;
    h *= 1099511628211ULL;
  };
  mix(title);
  mix(std::to_string(measure_count));
  mix(content_fingerprint.substr(0, std::min<std::size_t>(4096U, content_fingerprint.size())));
  return std::format("imp_{:016x}", h);
}

std::expected<MusicXmlImportResult, std::string>
import_musicxml_string(std::string_view xml_text, MusicXmlImportOptions options) {
  if (xml_text.empty()) {
    return std::unexpected("MusicXML input is empty");
  }
  if (xml_text.size() > kMaxXmlPayloadBytes) {
    return std::unexpected("MusicXML payload exceeds 32 MiB limit");
  }

  pugi::xml_document doc;
  const pugi::xml_parse_result parse_res = doc.load_buffer(xml_text.data(), xml_text.size());
  if (!parse_res) {
    return std::unexpected(std::format("Invalid XML: {}", parse_res.description()));
  }

  const pugi::xml_node root = doc.child("score-partwise");
  if (!root) {
    if (doc.child("score-timewise")) {
      return std::unexpected("score-timewise MusicXML is not supported (export as score-partwise)");
    }
    return std::unexpected("XML document is missing <score-partwise> root element");
  }

  std::map<std::string, std::string> part_names;
  for (pugi::xml_node sp : root.child("part-list").children("score-part")) {
    part_names[sp.attribute("id").as_string()] = trim_copy(sp.child_value("part-name"));
  }

  std::vector<pugi::xml_node> parts;
  for (pugi::xml_node p : root.children("part")) {
    parts.push_back(p);
  }
  if (parts.empty()) {
    return std::unexpected("MusicXML score contains no <part> elements");
  }

  pugi::xml_node primary_part = parts.front();
  pugi::xml_node secondary_bass_part{};
  bool has_grand_or_piano_part = false;

  for (const auto& p : parts) {
    const std::string pid = p.attribute("id").as_string();
    const std::string pname = part_names.contains(pid) ? part_names.at(pid) : std::string{};
    if (part_looks_grand_staff(p, pname)) {
      primary_part = p;
      has_grand_or_piano_part = true;
      break;
    }
  }

  if (!has_grand_or_piano_part && parts.size() >= 2U) {
    const std::string c0 = first_clef_sign(parts[0]);
    const std::string c1 = first_clef_sign(parts[1]);
    if (c0 == "G" && c1 == "F") {
      secondary_bass_part = parts[1];
    }
  }

  MusicXmlImportResult result{};
  auto& score = result.score;

  std::string title = trim_copy(root.child("work").child_value("work-title"));
  if (title.empty()) {
    title = trim_copy(root.child("movement-title").child_value());
  }
  if (title.empty()) {
    title = options.fallback_title.empty() ? std::string("Imported Piece") : options.fallback_title;
  }

  std::string composer;
  for (pugi::xml_node cr : root.child("identification").children("creator")) {
    if (std::string_view(cr.attribute("type").as_string()) == "composer") {
      composer = trim_copy(cr.child_value());
      break;
    }
    if (composer.empty()) {
      composer = trim_copy(cr.child_value());
    }
  }
  if (composer.empty()) {
    composer = "Imported Score";
  }

  score.meta.tier = 0;
  score.meta.order_in_tier = 0;
  score.meta.title = title;
  score.meta.composer_source = composer;
  score.meta.is_imported = true;
  score.meta.key = KeySignature{.fifths = 0, .mode = Mode::Major};
  score.meta.time_signature = TimeSignature{.numerator = 4, .denominator = 4};
  score.meta.target_bpm = std::clamp(options.default_bpm, 36.0, 220.0);

  std::vector<pugi::xml_node> primary_measures;
  for (pugi::xml_node m : primary_part.children("measure")) {
    primary_measures.push_back(m);
  }
  if (primary_measures.empty()) {
    return std::unexpected("Selected MusicXML part contains zero measures");
  }

  std::vector<pugi::xml_node> bass_part_measures;
  if (secondary_bass_part) {
    for (pugi::xml_node m : secondary_bass_part.children("measure")) {
      bass_part_measures.push_back(m);
    }
  }

  if (primary_measures.size() > options.max_measures) {
    result.warnings.push_back(std::format("Truncated score from {} to {} measures",
                                          primary_measures.size(), options.max_measures));
    primary_measures.resize(options.max_measures);
  }

  int divisions = 1;
  int bass_divisions = 1;
  std::uint32_t next_bar_num = 1U;
  MeasureParseContext ctx{.score = score, .total_measures = primary_measures.size()};

  for (std::size_t bi = 0; bi < primary_measures.size(); ++bi) {
    const KeySignature prev_key = ctx.cur_key;
    const TimeSignature prev_meter = ctx.cur_meter;

    std::vector<RawEvent> treble_raw;
    std::vector<RawEvent> bass_raw;
    std::vector<HarmonicAnnotation> harmony_out;

    parse_measure_nodes(primary_measures[bi], divisions, false, true, ctx.cur_meter.measure_ticks(),
                        bi, ctx, treble_raw, bass_raw, harmony_out);

    if (bi < bass_part_measures.size()) {
      std::vector<RawEvent> unused_tr;
      std::vector<HarmonicAnnotation> unused_h;
      parse_measure_nodes(bass_part_measures[bi], bass_divisions, true, false,
                          ctx.cur_meter.measure_ticks(), bi, ctx, unused_tr, bass_raw, unused_h);
    }

    const int full_mticks = ctx.cur_meter.measure_ticks();
    const bool is_implicit =
        iequals(primary_measures[bi].attribute("implicit").as_string(), "yes") ||
        trim_copy(primary_measures[bi].attribute("number").as_string()) == "0";

    int mticks = full_mticks;
    LessonMeasure lm{};
    if (is_implicit) {
      int max_end = 0;
      for (const auto& ev : treble_raw) {
        if (ev.voice == 0U) {
          max_end = std::max(max_end, ev.start_tick + ev.duration_ticks);
        }
      }
      for (const auto& ev : bass_raw) {
        if (ev.voice == 0U) {
          max_end = std::max(max_end, ev.start_tick + ev.duration_ticks);
        }
      }
      const int quantized_end = (max_end > 0) ? quantize_raw_ticks(max_end) : 0;
      if (quantized_end >= 120 && quantized_end < full_mticks) {
        mticks = quantized_end;
        lm.pickup_ticks = quantized_end;
      }
    }

    if (bi == 0U && lm.pickup_ticks.has_value()) {
      lm.bar_number = 0U;
    } else {
      lm.bar_number = next_bar_num++;
    }

    if (bi > 0U && ctx.cur_key != prev_key) {
      lm.key_change = ctx.cur_key;
    }
    if (bi > 0U && ctx.cur_meter != prev_meter) {
      lm.meter_change = ctx.cur_meter;
    }

    for (auto& ha : harmony_out) {
      ha.start_tick = std::clamp(ha.start_tick, 0, std::max(0, mticks - 1));
    }

    normalize_staff_events(treble_raw, mticks, lm.treble_events);
    normalize_staff_events(bass_raw, mticks, lm.bass_events);
    lm.harmony = std::move(harmony_out);

    score.measures.push_back(std::move(lm));
  }

  repair_staff_ties(score.measures, true, result.warnings);
  repair_staff_ties(score.measures, false, result.warnings);

  if (ctx.skipped_grace_notes > 0U) {
    result.warnings.push_back(std::format("Skipped {} grace note(s) (ornaments excluded in v1)",
                                          ctx.skipped_grace_notes));
  }
  if (ctx.skipped_out_of_range > 0U) {
    result.warnings.push_back(
        std::format("Skipped {} note(s) outside 88-key range [A0..C8]", ctx.skipped_out_of_range));
  }
  if (!ctx.found_tempo) {
    result.warnings.push_back(
        std::format("No tempo mark found; defaulted to {:.0f} BPM", score.meta.target_bpm));
  }

  build_phrases_from_breaks(ctx.phrase_break_bars, score);

  score.meta.id = generate_imported_lesson_id(score.meta.title, score.measures.size(), xml_text);
  score.meta.theory.summary =
      std::format("Imported MusicXML study in {} ({}/{}, {} bars).",
                  format_key_name(score.meta.key), score.meta.time_signature.numerator,
                  score.meta.time_signature.denominator, score.measures.size());
  score.meta.theory.pattern_tip =
      "Practice in 4-to-8 bar phrase chunks using Wait-for-Note before Steady Pulse.";
  score.meta.theory.technique_tip =
      "Isolate Left Hand and Right Hand separately at 60% tempo, then combine.";
  score.meta.theory.ballade_link =
      "Imported My Books repertoire integrated with Selfano's Tempo Ladder & SRS.";

  if (const auto valid = validate_lesson_score(score); !valid.has_value()) {
    return std::unexpected(
        std::format("Imported MusicXML failed score validation: {}", valid.error().front()));
  }
  return result;
}

std::expected<MusicXmlImportResult, std::string>
import_musicxml_file(const std::filesystem::path& path, MusicXmlImportOptions options) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::unexpected(std::format("File does not exist: {}", path.string()));
  }
  const auto fsize = std::filesystem::file_size(path, ec);
  if (ec || fsize == 0U || fsize > kMaxXmlPayloadBytes) {
    return std::unexpected("MusicXML/.mxl file is empty or exceeds size limit");
  }

  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    return std::unexpected(std::format("Unable to open file: {}", path.string()));
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fsize));
  if (!ifs.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
    return std::unexpected(std::format("Failed reading file: {}", path.string()));
  }

  if (options.fallback_title == "Imported Piece" && path.has_stem()) {
    options.fallback_title = path.stem().string();
  }

  std::expected<MusicXmlImportResult, std::string> res =
      (is_mxl_zip_bytes(bytes) || iends_with(path.extension().string(), ".mxl"))
          ? import_mxl_bytes(bytes, std::move(options))
          : import_musicxml_string(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                std::move(options));

  if (res.has_value()) {
    res->score.meta.source_filename = path.filename().string();
  }
  return res;
}

} // namespace selfano::core

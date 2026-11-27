#include "core/musicxml_importer.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <system_error>
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
                                                               std::uint8_t dots) noexcept {
  const std::string t = trim_copy(type_text);
  const std::uint8_t clamped_dots = std::min<std::uint8_t>(dots, 2U);
  const bool dotted = clamped_dots > 0U;
  if (t == "32nd") {
    return RhythmicValue{
        .base = NoteBaseDuration::ThirtySecond, .dotted = dotted, .dots = clamped_dots};
  }
  if (t == "16th") {
    return RhythmicValue{
        .base = NoteBaseDuration::Sixteenth, .dotted = dotted, .dots = clamped_dots};
  }
  if (t == "eighth") {
    return RhythmicValue{.base = NoteBaseDuration::Eighth, .dotted = dotted, .dots = clamped_dots};
  }
  if (t == "quarter") {
    return RhythmicValue{.base = NoteBaseDuration::Quarter, .dotted = dotted, .dots = clamped_dots};
  }
  if (t == "half") {
    return RhythmicValue{.base = NoteBaseDuration::Half, .dotted = dotted, .dots = clamped_dots};
  }
  if (t == "whole") {
    return RhythmicValue{.base = NoteBaseDuration::Whole, .dotted = dotted, .dots = clamped_dots};
  }
  return std::nullopt;
}

[[nodiscard]] int quantize_raw_ticks(int raw_ticks) noexcept {
  const int clamped = std::max(60, raw_ticks);
  const int rounded = ((clamped + 15) / 30) * 30;
  return std::max(60, rounded);
}

struct RawEvent {
  int start_tick{0};
  int duration_ticks{480};
  bool is_rest{false};
  std::uint8_t voice{0};
  std::uint8_t articulations{0};
  std::vector<LessonNote> notes;
  std::optional<RhythmicValue> explicit_duration{};
  std::optional<TupletRatio> tuplet{};
  bool is_grace{false};
  bool grace_slash{true};
};

void normalize_staff_events(std::vector<RawEvent>& raw_events, int measure_ticks,
                            std::vector<LessonEvent>& out_events) {
  out_events.clear();
  std::map<std::uint8_t, std::vector<RawEvent>> by_voice;
  std::vector<RawEvent> grace_events;
  for (auto& ev : raw_events) {
    if (ev.is_grace) {
      if (ev.start_tick >= 0 && ev.start_tick < measure_ticks && !ev.notes.empty()) {
        grace_events.push_back(std::move(ev));
      }
      continue;
    }
    if (ev.start_tick < 0 || ev.start_tick >= measure_ticks || ev.duration_ticks <= 0) {
      continue;
    }
    ev.duration_ticks = std::min(ev.duration_ticks, measure_ticks - ev.start_tick);
    if (!ev.tuplet.has_value() && ev.duration_ticks < 60) {
      continue;
    }
    by_voice[ev.voice].push_back(std::move(ev));
  }

  auto emit_split_event = [&](int start, int dur, bool is_rest, std::uint8_t voice,
                              std::uint8_t articulations, const std::vector<LessonNote>& notes,
                              const std::optional<RhythmicValue>& explicit_dur,
                              const std::optional<TupletRatio>& tuplet) {
    if (tuplet.has_value() && explicit_dur.has_value()) {
      LessonEvent lev{};
      lev.start_tick = start;
      lev.duration = *explicit_dur;
      lev.is_rest = is_rest;
      lev.voice = voice;
      lev.tuplet = tuplet;
      if (lev.event_ticks() != dur) {
        lev.tick_duration_override = dur;
      }
      if (!is_rest) {
        lev.articulations = articulations;
        lev.notes = notes;
      }
      out_events.push_back(std::move(lev));
      return;
    }
    if (const auto qv = quantize_ticks_to_rhythmic_value(dur); qv.has_value()) {
      LessonEvent lev{};
      lev.start_tick = start;
      lev.duration = *qv;
      lev.is_rest = is_rest;
      lev.voice = voice;
      if (!is_rest) {
        lev.articulations = articulations;
        lev.notes = notes;
      }
      out_events.push_back(std::move(lev));
      return;
    }
    const auto vals = split_ticks_to_values(dur);
    int cur = start;
    for (std::size_t vi = 0; vi < vals.size(); ++vi) {
      LessonEvent lev{};
      lev.start_tick = cur;
      lev.duration = vals[vi];
      lev.is_rest = is_rest;
      lev.voice = voice;
      if (!is_rest) {
        if (vi == 0U) {
          lev.articulations = articulations;
        }
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
    if (gap_ticks < 60) {
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
      const int aligned_gap = ((st - cursor) / 60) * 60;
      if (aligned_gap >= 60) {
        emit_rest_gap(cursor, aligned_gap, 0U);
        cursor += aligned_gap;
      }
      st = cursor;
    }
    const int max_rem = measure_ticks - st;
    int dur = 0;
    if (ev.tuplet.has_value()) {
      dur = std::min(ev.duration_ticks, max_rem);
    } else {
      dur = quantize_raw_ticks(std::min(ev.duration_ticks, max_rem));
      dur = std::min(dur, (max_rem / 30) * 30);
    }
    if (dur < 30) {
      continue;
    }
    emit_split_event(st, dur, ev.is_rest, 0U, ev.articulations, ev.notes, ev.explicit_duration,
                     ev.tuplet);
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
      const int max_rem = measure_ticks - st;
      const int dur = ev.tuplet.has_value()
                          ? std::min(ev.duration_ticks, max_rem)
                          : std::min(quantize_raw_ticks(ev.duration_ticks), (max_rem / 30) * 30);
      if (dur < 30) {
        continue;
      }
      emit_split_event(st, dur, false, voice_id, ev.articulations, ev.notes, ev.explicit_duration,
                       ev.tuplet);
      v_cursor = st + dur;
    }
  }

  for (auto& gev : grace_events) {
    out_events.push_back(LessonEvent{
        .start_tick = gev.start_tick,
        .duration = gev.explicit_duration.value_or(
            RhythmicValue{.base = NoteBaseDuration::Eighth, .dotted = false}),
        .is_rest = false,
        .voice = gev.voice,
        .articulations = gev.articulations,
        .notes = std::move(gev.notes),
        .is_grace = true,
        .grace_slash = gev.grace_slash,
    });
  }
  std::ranges::stable_sort(out_events, [](const LessonEvent& a, const LessonEvent& b) {
    if (a.start_tick != b.start_tick) {
      return a.start_tick < b.start_tick;
    }
    if (a.is_grace != b.is_grace) {
      return a.is_grace;
    }
    return a.voice < b.voice;
  });
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

struct OpenHairpinState {
  std::size_t bar_idx{0};
  int start_tick{0};
  HairpinKind kind{HairpinKind::Crescendo};
};

struct OpenOttavaState {
  std::size_t bar_idx{0};
  int start_tick{0};
  int octave_shift{1};
};

struct MeasureParseContext {
  LessonScore& score;
  KeySignature cur_key{.fifths = 0, .mode = Mode::Major};
  TimeSignature cur_meter{.numerator = 4, .denominator = 4};
  Clef cur_treble_clef{Clef::Treble};
  Clef cur_bass_clef{Clef::Bass};
  bool initial_key_set{false};
  bool initial_meter_set{false};
  bool found_tempo{false};
  std::size_t skipped_grace_notes{0};
  std::size_t skipped_out_of_range{0};
  std::size_t skipped_micro_durations{0};
  std::size_t total_measures{0};
  std::vector<std::size_t> phrase_break_bars{};
  std::map<std::pair<int, int>, std::pair<std::size_t, int>> open_slurs{};
  std::optional<std::pair<std::size_t, int>> open_pedal{};
  std::optional<OpenHairpinState> open_hairpin{};
  std::map<int, OpenOttavaState> open_ottavas{};
};

struct MeasureVoiceState {
  std::size_t bar_idx{0};
  int cursor_ticks{0};
  int measure_ticks{kTicksPerQuarter * 4};
  int active_div{1};
  bool force_bass_staff{false};
  std::map<std::string, std::uint8_t> treble_voice_map;
  std::map<std::string, std::uint8_t> bass_voice_map;
  std::map<std::pair<int, std::uint8_t>, std::size_t> last_event_idx;
  std::map<std::pair<int, std::uint8_t>, std::uint16_t> active_tuplet_group_id;
  std::uint16_t next_tuplet_group_id{1};
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
                           int cursor_ticks, int& active_div, int& measure_ticks,
                           MeasureParseContext& ctx,
                           std::vector<ClefChange>* clef_changes_out = nullptr) {
  if (!attr) {
    return;
  }
  if (const auto d = parse_int_sv(attr.child_value("divisions")); d.has_value() && *d > 0) {
    active_div = *d;
  }
  if (!allow_header_updates) {
    return;
  }
  for (pugi::xml_node clef_n : attr.children("clef")) {
    const int num = parse_int_sv(clef_n.attribute("number").as_string()).value_or(1);
    const std::string sign = trim_copy(clef_n.child_value("sign"));
    std::optional<Clef> parsed_clef;
    if (sign == "G") {
      parsed_clef = Clef::Treble;
    } else if (sign == "F") {
      parsed_clef = Clef::Bass;
    }
    if (parsed_clef.has_value()) {
      const bool treble_staff = (num != 2);
      Clef& active = treble_staff ? ctx.cur_treble_clef : ctx.cur_bass_clef;
      if (*parsed_clef != active) {
        active = *parsed_clef;
        if (clef_changes_out != nullptr && !(bar_idx == 0U && cursor_ticks == 0)) {
          clef_changes_out->push_back(ClefChange{
              .start_tick = std::clamp(cursor_ticks, 0, std::max(0, measure_ticks - 1)),
              .clef = *parsed_clef,
              .treble_staff = treble_staff,
          });
        }
      }
    }
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
  const pugi::xml_node grace_node = child.child("grace");
  const bool is_grace = static_cast<bool>(grace_node);
  bool grace_slash = true;
  if (is_grace) {
    const std::string_view sl = grace_node.attribute("slash").as_string();
    if (sl == "no") {
      grace_slash = false;
    }
  }

  std::uint8_t dot_count = 0U;
  for (pugi::xml_node d : child.children("dot")) {
    (void)d;
    if (dot_count < 2U) {
      ++dot_count;
    }
  }
  const auto explicit_dur = parse_musicxml_type(child.child_value("type"), dot_count);

  const bool is_chord = static_cast<bool>(child.child("chord"));
  const bool is_rest = static_cast<bool>(child.child("rest"));
  const std::string voice_str = trim_copy(child.child_value("voice"));
  const auto staff_attr = parse_int_sv(child.child_value("staff"));
  const int resolved_staff = vs.force_bass_staff ? 2 : (staff_attr.value_or(1) == 2 ? 2 : 1);
  const std::uint8_t resolved_v =
      resolve_voice((resolved_staff == 2) ? vs.bass_voice_map : vs.treble_voice_map, voice_str);

  std::optional<TupletRatio> tuplet_ratio;
  if (const pugi::xml_node tm = child.child("time-modification")) {
    const auto act = parse_int_sv(tm.child_value("actual-notes"));
    const auto norm = parse_int_sv(tm.child_value("normal-notes"));
    if (act.has_value() && norm.has_value() && *act >= 2 && *act <= 16 && *norm >= 1 &&
        *norm <= 16) {
      bool is_start = false;
      bool is_stop = false;
      for (pugi::xml_node tup : child.child("notations").children("tuplet")) {
        const std::string_view ttype = tup.attribute("type").as_string();
        if (ttype == "start") {
          is_start = true;
        } else if (ttype == "stop") {
          is_stop = true;
        }
      }
      const std::pair<int, std::uint8_t> tkey{resolved_staff, resolved_v};
      if (is_start || !vs.active_tuplet_group_id.contains(tkey)) {
        vs.active_tuplet_group_id[tkey] = vs.next_tuplet_group_id++;
      }
      tuplet_ratio = TupletRatio{
          .actual = static_cast<std::uint8_t>(*act),
          .normal = static_cast<std::uint8_t>(*norm),
          .group_id = vs.active_tuplet_group_id[tkey],
      };
      if (is_stop) {
        vs.active_tuplet_group_id.erase(tkey);
      }
    }
  }

  int dur_ticks = 480;
  if (is_grace) {
    dur_ticks = 0;
  } else if (const auto raw_d = parse_int_sv(child.child_value("duration"));
             raw_d.has_value() && *raw_d > 0) {
    const int scaled = static_cast<int>(
        std::llround(static_cast<double>(*raw_d) * kTicksPerQuarter / vs.active_div));
    if (tuplet_ratio.has_value()) {
      dur_ticks = std::max(1, scaled);
    } else if (scaled < 30) {
      ++ctx.skipped_micro_durations;
      return;
    } else {
      dur_ticks = quantize_raw_ticks(scaled);
    }
  } else if (explicit_dur.has_value()) {
    const int base_t = explicit_dur->ticks();
    dur_ticks = tuplet_ratio.has_value()
                    ? std::max(1, (base_t * static_cast<int>(tuplet_ratio->normal)) /
                                      static_cast<int>(tuplet_ratio->actual))
                    : base_t;
  }

  if (is_rest) {
    if (is_grace) {
      ++ctx.skipped_grace_notes;
      return;
    }
    auto& vec = (resolved_staff == 2) ? *vs.bass_raw : *vs.treble_raw;
    if (!is_chord) {
      vec.push_back(RawEvent{
          .start_tick = vs.cursor_ticks,
          .duration_ticks = dur_ticks,
          .is_rest = true,
          .voice = resolved_v,
          .notes = {},
          .explicit_duration = explicit_dur,
          .tuplet = tuplet_ratio,
      });
      vs.last_event_idx[{resolved_staff, resolved_v}] = vec.size() - 1U;
      vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
    }
    return;
  }

  const pugi::xml_node pitch_n = child.child("pitch");
  const auto letter = parse_step_letter(pitch_n.child_value("step"));
  const auto octave = parse_int_sv(pitch_n.child_value("octave"));
  if (!letter.has_value() || !octave.has_value()) {
    if (!is_chord && !is_grace) {
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
    if (!is_chord && !is_grace) {
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

  std::uint8_t raw_art = 0U;
  const pugi::xml_node notations = child.child("notations");
  if (notations) {
    if (const pugi::xml_node arts = notations.child("articulations")) {
      if (arts.child("staccato")) {
        raw_art |= articulation_bit(ArticulationFlag::Staccato);
      }
      if (arts.child("tenuto")) {
        raw_art |= articulation_bit(ArticulationFlag::Tenuto);
      }
      if (arts.child("accent") || arts.child("strong-accent")) {
        raw_art |= articulation_bit(ArticulationFlag::Accent);
      }
    }
    if (notations.child("arpeggiate")) {
      raw_art |= articulation_bit(ArticulationFlag::Arpeggiate);
    }
  }

  int ev_start = std::clamp(vs.cursor_ticks, 0, std::max(0, vs.measure_ticks - 1));
  if (is_chord && !is_grace && vs.last_event_idx.contains({staff_id, v})) {
    auto& prev = vec[vs.last_event_idx[{staff_id, v}]];
    ev_start = prev.start_tick;
    prev.articulations |= raw_art;
    prev.notes.push_back(ln);
  } else {
    ev_start = (is_chord && !is_grace && !vec.empty())
                   ? vec.back().start_tick
                   : std::clamp(vs.cursor_ticks, 0, std::max(0, vs.measure_ticks - 1));
    vec.push_back(RawEvent{
        .start_tick = ev_start,
        .duration_ticks = dur_ticks,
        .is_rest = false,
        .voice = v,
        .articulations = raw_art,
        .notes = {ln},
        .explicit_duration = explicit_dur,
        .tuplet = tuplet_ratio,
        .is_grace = is_grace,
        .grace_slash = grace_slash,
    });
    if (!is_grace) {
      vs.last_event_idx[{staff_id, v}] = vec.size() - 1U;
      if (!is_chord) {
        vs.cursor_ticks = std::min(vs.measure_ticks, vs.cursor_ticks + dur_ticks);
      }
    }
  }

  if (notations) {
    for (pugi::xml_node sl : notations.children("slur")) {
      const std::string_view sl_type = sl.attribute("type").as_string();
      const int sl_num = parse_int_sv(sl.attribute("number").as_string()).value_or(1);
      const std::pair<int, int> key{staff_id, sl_num};
      const int tick = std::clamp(ev_start, 0, std::max(0, vs.measure_ticks - 1));
      if (sl_type == "start") {
        ctx.open_slurs[key] = {vs.bar_idx, tick};
      } else if (sl_type == "stop") {
        if (const auto it = ctx.open_slurs.find(key); it != ctx.open_slurs.end()) {
          const auto [sb, st] = it->second;
          if (vs.bar_idx > sb || (vs.bar_idx == sb && tick > st)) {
            ctx.score.slurs.push_back(SlurSpan{
                .start_bar = sb,
                .start_tick = st,
                .end_bar = vs.bar_idx,
                .end_tick = tick,
                .treble = (staff_id == 1),
            });
          }
          ctx.open_slurs.erase(it);
        }
      }
    }
  }
}

void parse_measure_nodes(pugi::xml_node meas, int& active_div, bool force_bass_staff,
                         bool allow_header_updates, int measure_ticks, std::size_t bar_idx,
                         MeasureParseContext& ctx, std::vector<RawEvent>& treble_raw,
                         std::vector<RawEvent>& bass_raw,
                         std::vector<HarmonicAnnotation>& harmony_out,
                         std::vector<DynamicEvent>* dynamics_out = nullptr,
                         std::vector<HairpinSpan>* hairpins_out = nullptr,
                         std::vector<ClefChange>* clef_changes_out = nullptr,
                         std::vector<TempoEvent>* tempo_changes_out = nullptr) {
  MeasureVoiceState vs{};
  vs.bar_idx = bar_idx;
  vs.measure_ticks = measure_ticks;
  vs.active_div = active_div;
  vs.force_bass_staff = force_bass_staff;
  vs.treble_raw = &treble_raw;
  vs.bass_raw = &bass_raw;
  vs.harmony_out = &harmony_out;

  for (pugi::xml_node child : meas.children()) {
    const std::string_view tag = child.name();
    if (tag == "attributes") {
      parse_attributes_node(child, allow_header_updates, bar_idx, vs.cursor_ticks, active_div,
                            vs.measure_ticks, ctx, clef_changes_out);
      vs.active_div = active_div;
    } else if (tag == "direction") {
      std::optional<double> dir_bpm;
      std::string dir_words;
      if (const pugi::xml_node snd = child.child("sound")) {
        if (const auto t = parse_double_sv(snd.attribute("tempo").as_string());
            t.has_value() && *t > 0.0) {
          dir_bpm = std::clamp(*t, 20.0, 300.0);
        }
      }
      for (pugi::xml_node dt : child.children("direction-type")) {
        if (!dir_bpm.has_value()) {
          if (const pugi::xml_node met = dt.child("metronome")) {
            if (const auto t = parse_double_sv(met.child_value("per-minute"));
                t.has_value() && *t > 0.0) {
              dir_bpm = std::clamp(*t, 20.0, 300.0);
            }
          }
        }
        if (dir_words.empty()) {
          if (const pugi::xml_node words = dt.child("words")) {
            const std::string w = trim_copy(words.text().as_string());
            if (!w.empty() && w.size() <= 40U) {
              dir_words = w;
            }
          }
        }
        if (dynamics_out != nullptr) {
          if (const pugi::xml_node dyn_n = dt.child("dynamics")) {
            for (pugi::xml_node dchild : dyn_n.children()) {
              if (const auto lvl = parse_dynamic_level(dchild.name()); lvl.has_value()) {
                dynamics_out->push_back(DynamicEvent{
                    .start_tick = std::clamp(vs.cursor_ticks, 0, std::max(0, vs.measure_ticks - 1)),
                    .level = *lvl,
                });
                break;
              }
            }
          }
        }
        if (hairpins_out != nullptr) {
          if (const pugi::xml_node wedge = dt.child("wedge")) {
            const std::string_view wtype = wedge.attribute("type").as_string();
            const int wtick = std::clamp(vs.cursor_ticks, 0, vs.measure_ticks);
            if (wtype == "crescendo" || wtype == "diminuendo") {
              ctx.open_hairpin = OpenHairpinState{
                  .bar_idx = bar_idx,
                  .start_tick = wtick,
                  .kind = (wtype == "crescendo") ? HairpinKind::Crescendo : HairpinKind::Diminuendo,
              };
            } else if (wtype == "stop" && ctx.open_hairpin.has_value()) {
              const int h0 =
                  (ctx.open_hairpin->bar_idx == bar_idx) ? ctx.open_hairpin->start_tick : 0;
              if (wtick > h0) {
                hairpins_out->push_back(HairpinSpan{
                    .start_tick = h0,
                    .end_tick = wtick,
                    .kind = ctx.open_hairpin->kind,
                });
              }
              ctx.open_hairpin.reset();
            }
          }
        }
        if (allow_header_updates) {
          if (const pugi::xml_node oshift = dt.child("octave-shift")) {
            const std::string_view otype = oshift.attribute("type").as_string();
            const int o_staff = parse_int_sv(child.child_value("staff")).value_or(1);
            const int staff_key = (o_staff == 2) ? 2 : 1;
            const int otick = std::clamp(vs.cursor_ticks, 0, vs.measure_ticks);
            if (otype == "down" || otype == "up") {
              // MusicXML: "down" = 8va (written lower, sounds +1 octave); "up" = 8vb (-1 octave)
              const int shift = (otype == "down") ? 1 : -1;
              if (const auto it = ctx.open_ottavas.find(staff_key); it != ctx.open_ottavas.end()) {
                if (bar_idx > it->second.bar_idx ||
                    (bar_idx == it->second.bar_idx && otick > it->second.start_tick)) {
                  ctx.score.ottavas.push_back(OttavaSpan{
                      .start_bar = it->second.bar_idx,
                      .start_tick = it->second.start_tick,
                      .end_bar = bar_idx,
                      .end_tick = otick,
                      .treble = (staff_key == 1),
                      .octave_shift = it->second.octave_shift,
                  });
                }
              }
              ctx.open_ottavas[staff_key] =
                  OpenOttavaState{.bar_idx = bar_idx, .start_tick = otick, .octave_shift = shift};
            } else if (otype == "stop") {
              if (const auto it = ctx.open_ottavas.find(staff_key); it != ctx.open_ottavas.end()) {
                if (bar_idx > it->second.bar_idx ||
                    (bar_idx == it->second.bar_idx && otick > it->second.start_tick)) {
                  ctx.score.ottavas.push_back(OttavaSpan{
                      .start_bar = it->second.bar_idx,
                      .start_tick = it->second.start_tick,
                      .end_bar = bar_idx,
                      .end_tick = otick,
                      .treble = (staff_key == 1),
                      .octave_shift = it->second.octave_shift,
                  });
                }
                ctx.open_ottavas.erase(it);
              }
            }
          }
          if (const pugi::xml_node ped = dt.child("pedal")) {
            const std::string_view ptype = ped.attribute("type").as_string();
            const int ptick = std::clamp(vs.cursor_ticks, 0, vs.measure_ticks);
            if (ptype == "start" || ptype == "change") {
              if (ctx.open_pedal.has_value()) {
                const auto [sb, st] = *ctx.open_pedal;
                if (bar_idx > sb || (bar_idx == sb && ptick > st)) {
                  ctx.score.pedals.push_back(PedalSpan{
                      .start_bar = sb, .start_tick = st, .end_bar = bar_idx, .end_tick = ptick});
                }
              }
              ctx.open_pedal = {bar_idx, ptick};
            } else if (ptype == "stop" && ctx.open_pedal.has_value()) {
              const auto [sb, st] = *ctx.open_pedal;
              if (bar_idx > sb || (bar_idx == sb && ptick > st)) {
                ctx.score.pedals.push_back(PedalSpan{
                    .start_bar = sb, .start_tick = st, .end_bar = bar_idx, .end_tick = ptick});
              }
              ctx.open_pedal.reset();
            }
          }
        }
        if (dt.child("rehearsal") && bar_idx > 0U) {
          ctx.phrase_break_bars.push_back(bar_idx);
        }
      }
      if (allow_header_updates) {
        if (!ctx.found_tempo && dir_bpm.has_value()) {
          ctx.score.meta.target_bpm = std::clamp(*dir_bpm, 36.0, 220.0);
          ctx.found_tempo = true;
          if (!dir_words.empty() && ctx.score.meta.tempo_marking.empty()) {
            ctx.score.meta.tempo_marking = dir_words;
          }
        } else if (dir_bpm.has_value() && tempo_changes_out != nullptr) {
          tempo_changes_out->push_back(TempoEvent{
              .start_tick = std::clamp(vs.cursor_ticks, 0, std::max(0, vs.measure_ticks - 1)),
              .bpm = *dir_bpm,
              .label = dir_words,
          });
        }
        if (bar_idx == 0U && !dir_words.empty() && ctx.score.meta.tempo_marking.empty()) {
          ctx.score.meta.tempo_marking = dir_words;
        }
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
  if (hairpins_out != nullptr && ctx.open_hairpin.has_value() &&
      ctx.open_hairpin->bar_idx == bar_idx) {
    const int h0 = std::clamp(ctx.open_hairpin->start_tick, 0, vs.measure_ticks);
    if (vs.measure_ticks > h0) {
      hairpins_out->push_back(HairpinSpan{
          .start_tick = h0,
          .end_tick = vs.measure_ticks,
          .kind = ctx.open_hairpin->kind,
      });
    }
    ctx.open_hairpin->bar_idx = bar_idx + 1U;
    ctx.open_hairpin->start_tick = 0;
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
    std::vector<DynamicEvent> dynamics_out;
    std::vector<HairpinSpan> hairpins_out;
    std::vector<ClefChange> clef_changes_out;
    std::vector<TempoEvent> tempo_changes_out;

    parse_measure_nodes(primary_measures[bi], divisions, false, true, ctx.cur_meter.measure_ticks(),
                        bi, ctx, treble_raw, bass_raw, harmony_out, &dynamics_out, &hairpins_out,
                        &clef_changes_out, &tempo_changes_out);

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
    for (auto& d : dynamics_out) {
      d.start_tick = std::clamp(d.start_tick, 0, std::max(0, mticks - 1));
    }
    std::vector<HairpinSpan> clamped_hp;
    for (auto hp : hairpins_out) {
      hp.start_tick = std::clamp(hp.start_tick, 0, mticks);
      hp.end_tick = std::clamp(hp.end_tick, 0, mticks);
      if (hp.end_tick > hp.start_tick) {
        clamped_hp.push_back(hp);
      }
    }

    for (auto& tc : tempo_changes_out) {
      tc.start_tick = std::clamp(tc.start_tick, 0, std::max(0, mticks - 1));
      tc.bpm = std::clamp(tc.bpm, 20.0, 300.0);
    }

    normalize_staff_events(treble_raw, mticks, lm.treble_events);
    normalize_staff_events(bass_raw, mticks, lm.bass_events);
    lm.harmony = std::move(harmony_out);
    lm.dynamics = std::move(dynamics_out);
    lm.hairpins = std::move(clamped_hp);
    lm.clef_changes = std::move(clef_changes_out);
    lm.tempo_changes = std::move(tempo_changes_out);

    score.measures.push_back(std::move(lm));
  }

  // Close any open pedal span cleanly and clamp slurs/pedals against actual bar lengths.
  std::vector<int> actual_bar_ticks;
  actual_bar_ticks.reserve(score.measures.size());
  TimeSignature span_meter = score.meta.time_signature;
  for (const auto& m : score.measures) {
    if (m.meter_change.has_value()) {
      span_meter = *m.meter_change;
    }
    actual_bar_ticks.push_back(effective_measure_ticks(m, span_meter));
  }
  if (ctx.open_pedal.has_value() && !score.measures.empty()) {
    const auto [sb, st] = *ctx.open_pedal;
    const std::size_t last_b = score.measures.size() - 1U;
    const int last_ticks = actual_bar_ticks[last_b];
    if (last_b > sb || (last_b == sb && last_ticks > st)) {
      score.pedals.push_back(PedalSpan{
          .start_bar = sb,
          .start_tick = st,
          .end_bar = last_b,
          .end_tick = last_ticks,
      });
    }
    ctx.open_pedal.reset();
  }
  if (!ctx.open_ottavas.empty() && !score.measures.empty()) {
    const std::size_t last_b = score.measures.size() - 1U;
    const int last_ticks = actual_bar_ticks[last_b];
    for (const auto& [staff_key, st] : ctx.open_ottavas) {
      if (last_b > st.bar_idx || (last_b == st.bar_idx && last_ticks > st.start_tick)) {
        score.ottavas.push_back(OttavaSpan{
            .start_bar = st.bar_idx,
            .start_tick = st.start_tick,
            .end_bar = last_b,
            .end_tick = last_ticks,
            .treble = (staff_key == 1),
            .octave_shift = st.octave_shift,
        });
      }
    }
    ctx.open_ottavas.clear();
  }
  auto clamp_spans = [&](auto& spans) {
    for (auto it = spans.begin(); it != spans.end();) {
      if (it->start_bar >= actual_bar_ticks.size() || it->end_bar >= actual_bar_ticks.size()) {
        it = spans.erase(it);
        continue;
      }
      it->start_tick = std::clamp(it->start_tick, 0, actual_bar_ticks[it->start_bar]);
      it->end_tick = std::clamp(it->end_tick, 0, actual_bar_ticks[it->end_bar]);
      if (it->start_bar > it->end_bar ||
          (it->start_bar == it->end_bar && it->start_tick >= it->end_tick)) {
        it = spans.erase(it);
        continue;
      }
      ++it;
    }
  };
  clamp_spans(score.slurs);
  clamp_spans(score.pedals);
  clamp_spans(score.ottavas);

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
  if (ctx.skipped_micro_durations > 0U) {
    result.warnings.push_back(
        std::format("Skipped {} micro-duration note(s) (< 64th)", ctx.skipped_micro_durations));
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

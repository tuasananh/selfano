#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace selfano::core {

namespace {

using json = nlohmann::json;

[[nodiscard]] std::string_view duration_to_string(NoteBaseDuration base) noexcept {
  switch (base) {
  case NoteBaseDuration::ThirtySecond:
    return "32nd";
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
  if (s == "32nd" || s == "thirty-second") {
    return NoteBaseDuration::ThirtySecond;
  }
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
  const std::uint8_t dots = ev.duration.effective_dots();
  json ej{
      {"start_tick", ev.start_tick}, {"dur", duration_to_string(ev.duration.base)},
      {"dotted", dots > 0U},         {"is_rest", ev.is_rest},
      {"voice", ev.voice},           {"notes", std::move(notes_arr)},
  };
  if (dots >= 2U) {
    ej["dots"] = dots;
  }
  if (ev.tuplet.has_value()) {
    ej["tuplet"] = {
        {"actual", ev.tuplet->actual},
        {"normal", ev.tuplet->normal},
        {"group_id", ev.tuplet->group_id},
    };
  }
  if (ev.tick_duration_override > 0) {
    ej["tick_duration_override"] = ev.tick_duration_override;
  }
  if (ev.articulations != 0U) {
    json art = json::array();
    if (has_articulation(ev.articulations, ArticulationFlag::Staccato)) {
      art.push_back("staccato");
    }
    if (has_articulation(ev.articulations, ArticulationFlag::Tenuto)) {
      art.push_back("tenuto");
    }
    if (has_articulation(ev.articulations, ArticulationFlag::Accent)) {
      art.push_back("accent");
    }
    if (has_articulation(ev.articulations, ArticulationFlag::Arpeggiate)) {
      art.push_back("arpeggiate");
    }
    ej["articulations"] = std::move(art);
  }
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
  std::uint8_t shorthand_dots = 0U;
  while (!dur_str.empty() && dur_str.back() == '.' && shorthand_dots < 2U) {
    ++shorthand_dots;
    dur_str.pop_back();
  }
  const auto base_opt = string_to_duration(dur_str);
  if (!base_opt.has_value()) {
    return std::unexpected(std::format("Unknown rhythmic duration '{}'", dur_str));
  }
  ev.duration.base = *base_opt;
  ev.duration.dots = shorthand_dots;
  ev.duration.dotted = (shorthand_dots > 0U);

  if (obj.contains("dotted")) {
    if (!obj.at("dotted").is_boolean()) {
      return std::unexpected("Event 'dotted' must be a boolean");
    }
    const bool b = obj.at("dotted").get<bool>();
    if (b) {
      ev.duration.dotted = true;
      if (ev.duration.dots == 0U) {
        ev.duration.dots = 1U;
      }
    } else if (shorthand_dots == 0U) {
      ev.duration.dotted = false;
      ev.duration.dots = 0U;
    }
  }
  if (obj.contains("dots")) {
    if (!obj.at("dots").is_number_integer()) {
      return std::unexpected("Event 'dots' must be an integer");
    }
    const int d = obj.at("dots").get<int>();
    if (d < 0 || d > 2) {
      return std::unexpected("Event 'dots' must be in [0, 2]");
    }
    ev.duration.dots = static_cast<std::uint8_t>(d);
    ev.duration.dotted = (d > 0);
  }
  if (obj.contains("tuplet") && !obj.at("tuplet").is_null()) {
    const auto& tj = obj.at("tuplet");
    if (!tj.is_object() || !tj.contains("actual") || !tj.at("actual").is_number_integer() ||
        !tj.contains("normal") || !tj.at("normal").is_number_integer()) {
      return std::unexpected("Event 'tuplet' must be an object with 'actual' and 'normal'");
    }
    const int act = tj.at("actual").get<int>();
    const int norm = tj.at("normal").get<int>();
    const int gid = (tj.contains("group_id") && tj.at("group_id").is_number_integer())
                        ? tj.at("group_id").get<int>()
                        : 0;
    if (act < 2 || act > 16 || norm < 1 || norm > 16 || gid < 0 || gid > 65535) {
      return std::unexpected("Event 'tuplet' values out of range");
    }
    ev.tuplet = TupletRatio{
        .actual = static_cast<std::uint8_t>(act),
        .normal = static_cast<std::uint8_t>(norm),
        .group_id = static_cast<std::uint16_t>(gid),
    };
  }
  if (obj.contains("tick_duration_override")) {
    if (!obj.at("tick_duration_override").is_number_integer()) {
      return std::unexpected("Event 'tick_duration_override' must be an integer");
    }
    ev.tick_duration_override = std::max(0, obj.at("tick_duration_override").get<int>());
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
  if (obj.contains("articulations")) {
    if (!obj.at("articulations").is_array()) {
      return std::unexpected("Event 'articulations' must be an array");
    }
    for (const auto& a : obj.at("articulations")) {
      if (!a.is_string()) {
        return std::unexpected("Event articulation entry must be a string");
      }
      const auto s = a.get<std::string>();
      if (s == "staccato") {
        ev.articulations |= articulation_bit(ArticulationFlag::Staccato);
      } else if (s == "tenuto") {
        ev.articulations |= articulation_bit(ArticulationFlag::Tenuto);
      } else if (s == "accent") {
        ev.articulations |= articulation_bit(ArticulationFlag::Accent);
      } else if (s == "arpeggiate") {
        ev.articulations |= articulation_bit(ArticulationFlag::Arpeggiate);
      } else {
        return std::unexpected(std::format("Unknown articulation '{}'", s));
      }
    }
  }
  return ev;
}

} // namespace

std::string lesson_score_to_json(const LessonScore& score) {
  json j;
  json theory_json{
      {"summary", score.meta.theory.summary},
      {"pattern_tip", score.meta.theory.pattern_tip},
      {"technique_tip", score.meta.theory.technique_tip},
      {"ballade_link", score.meta.theory.ballade_link},
  };
  if (!score.meta.theory.concept_title.empty()) {
    theory_json["concept_title"] = score.meta.theory.concept_title;
  }
  if (!score.meta.theory.concept_paragraphs.empty()) {
    theory_json["concept_paragraphs"] = score.meta.theory.concept_paragraphs;
  }
  if (!score.meta.theory.form_map.empty()) {
    json fm_arr = json::array();
    for (const auto& fn : score.meta.theory.form_map) {
      fm_arr.push_back({{"phrase_label", fn.phrase_label}, {"role", fn.role}});
    }
    theory_json["form_map"] = std::move(fm_arr);
  }
  if (!score.meta.theory.practice_steps.empty()) {
    theory_json["practice_steps"] = score.meta.theory.practice_steps;
  }
  if (!score.meta.theory.audio_examples.empty()) {
    json ex_arr = json::array();
    for (const auto& ex : score.meta.theory.audio_examples) {
      ex_arr.push_back({{"label", ex.label}, {"caption", ex.caption}, {"chords", ex.chords}});
    }
    theory_json["audio_examples"] = std::move(ex_arr);
  }

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
      {"theory", std::move(theory_json)},
      {"is_imported", score.meta.is_imported},
      {"source_filename", score.meta.source_filename},
  };
  if (!score.meta.tempo_marking.empty()) {
    j["meta"]["tempo_marking"] = score.meta.tempo_marking;
  }

  json measures_arr = json::array();
  for (const auto& m : score.measures) {
    json mj;
    mj["bar_number"] = m.bar_number;
    if (m.pickup_ticks.has_value()) {
      mj["pickup_ticks"] = *m.pickup_ticks;
    }
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
    if (!m.dynamics.empty()) {
      json dyn_arr = json::array();
      for (const auto& d : m.dynamics) {
        dyn_arr.push_back({
            {"start_tick", d.start_tick},
            {"level", dynamic_level_to_string(d.level)},
        });
      }
      mj["dynamics"] = std::move(dyn_arr);
    }
    if (!m.hairpins.empty()) {
      json hp_arr = json::array();
      for (const auto& hp : m.hairpins) {
        hp_arr.push_back({
            {"start_tick", hp.start_tick},
            {"end_tick", hp.end_tick},
            {"kind", hp.kind == HairpinKind::Crescendo ? "crescendo" : "diminuendo"},
        });
      }
      mj["hairpins"] = std::move(hp_arr);
    }
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

  if (!score.slurs.empty()) {
    json slur_arr = json::array();
    for (const auto& sl : score.slurs) {
      slur_arr.push_back({
          {"start_bar", sl.start_bar},
          {"start_tick", sl.start_tick},
          {"end_bar", sl.end_bar},
          {"end_tick", sl.end_tick},
          {"treble", sl.treble},
      });
    }
    j["slurs"] = std::move(slur_arr);
  }
  if (!score.pedals.empty()) {
    json ped_arr = json::array();
    for (const auto& pd : score.pedals) {
      ped_arr.push_back({
          {"start_bar", pd.start_bar},
          {"start_tick", pd.start_tick},
          {"end_bar", pd.end_bar},
          {"end_tick", pd.end_tick},
      });
    }
    j["pedals"] = std::move(ped_arr);
  }

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
  if (meta.contains("tempo_marking")) {
    if (!meta.at("tempo_marking").is_string()) {
      return std::unexpected("LessonMetadata 'tempo_marking' must be a string");
    }
    out.meta.tempo_marking = meta.at("tempo_marking").get<std::string>();
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
    if (th.contains("concept_title")) {
      if (!th.at("concept_title").is_string()) {
        return std::unexpected("Theory 'concept_title' must be a string");
      }
      out.meta.theory.concept_title = th.at("concept_title").get<std::string>();
    }
    if (th.contains("concept_paragraphs")) {
      if (!th.at("concept_paragraphs").is_array()) {
        return std::unexpected("Theory 'concept_paragraphs' must be an array");
      }
      for (const auto& p : th.at("concept_paragraphs")) {
        if (!p.is_string()) {
          return std::unexpected("Theory 'concept_paragraphs' entry must be a string");
        }
        out.meta.theory.concept_paragraphs.push_back(p.get<std::string>());
      }
    }
    if (th.contains("form_map")) {
      if (!th.at("form_map").is_array()) {
        return std::unexpected("Theory 'form_map' must be an array");
      }
      for (const auto& fj : th.at("form_map")) {
        if (!fj.is_object()) {
          return std::unexpected("Theory 'form_map' entry must be an object");
        }
        PhraseFormNote fn{};
        if (fj.contains("phrase_label")) {
          if (!fj.at("phrase_label").is_string()) {
            return std::unexpected("Theory form_map 'phrase_label' must be a string");
          }
          fn.phrase_label = fj.at("phrase_label").get<std::string>();
        }
        if (fj.contains("role")) {
          if (!fj.at("role").is_string()) {
            return std::unexpected("Theory form_map 'role' must be a string");
          }
          fn.role = fj.at("role").get<std::string>();
        }
        out.meta.theory.form_map.push_back(std::move(fn));
      }
    }
    if (th.contains("practice_steps")) {
      if (!th.at("practice_steps").is_array()) {
        return std::unexpected("Theory 'practice_steps' must be an array");
      }
      for (const auto& st : th.at("practice_steps")) {
        if (!st.is_string()) {
          return std::unexpected("Theory 'practice_steps' entry must be a string");
        }
        out.meta.theory.practice_steps.push_back(st.get<std::string>());
      }
    }
    if (th.contains("audio_examples")) {
      if (!th.at("audio_examples").is_array()) {
        return std::unexpected("Theory 'audio_examples' must be an array");
      }
      for (const auto& ej : th.at("audio_examples")) {
        if (!ej.is_object()) {
          return std::unexpected("Theory 'audio_examples' entry must be an object");
        }
        TheoryAudioExample ex{};
        if (ej.contains("label")) {
          if (!ej.at("label").is_string()) {
            return std::unexpected("Theory audio_examples 'label' must be a string");
          }
          ex.label = ej.at("label").get<std::string>();
        }
        if (ej.contains("caption")) {
          if (!ej.at("caption").is_string()) {
            return std::unexpected("Theory audio_examples 'caption' must be a string");
          }
          ex.caption = ej.at("caption").get<std::string>();
        }
        if (ej.contains("chords")) {
          if (!ej.at("chords").is_array()) {
            return std::unexpected("Theory audio_examples 'chords' must be an array");
          }
          for (const auto& cj : ej.at("chords")) {
            if (!cj.is_string()) {
              return std::unexpected("Theory audio_examples 'chords' entry must be a string");
            }
            ex.chords.push_back(cj.get<std::string>());
          }
        }
        out.meta.theory.audio_examples.push_back(std::move(ex));
      }
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
    if (mj.contains("bar_number") && mj.at("bar_number").is_number_integer()) {
      m.bar_number = mj.at("bar_number").get<std::uint32_t>();
      fallback_bar = m.bar_number + 1U;
    } else {
      m.bar_number = fallback_bar;
      ++fallback_bar;
    }
    if (mj.contains("pickup_ticks") && !mj.at("pickup_ticks").is_null()) {
      if (!mj.at("pickup_ticks").is_number_integer()) {
        return std::unexpected("Measure 'pickup_ticks' must be an integer");
      }
      m.pickup_ticks = mj.at("pickup_ticks").get<int>();
    }

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
    if (mj.contains("dynamics")) {
      if (!mj.at("dynamics").is_array()) {
        return std::unexpected("Measure 'dynamics' must be an array");
      }
      for (const auto& dj : mj.at("dynamics")) {
        if (!dj.is_object() || !dj.contains("level") || !dj.at("level").is_string()) {
          return std::unexpected("Dynamic entry must be an object with string 'level'");
        }
        const auto lvl = parse_dynamic_level(dj.at("level").get<std::string>());
        if (!lvl.has_value()) {
          return std::unexpected("Unknown dynamic level");
        }
        DynamicEvent de{};
        if (dj.contains("start_tick") && dj.at("start_tick").is_number_integer()) {
          de.start_tick = dj.at("start_tick").get<int>();
        }
        de.level = *lvl;
        m.dynamics.push_back(de);
      }
    }
    if (mj.contains("hairpins")) {
      if (!mj.at("hairpins").is_array()) {
        return std::unexpected("Measure 'hairpins' must be an array");
      }
      for (const auto& hpj : mj.at("hairpins")) {
        if (!hpj.is_object() || !hpj.contains("kind") || !hpj.at("kind").is_string()) {
          return std::unexpected("Hairpin entry must be an object with string 'kind'");
        }
        HairpinSpan hp{};
        if (hpj.contains("start_tick") && hpj.at("start_tick").is_number_integer()) {
          hp.start_tick = hpj.at("start_tick").get<int>();
        }
        if (hpj.contains("end_tick") && hpj.at("end_tick").is_number_integer()) {
          hp.end_tick = hpj.at("end_tick").get<int>();
        }
        const auto ks = hpj.at("kind").get<std::string>();
        if (ks == "crescendo" || ks == "cresc") {
          hp.kind = HairpinKind::Crescendo;
        } else if (ks == "diminuendo" || ks == "dim" || ks == "decrescendo") {
          hp.kind = HairpinKind::Diminuendo;
        } else {
          return std::unexpected(std::format("Unknown hairpin kind '{}'", ks));
        }
        m.hairpins.push_back(hp);
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

  if (root.contains("slurs")) {
    if (!root.at("slurs").is_array()) {
      return std::unexpected("LessonScore 'slurs' must be an array");
    }
    for (const auto& sj : root.at("slurs")) {
      if (!sj.is_object()) {
        return std::unexpected("Slur entry must be an object");
      }
      SlurSpan sl{};
      if (sj.contains("start_bar") && sj.at("start_bar").is_number_unsigned()) {
        sl.start_bar = sj.at("start_bar").get<std::size_t>();
      }
      if (sj.contains("start_tick") && sj.at("start_tick").is_number_integer()) {
        sl.start_tick = sj.at("start_tick").get<int>();
      }
      if (sj.contains("end_bar") && sj.at("end_bar").is_number_unsigned()) {
        sl.end_bar = sj.at("end_bar").get<std::size_t>();
      }
      if (sj.contains("end_tick") && sj.at("end_tick").is_number_integer()) {
        sl.end_tick = sj.at("end_tick").get<int>();
      }
      if (sj.contains("treble") && sj.at("treble").is_boolean()) {
        sl.treble = sj.at("treble").get<bool>();
      }
      out.slurs.push_back(sl);
    }
  }
  if (root.contains("pedals")) {
    if (!root.at("pedals").is_array()) {
      return std::unexpected("LessonScore 'pedals' must be an array");
    }
    for (const auto& pj : root.at("pedals")) {
      if (!pj.is_object()) {
        return std::unexpected("Pedal entry must be an object");
      }
      PedalSpan pd{};
      if (pj.contains("start_bar") && pj.at("start_bar").is_number_unsigned()) {
        pd.start_bar = pj.at("start_bar").get<std::size_t>();
      }
      if (pj.contains("start_tick") && pj.at("start_tick").is_number_integer()) {
        pd.start_tick = pj.at("start_tick").get<int>();
      }
      if (pj.contains("end_bar") && pj.at("end_bar").is_number_unsigned()) {
        pd.end_bar = pj.at("end_bar").get<std::size_t>();
      }
      if (pj.contains("end_tick") && pj.at("end_tick").is_number_integer()) {
        pd.end_tick = pj.at("end_tick").get<int>();
      }
      out.pedals.push_back(pd);
    }
  }

  return out;
}

} // namespace selfano::core

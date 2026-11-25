#include "core/lesson_score.hpp"

#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace selfano::core {

namespace {

using json = nlohmann::json;

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
  return json{
      {"start_tick", ev.start_tick},
      {"dur", duration_to_string(ev.duration.base)},
      {"dotted", ev.duration.dotted},
      {"is_rest", ev.is_rest},
      {"voice", ev.voice},
      {"notes", std::move(notes_arr)},
  };
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

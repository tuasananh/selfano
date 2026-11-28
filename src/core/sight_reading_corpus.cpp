#include "core/sight_reading_corpus.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

using json = nlohmann::json;

[[nodiscard]] NoteBaseDuration parse_dur_str(std::string_view s) noexcept {
  if (s == "32nd") {
    return NoteBaseDuration::ThirtySecond;
  }
  if (s == "16th") {
    return NoteBaseDuration::Sixteenth;
  }
  if (s == "8th") {
    return NoteBaseDuration::Eighth;
  }
  if (s == "half") {
    return NoteBaseDuration::Half;
  }
  if (s == "whole") {
    return NoteBaseDuration::Whole;
  }
  return NoteBaseDuration::Quarter;
}

[[nodiscard]] std::expected<std::vector<LessonEvent>, std::string>
parse_staff_events(const json& arr) {
  if (!arr.is_array()) {
    return std::unexpected("Sight reading events must be an array");
  }
  std::vector<LessonEvent> out;
  out.reserve(arr.size());
  for (const auto& ej : arr) {
    if (!ej.is_object()) {
      return std::unexpected("Sight reading event must be an object");
    }
    LessonEvent ev{};
    ev.start_tick = ej.value("start_tick", 0);
    ev.duration.base = parse_dur_str(ej.value("dur", "quarter"));
    ev.duration.dotted = ej.value("dotted", false);
    ev.duration.dots = ev.duration.dotted ? 1U : static_cast<std::uint8_t>(ej.value("dots", 0));
    ev.is_rest = ej.value("is_rest", false);
    if (ej.contains("notes") && ej.at("notes").is_array()) {
      for (const auto& nj : ej.at("notes")) {
        LessonNote ln{};
        if (nj.is_string()) {
          const auto sp = parse_spelled_note(nj.get<std::string>());
          if (!sp.has_value()) {
            return std::unexpected(sp.error());
          }
          const auto m = to_midi(*sp);
          if (!m.has_value()) {
            return std::unexpected(std::string(m.error()));
          }
          ln.spelled = *sp;
          ln.midi = *m;
        } else if (nj.is_object()) {
          const auto sp = parse_spelled_note(nj.value("spell", "C4"));
          if (!sp.has_value()) {
            return std::unexpected(sp.error());
          }
          const auto m = to_midi(*sp);
          if (!m.has_value()) {
            return std::unexpected(std::string(m.error()));
          }
          ln.spelled = *sp;
          ln.midi = nj.contains("midi") ? static_cast<MidiPitch>(nj.at("midi").get<int>()) : *m;
          ln.tie_start = nj.value("tie_start", false);
          ln.tie_stop = nj.value("tie_stop", false);
        }
        ev.notes.push_back(ln);
      }
    }
    out.push_back(std::move(ev));
  }
  return out;
}

} // namespace

std::expected<SightReadingCorpus, std::string>
parse_sight_reading_corpus_json(std::string_view json_text) {
  const json root = json::parse(json_text, nullptr, false);
  if (root.is_discarded() || !root.is_object() || !root.contains("excerpts") ||
      !root.at("excerpts").is_array()) {
    return std::unexpected("Sight reading corpus JSON must contain an 'excerpts' array");
  }

  SightReadingCorpus corpus{};
  for (const auto& item : root.at("excerpts")) {
    if (!item.is_object()) {
      return std::unexpected("Corpus excerpt must be an object");
    }
    SightReadingExcerpt ex{};
    ex.id = item.value("id", "");
    ex.title = item.value("title", "");
    ex.composer = item.value("composer", "");
    ex.level = std::clamp(item.value("level", 1), 1, 5);
    if (item.contains("key") && item.at("key").is_object()) {
      const auto& kj = item.at("key");
      const int fifths = kj.value("fifths", 0);
      const std::string mode_s = kj.value("mode", "major");
      const Mode mode = (mode_s == "minor" || mode_s == "Minor") ? Mode::Minor : Mode::Major;
      const auto ks = make_key_signature(fifths, mode);
      if (!ks.has_value()) {
        return std::unexpected(std::string(ks.error()));
      }
      ex.key = *ks;
    }
    if (item.contains("time_signature") && item.at("time_signature").is_object()) {
      const auto& ts = item.at("time_signature");
      ex.time_signature.numerator = static_cast<std::uint8_t>(ts.value("numerator", 4));
      ex.time_signature.denominator = static_cast<std::uint8_t>(ts.value("denominator", 4));
    }
    if (!item.contains("measures") || !item.at("measures").is_array() ||
        item.at("measures").size() != 4U) {
      return std::unexpected(std::format("Excerpt '{}' must have exactly 4 measures", ex.id));
    }
    for (std::size_t b = 0; b < 4U; ++b) {
      const auto& mj = item.at("measures")[b];
      LessonMeasure lm{};
      lm.bar_number = static_cast<std::uint32_t>(mj.value("bar_number", static_cast<int>(b + 1U)));
      if (mj.contains("treble")) {
        const auto tr = parse_staff_events(mj.at("treble"));
        if (!tr.has_value()) {
          return std::unexpected(tr.error());
        }
        lm.treble_events = *tr;
      }
      if (mj.contains("bass")) {
        const auto ba = parse_staff_events(mj.at("bass"));
        if (!ba.has_value()) {
          return std::unexpected(ba.error());
        }
        lm.bass_events = *ba;
      }
      ex.measures[b] = std::move(lm);
    }
    corpus.excerpts.push_back(std::move(ex));
  }
  return corpus;
}

std::expected<SightReadingCorpus, std::string>
load_sight_reading_corpus(const std::filesystem::path& json_path) {
  std::ifstream ifs(json_path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    return std::unexpected(
        std::format("Unable to open sight-reading corpus: {}", json_path.string()));
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  if (ifs.bad()) {
    return std::unexpected(
        std::format("Failed reading sight-reading corpus: {}", json_path.string()));
  }
  return parse_sight_reading_corpus_json(ss.str());
}

SightReadingExcerpt transpose_excerpt_to_key(const SightReadingExcerpt& excerpt,
                                             KeySignature target_key) {
  if (excerpt.key == target_key) {
    return excerpt;
  }
  const auto src_t = tonic_of(excerpt.key);
  const auto dst_t = tonic_of(target_key);
  const int src_pc = static_cast<int>(pitch_class(SpelledNote{src_t.letter, src_t.accidental, 4}));
  const int dst_pc = static_cast<int>(pitch_class(SpelledNote{dst_t.letter, dst_t.accidental, 4}));
  int semitone_delta = (dst_pc - src_pc) % 12;
  if (semitone_delta > 6) {
    semitone_delta -= 12;
  } else if (semitone_delta < -6) {
    semitone_delta += 12;
  }

  SightReadingExcerpt out = excerpt;
  out.key = target_key;
  for (auto& m : out.measures) {
    auto transpose_events = [&](std::vector<LessonEvent>& evs, bool is_treble) {
      for (auto& ev : evs) {
        for (auto& ln : ev.notes) {
          int shifted = static_cast<int>(ln.midi) + semitone_delta;
          const int min_m = is_treble ? 57 : 36;
          const int max_m = is_treble ? 84 : 62;
          while (shifted < min_m) {
            shifted += 12;
          }
          while (shifted > max_m) {
            shifted -= 12;
          }
          const auto m_clamped = static_cast<MidiPitch>(std::clamp(
              shifted, static_cast<int>(kPianoLowestMidi), static_cast<int>(kPianoHighestMidi)));
          ln.midi = m_clamped;
          ln.spelled = spell_midi_in_key(m_clamped, target_key).value_or(ln.spelled);
        }
      }
    };
    transpose_events(m.treble_events, true);
    transpose_events(m.bass_events, false);
  }
  return out;
}

} // namespace selfano::core

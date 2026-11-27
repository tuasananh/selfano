#include "core/harmonic_analyzer.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

enum class CandidateQuality : std::uint8_t {
  Major = 0,
  Minor,
  Dominant7,
  Minor7,
  Major7,
  Diminished,
  Diminished7,
  HalfDiminished7,
};

enum class HarmonyRole : std::uint8_t {
  Tonic = 0,
  Predominant,
  Dominant,
  SecondaryDominant,
  Neapolitan,
  Other,
};

struct QualitySpec {
  CandidateQuality quality{CandidateQuality::Major};
  const char* suffix{""};
  std::array<int, 4> intervals{0, 4, 7, -1};
  int tone_count{3};
  bool is_seventh{false};
};

constexpr std::array<QualitySpec, 8> kQualities{{
    {CandidateQuality::Major, "", {0, 4, 7, -1}, 3, false},
    {CandidateQuality::Minor, "m", {0, 3, 7, -1}, 3, false},
    {CandidateQuality::Dominant7, "7", {0, 4, 7, 10}, 4, true},
    {CandidateQuality::Minor7, "m7", {0, 3, 7, 10}, 4, true},
    {CandidateQuality::Major7, "maj7", {0, 4, 7, 11}, 4, true},
    {CandidateQuality::Diminished, "dim", {0, 3, 6, -1}, 3, false},
    {CandidateQuality::Diminished7, "dim7", {0, 3, 6, 9}, 4, true},
    {CandidateQuality::HalfDiminished7, "m7b5", {0, 3, 6, 10}, 4, true},
}};

struct WindowHistogram {
  std::size_t bar_index{0};
  int start_tick{0};
  int end_tick{0};
  std::array<double, 12> pc_weights{};
  double total_weight{0.0};
  std::optional<std::uint8_t> bass_pc{};
  KeySignature key{};
};

[[nodiscard]] WindowHistogram build_window_histogram(const LessonMeasure& m, std::size_t bar_index,
                                                     int win_start, int win_end, int beat_ticks,
                                                     KeySignature key) noexcept {
  WindowHistogram hist{};
  hist.bar_index = bar_index;
  hist.start_tick = win_start;
  hist.end_tick = win_end;
  hist.key = key;

  int best_bass_tick = std::numeric_limits<int>::max();
  MidiPitch best_bass_midi = 127;
  bool best_from_bass_staff = false;

  auto accumulate_staff = [&](std::span<const LessonEvent> evs, bool is_bass_staff) {
    for (const auto& ev : evs) {
      if (ev.is_rest || ev.is_grace) {
        continue;
      }
      const int ev_dur = std::max(1, ev.event_ticks());
      const int ev_start = ev.start_tick;
      const int ev_end = ev_start + ev_dur;
      const int ov_start = std::max(ev_start, win_start);
      const int ov_end = std::min(ev_end, win_end);
      if (ov_end <= ov_start) {
        continue;
      }
      const double ov_dur = static_cast<double>(ov_end - ov_start);
      const double dur_w = std::clamp(ov_dur / 480.0, 0.20, 4.0);
      double beat_w = 0.85;
      if (ev_start == win_start) {
        beat_w = 1.65;
      } else if (beat_ticks > 0 && (ev_start % beat_ticks) == 0) {
        beat_w = 1.25;
      } else if (ev_dur <= 240) {
        beat_w = 0.50;
      }

      for (const auto& ln : ev.notes) {
        const std::uint8_t pc = pitch_class(ln.midi);
        const double reg_w = (is_bass_staff || ln.midi < 60) ? 1.75 : 1.0;
        const double w = dur_w * beat_w * reg_w;
        hist.pc_weights[pc] += w;
        hist.total_weight += w;

        const bool starts_in_win = (ev_start >= win_start && ev_start < win_end);
        const int cand_tick = starts_in_win ? ev_start : win_start;
        const bool better_staff = (is_bass_staff && !best_from_bass_staff);
        const bool same_staff_earlier = (is_bass_staff == best_from_bass_staff) &&
                                        (cand_tick < best_bass_tick ||
                                         (cand_tick == best_bass_tick && ln.midi < best_bass_midi));
        if (!hist.bass_pc.has_value() || better_staff || same_staff_earlier) {
          best_from_bass_staff = is_bass_staff;
          best_bass_tick = cand_tick;
          best_bass_midi = ln.midi;
          hist.bass_pc = pc;
        }
      }
    }
  };

  accumulate_staff(m.bass_events, true);
  accumulate_staff(m.treble_events, false);
  return hist;
}

struct ChordCandidate {
  std::uint8_t root_pc{0};
  std::size_t qual_idx{0};
  std::string roman_base{};
  HarmonyRole role{HarmonyRole::Other};
  bool diatonic{false};
  double key_prior{0.0};
  double emission_score{0.0};
};

struct RomanTag {
  std::string roman_base{};
  HarmonyRole role{HarmonyRole::Other};
  bool diatonic{false};
  double prior{0.0};
};

[[nodiscard]] constexpr int key_tonic_pc(KeySignature key) noexcept {
  const int base = (key.mode == Mode::Major) ? 0 : 9;
  return ((base + key.fifths * 7) % 12 + 12) % 12;
}

[[nodiscard]] RomanTag classify_candidate_in_key(std::uint8_t root_pc, CandidateQuality q,
                                                 KeySignature key) {
  const int tonic_pc = key_tonic_pc(key);
  const int rel = (static_cast<int>(root_pc) - tonic_pc + 12) % 12;
  const bool minor = (key.mode == Mode::Minor);

  if (!minor) {
    // Major key diatonic + common chromatic
    switch (rel) {
    case 0:
      if (q == CandidateQuality::Major || q == CandidateQuality::Major7) {
        return {q == CandidateQuality::Major7 ? "Imaj7" : "I", HarmonyRole::Tonic, true, 0.34};
      }
      if (q == CandidateQuality::Dominant7) {
        return {"V7/IV", HarmonyRole::SecondaryDominant, false, 0.12};
      }
      break;
    case 1:
      if (q == CandidateQuality::Major) {
        return {"bII", HarmonyRole::Neapolitan, false, 0.14};
      }
      break;
    case 2:
      if (q == CandidateQuality::Minor || q == CandidateQuality::Minor7) {
        return {q == CandidateQuality::Minor7 ? "ii7" : "ii", HarmonyRole::Predominant, true, 0.28};
      }
      if (q == CandidateQuality::Dominant7 || q == CandidateQuality::Major) {
        return {q == CandidateQuality::Dominant7 ? "V7/V" : "V/V", HarmonyRole::SecondaryDominant,
                false, 0.18};
      }
      break;
    case 4:
      if (q == CandidateQuality::Minor || q == CandidateQuality::Minor7) {
        return {q == CandidateQuality::Minor7 ? "iii7" : "iii", HarmonyRole::Tonic, true, 0.22};
      }
      if (q == CandidateQuality::Dominant7 || q == CandidateQuality::Major) {
        return {q == CandidateQuality::Dominant7 ? "V7/vi" : "V/vi", HarmonyRole::SecondaryDominant,
                false, 0.16};
      }
      break;
    case 5:
      if (q == CandidateQuality::Major || q == CandidateQuality::Major7) {
        return {q == CandidateQuality::Major7 ? "IVmaj7" : "IV", HarmonyRole::Predominant, true,
                0.30};
      }
      if (q == CandidateQuality::Minor) {
        return {"iv", HarmonyRole::Predominant, false, 0.12};
      }
      break;
    case 6:
      if (q == CandidateQuality::Diminished7) {
        return {"viio7/V", HarmonyRole::SecondaryDominant, false, 0.14};
      }
      break;
    case 7:
      if (q == CandidateQuality::Major || q == CandidateQuality::Dominant7) {
        return {q == CandidateQuality::Dominant7 ? "V7" : "V", HarmonyRole::Dominant, true, 0.34};
      }
      break;
    case 9:
      if (q == CandidateQuality::Minor || q == CandidateQuality::Minor7) {
        return {q == CandidateQuality::Minor7 ? "vi7" : "vi", HarmonyRole::Tonic, true, 0.26};
      }
      if (q == CandidateQuality::Dominant7 || q == CandidateQuality::Major) {
        return {q == CandidateQuality::Dominant7 ? "V7/ii" : "V/ii", HarmonyRole::SecondaryDominant,
                false, 0.15};
      }
      break;
    case 10:
      if (q == CandidateQuality::Major) {
        return {"bVII", HarmonyRole::Other, false, 0.08};
      }
      break;
    case 11:
      if (q == CandidateQuality::Diminished || q == CandidateQuality::HalfDiminished7 ||
          q == CandidateQuality::Diminished7) {
        return {q == CandidateQuality::Diminished ? "viio" : "viio7", HarmonyRole::Dominant, true,
                0.22};
      }
      break;
    default:
      break;
    }
  } else {
    // Minor key diatonic + harmonic/melodic minor + Neapolitan + secondary dominants
    switch (rel) {
    case 0:
      if (q == CandidateQuality::Minor || q == CandidateQuality::Minor7) {
        return {q == CandidateQuality::Minor7 ? "i7" : "i", HarmonyRole::Tonic, true, 0.34};
      }
      if (q == CandidateQuality::Major || q == CandidateQuality::Dominant7) {
        return {q == CandidateQuality::Dominant7 ? "V7/iv" : "V/iv", HarmonyRole::SecondaryDominant,
                false, 0.14};
      }
      break;
    case 1:
      if (q == CandidateQuality::Major) {
        return {"bII", HarmonyRole::Neapolitan, false, 0.22};
      }
      break;
    case 2:
      if (q == CandidateQuality::Diminished || q == CandidateQuality::HalfDiminished7) {
        return {q == CandidateQuality::Diminished ? "iio" : "iio7", HarmonyRole::Predominant, true,
                0.26};
      }
      if (q == CandidateQuality::Minor) {
        return {"ii", HarmonyRole::Predominant, false, 0.14};
      }
      if (q == CandidateQuality::Dominant7 || q == CandidateQuality::Major) {
        return {q == CandidateQuality::Dominant7 ? "V7/V" : "V/V", HarmonyRole::SecondaryDominant,
                false, 0.18};
      }
      break;
    case 3:
      if (q == CandidateQuality::Major || q == CandidateQuality::Major7) {
        return {q == CandidateQuality::Major7 ? "IIImaj7" : "III", HarmonyRole::Tonic, true, 0.26};
      }
      if (q == CandidateQuality::Dominant7) {
        return {"V7/VI", HarmonyRole::SecondaryDominant, false, 0.16};
      }
      break;
    case 5:
      if (q == CandidateQuality::Minor || q == CandidateQuality::Minor7) {
        return {q == CandidateQuality::Minor7 ? "iv7" : "iv", HarmonyRole::Predominant, true, 0.30};
      }
      if (q == CandidateQuality::Major || q == CandidateQuality::Dominant7) {
        return {"IV", HarmonyRole::Predominant, false, 0.10};
      }
      break;
    case 6:
      if (q == CandidateQuality::Diminished7) {
        return {"viio7/V", HarmonyRole::SecondaryDominant, false, 0.15};
      }
      break;
    case 7:
      if (q == CandidateQuality::Major || q == CandidateQuality::Dominant7) {
        return {q == CandidateQuality::Dominant7 ? "V7" : "V", HarmonyRole::Dominant, true, 0.34};
      }
      if (q == CandidateQuality::Minor) {
        return {"v", HarmonyRole::Dominant, false, 0.10};
      }
      break;
    case 8:
      if (q == CandidateQuality::Major || q == CandidateQuality::Major7) {
        return {q == CandidateQuality::Major7 ? "VImaj7" : "VI", HarmonyRole::Tonic, true, 0.26};
      }
      break;
    case 10:
      if (q == CandidateQuality::Major || q == CandidateQuality::Dominant7) {
        return {q == CandidateQuality::Dominant7 ? "VII7" : "VII", HarmonyRole::Other, true, 0.20};
      }
      if (q == CandidateQuality::Dominant7) {
        return {"V7/III", HarmonyRole::SecondaryDominant, false, 0.16};
      }
      break;
    case 11:
      if (q == CandidateQuality::Diminished || q == CandidateQuality::Diminished7) {
        return {q == CandidateQuality::Diminished ? "viio" : "viio7", HarmonyRole::Dominant, true,
                0.24};
      }
      break;
    default:
      break;
    }
  }

  return {"?", HarmonyRole::Other, false, -0.15};
}

[[nodiscard]] double score_candidate(const WindowHistogram& hist, std::uint8_t root_pc,
                                     const QualitySpec& qs, const RomanTag& tag) noexcept {
  if (hist.total_weight <= 1e-9) {
    return 0.0;
  }
  const double inv_total = 1.0 / hist.total_weight;
  std::array<bool, 12> in_chord{};
  static constexpr std::array<double, 4> kToneWeights{1.15, 1.10, 0.90, 1.05};

  double in_score = 0.0;
  for (int i = 0; i < qs.tone_count; ++i) {
    const std::uint8_t pc = static_cast<std::uint8_t>((root_pc + qs.intervals[i]) % 12);
    in_chord[pc] = true;
    in_score += hist.pc_weights[pc] * inv_total * kToneWeights[i];
  }

  double out_weight = 0.0;
  for (std::size_t pc = 0; pc < 12U; ++pc) {
    if (!in_chord[pc]) {
      out_weight += hist.pc_weights[pc] * inv_total;
    }
  }

  double penalty = 0.0;
  const std::uint8_t third_pc = static_cast<std::uint8_t>((root_pc + qs.intervals[1]) % 12);
  if (hist.pc_weights[third_pc] * inv_total < 0.04) {
    penalty += 0.14;
  }
  if (qs.is_seventh) {
    const std::uint8_t seventh_pc = static_cast<std::uint8_t>((root_pc + qs.intervals[3]) % 12);
    if (hist.pc_weights[seventh_pc] * inv_total < 0.06) {
      penalty += 0.22;
    }
  }

  double bass_bonus = 0.0;
  if (hist.bass_pc.has_value()) {
    const int bpc = static_cast<int>(*hist.bass_pc);
    const int fifth_pc = (static_cast<int>(root_pc) + qs.intervals[2]) % 12;
    const int seventh_pc =
        qs.is_seventh ? ((static_cast<int>(root_pc) + qs.intervals[3]) % 12) : -1;
    if (bpc == static_cast<int>(root_pc)) {
      bass_bonus = 0.42;
    } else if (bpc == static_cast<int>(third_pc)) {
      bass_bonus = (tag.role == HarmonyRole::Neapolitan) ? 0.38 : 0.26;
    } else if (bpc == fifth_pc) {
      bass_bonus = 0.16;
    } else if (bpc == seventh_pc) {
      bass_bonus = 0.12;
    } else {
      bass_bonus = -0.15;
    }
  }

  return in_score - 0.90 * out_weight - penalty + bass_bonus + tag.prior;
}

[[nodiscard]] std::vector<ChordCandidate> build_candidates(const WindowHistogram& hist) {
  std::vector<ChordCandidate> out;
  out.reserve(kQualities.size() * 12U);
  for (std::uint8_t root = 0; root < 12U; ++root) {
    for (std::size_t qi = 0; qi < kQualities.size(); ++qi) {
      const auto& qs = kQualities[qi];
      auto tag = classify_candidate_in_key(root, qs.quality, hist.key);
      const double sc = score_candidate(hist, root, qs, tag);
      out.push_back(ChordCandidate{
          .root_pc = root,
          .qual_idx = qi,
          .roman_base = std::move(tag.roman_base),
          .role = tag.role,
          .diatonic = tag.diatonic,
          .key_prior = tag.prior,
          .emission_score = sc,
      });
    }
  }
  return out;
}

[[nodiscard]] double transition_score(const ChordCandidate& prev,
                                      const ChordCandidate& cur) noexcept {
  double bonus = 0.0;
  const int down_fifth = (static_cast<int>(prev.root_pc) - static_cast<int>(cur.root_pc) + 12) % 12;
  const auto pq = kQualities[prev.qual_idx].quality;
  if (down_fifth == 7 && (pq == CandidateQuality::Major || pq == CandidateQuality::Dominant7)) {
    bonus += 0.28;
  }
  if ((prev.role == HarmonyRole::Predominant || prev.role == HarmonyRole::Neapolitan) &&
      cur.role == HarmonyRole::Dominant) {
    bonus += 0.24;
  }
  if (prev.role == HarmonyRole::Dominant && cur.role == HarmonyRole::Tonic) {
    bonus += 0.30;
  }
  if (prev.role == HarmonyRole::Tonic &&
      (cur.role == HarmonyRole::Predominant || cur.role == HarmonyRole::Dominant)) {
    bonus += 0.10;
  }
  if (prev.root_pc == cur.root_pc && prev.qual_idx == cur.qual_idx) {
    bonus += 0.06;
  }
  return bonus;
}

[[nodiscard]] std::string spell_pc_name(std::uint8_t pc, KeySignature key,
                                        bool force_flats) noexcept {
  static constexpr std::array<char, 7> kLetters{'C', 'D', 'E', 'F', 'G', 'A', 'B'};
  const auto sp_exp =
      spell_midi_in_key(static_cast<MidiPitch>(60 + (pc % 12U)),
                        force_flats ? KeySignature{.fifths = -4, .mode = key.mode} : key);
  const SpelledNote sp =
      sp_exp.value_or(SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Natural});
  std::string out(1, kLetters[static_cast<std::size_t>(sp.letter)]);
  if (sp.accidental == Accidental::Sharp) {
    out.push_back('#');
  } else if (sp.accidental == Accidental::Flat) {
    out.push_back('b');
  }
  return out;
}

[[nodiscard]] std::string format_roman_with_inversion(const ChordCandidate& cand,
                                                      std::optional<std::uint8_t> bass_pc,
                                                      bool include_inversions) {
  std::string r = cand.roman_base;
  if (r.empty() || r == "?") {
    return "I";
  }
  if (!include_inversions || !bass_pc.has_value() || *bass_pc == cand.root_pc) {
    return r;
  }
  // Don't append inversion figures onto secondary slash numerals (e.g., V7/V)
  if (r.find('/') != std::string::npos) {
    return r;
  }
  const auto& qs = kQualities[cand.qual_idx];
  const int bpc = static_cast<int>(*bass_pc);
  const int third = (static_cast<int>(cand.root_pc) + qs.intervals[1]) % 12;
  const int fifth = (static_cast<int>(cand.root_pc) + qs.intervals[2]) % 12;
  const int seventh =
      qs.is_seventh ? ((static_cast<int>(cand.root_pc) + qs.intervals[3]) % 12) : -1;

  if (!qs.is_seventh) {
    if (bpc == third) {
      return r + "6";
    }
    if (bpc == fifth) {
      return r + "6/4";
    }
    return r;
  }
  // Seventh chords: strip trailing '7' before appending 6/5, 4/3, 4/2
  if (r.back() == '7') {
    r.pop_back();
  }
  if (bpc == third) {
    return r + "6/5";
  }
  if (bpc == fifth) {
    return r + "4/3";
  }
  if (bpc == seventh) {
    return r + "4/2";
  }
  return cand.roman_base;
}

[[nodiscard]] HarmonicAnnotation format_annotation(const WindowHistogram& hist,
                                                   const ChordCandidate& cand,
                                                   bool include_inversions) {
  const auto& qs = kQualities[cand.qual_idx];
  const bool force_flats =
      (cand.role == HarmonyRole::Neapolitan) || (cand.roman_base.starts_with('b'));
  const std::string root_name = spell_pc_name(cand.root_pc, hist.key, force_flats);
  std::string chord = root_name + qs.suffix;

  if (include_inversions && hist.bass_pc.has_value() && *hist.bass_pc != cand.root_pc) {
    const int bpc = static_cast<int>(*hist.bass_pc);
    const int third = (static_cast<int>(cand.root_pc) + qs.intervals[1]) % 12;
    const int fifth = (static_cast<int>(cand.root_pc) + qs.intervals[2]) % 12;
    const int seventh =
        qs.is_seventh ? ((static_cast<int>(cand.root_pc) + qs.intervals[3]) % 12) : -1;
    if (bpc == third || bpc == fifth || bpc == seventh) {
      const std::string bass_name = spell_pc_name(*hist.bass_pc, hist.key, force_flats);
      const std::string with_slash = chord + "/" + bass_name;
      if (parse_chord_symbol(with_slash).has_value()) {
        chord = with_slash;
      }
    }
  }
  if (!parse_chord_symbol(chord).has_value()) {
    chord = root_name;
  }

  std::string roman = format_roman_with_inversion(cand, hist.bass_pc, include_inversions);
  if (cand.roman_base == "?") {
    roman = (qs.quality == CandidateQuality::Minor || qs.quality == CandidateQuality::Minor7) ? "i"
                                                                                              : "I";
  }

  return HarmonicAnnotation{
      .start_tick = hist.start_tick,
      .chord_symbol = std::move(chord),
      .roman_numeral = std::move(roman),
  };
}

} // namespace

std::vector<std::vector<HarmonicAnnotation>>
analyze_score_harmony(const LessonScore& score, HarmonicAnalysisOptions options) {
  std::vector<std::vector<HarmonicAnnotation>> result(score.measures.size());
  if (score.measures.empty()) {
    return result;
  }

  std::vector<WindowHistogram> windows;
  windows.reserve(score.measures.size() * 2U);

  KeySignature active_key = score.meta.key;
  TimeSignature active_meter = score.meta.time_signature;

  for (std::size_t bi = 0; bi < score.measures.size(); ++bi) {
    const auto& m = score.measures[bi];
    if (m.key_change.has_value()) {
      active_key = *m.key_change;
    }
    if (m.meter_change.has_value()) {
      active_meter = *m.meter_change;
    }
    const int mticks = std::max(1, effective_measure_ticks(m, active_meter));
    const int beat_ticks = std::max(1, active_meter.beat_ticks());

    const auto full_hist = build_window_histogram(m, bi, 0, mticks, beat_ticks, active_key);
    if (full_hist.total_weight <= 1e-6) {
      continue;
    }

    if (options.allow_half_bar_splits && mticks >= 960) {
      const int half = mticks / 2;
      const auto h1 = build_window_histogram(m, bi, 0, half, beat_ticks, active_key);
      const auto h2 = build_window_histogram(m, bi, half, mticks, beat_ticks, active_key);
      if (h1.total_weight > 1e-6 && h2.total_weight > 1e-6) {
        const auto c_full = build_candidates(full_hist);
        const auto c1 = build_candidates(h1);
        const auto c2 = build_candidates(h2);
        auto best_of = [](const std::vector<ChordCandidate>& cands) -> const ChordCandidate& {
          return *std::ranges::max_element(cands,
                                           [](const ChordCandidate& a, const ChordCandidate& b) {
                                             return a.emission_score < b.emission_score;
                                           });
        };
        const auto& bf = best_of(c_full);
        const auto& b1 = best_of(c1);
        const auto& b2 = best_of(c2);
        if ((b1.root_pc != b2.root_pc || b1.qual_idx != b2.qual_idx) &&
            (0.5 * (b1.emission_score + b2.emission_score)) > bf.emission_score + 0.16) {
          windows.push_back(h1);
          windows.push_back(h2);
          continue;
        }
      }
    }

    windows.push_back(full_hist);
  }

  if (windows.empty()) {
    return result;
  }

  const std::size_t W = windows.size();
  std::vector<std::vector<ChordCandidate>> all_cands(W);
  for (std::size_t w = 0; w < W; ++w) {
    all_cands[w] = build_candidates(windows[w]);
  }

  const std::size_t C = all_cands[0].size();
  std::vector<std::vector<double>> dp(W, std::vector<double>(C, -1e9));
  std::vector<std::vector<std::size_t>> back(W, std::vector<std::size_t>(C, 0U));

  for (std::size_t c = 0; c < C; ++c) {
    dp[0][c] = all_cands[0][c].emission_score;
  }
  for (std::size_t w = 1; w < W; ++w) {
    for (std::size_t c = 0; c < C; ++c) {
      double best_val = -1e9;
      std::size_t best_prev = 0U;
      for (std::size_t p = 0; p < C; ++p) {
        const double v = dp[w - 1U][p] + transition_score(all_cands[w - 1U][p], all_cands[w][c]) +
                         all_cands[w][c].emission_score;
        if (v > best_val) {
          best_val = v;
          best_prev = p;
        }
      }
      dp[w][c] = best_val;
      back[w][c] = best_prev;
    }
  }

  std::vector<std::size_t> chosen(W, 0U);
  chosen[W - 1U] = static_cast<std::size_t>(
      std::distance(dp[W - 1U].begin(), std::ranges::max_element(dp[W - 1U])));
  for (std::size_t w = W - 1U; w-- > 0;) {
    chosen[w] = back[w + 1U][chosen[w + 1U]];
  }

  for (std::size_t w = 0; w < W; ++w) {
    const auto& hist = windows[w];
    const auto& cand = all_cands[w][chosen[w]];
    result[hist.bar_index].push_back(format_annotation(hist, cand, options.include_inversions));
  }

  return result;
}

} // namespace selfano::core

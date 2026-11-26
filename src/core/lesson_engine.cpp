#include "core/lesson_engine.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

[[nodiscard]] std::uint16_t midi_pitch_class_mask(std::span<const MidiPitch> notes) noexcept {
  std::uint16_t mask = 0U;
  for (const MidiPitch m : notes) {
    mask |= static_cast<std::uint16_t>(1U << (m % 12U));
  }
  return mask;
}

[[nodiscard]] std::vector<LessonNote> make_skeleton_lh(const ChordSymbol& chord, KeySignature key) {
  const std::uint8_t bass_pc = chord.bass_pc.value_or(chord.root_pc);
  int bass_midi = 36 + static_cast<int>(bass_pc); // C2..B2
  if (bass_midi < 40) {
    bass_midi += 12; // Keep comfortably in bass staff E2..Eb3
  }
  const auto m = static_cast<MidiPitch>(bass_midi);
  const auto sp = spell_midi_in_key(m, key);
  return {LessonNote{
      .spelled = sp.value_or(
          SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Natural, .octave = 3}),
      .midi = m,
      .finger = 5,
  }};
}

[[nodiscard]] std::vector<std::uint8_t> skeleton_pitch_classes(const ChordSymbol& chord) {
  std::vector<std::uint8_t> pcs;
  pcs.push_back(chord.root_pc);
  for (int offset = 1; offset < 12; ++offset) {
    const auto pc = static_cast<std::uint8_t>((chord.root_pc + offset) % 12U);
    if (chord.contains_pitch_class(pc)) {
      pcs.push_back(pc);
    }
  }
  if (pcs.size() > 4U) {
    const auto fifth_pc = static_cast<std::uint8_t>((chord.root_pc + 7U) % 12U);
    const auto it = std::ranges::find(pcs, fifth_pc);
    if (it != pcs.end() && it != pcs.begin()) {
      pcs.erase(it);
    }
  }
  while (pcs.size() > 4U) {
    pcs.erase(pcs.begin() + 2);
  }
  return pcs;
}

[[nodiscard]] std::vector<std::uint8_t>
assign_skeleton_rh_fingerings(std::span<const MidiPitch> pitches) {
  const std::size_t k = pitches.size();
  if (k == 0U) {
    return {};
  }
  if (k == 1U) {
    return {3U};
  }
  if (k == 2U) {
    const int span = static_cast<int>(pitches[1]) - static_cast<int>(pitches[0]);
    return {1U, static_cast<std::uint8_t>(span >= 7 ? 5 : (span >= 5 ? 4 : 3))};
  }
  if (k == 3U) {
    const int g0 = static_cast<int>(pitches[1]) - static_cast<int>(pitches[0]);
    const int g1 = static_cast<int>(pitches[2]) - static_cast<int>(pitches[1]);
    if (g0 <= 4 && g1 >= 5) {
      return {1U, 2U, 5U};
    }
    return {1U, 3U, 5U};
  }
  const int g0 = static_cast<int>(pitches[1]) - static_cast<int>(pitches[0]);
  const int g1 = static_cast<int>(pitches[2]) - static_cast<int>(pitches[1]);
  if (g0 <= 3 && g1 >= 4) {
    return {1U, 2U, 4U, 5U};
  }
  return {1U, 2U, 3U, 5U};
}

[[nodiscard]] double voice_leading_cost(std::span<const MidiPitch> cand,
                                        std::span<const MidiPitch> prev) noexcept {
  if (cand.empty() || prev.empty()) {
    return 0.0;
  }
  if (cand.size() == prev.size()) {
    double sum = 0.0;
    for (std::size_t i = 0; i < cand.size(); ++i) {
      sum += std::abs(static_cast<int>(cand[i]) - static_cast<int>(prev[i]));
    }
    return sum / static_cast<double>(cand.size());
  }
  double total = 0.0;
  for (const MidiPitch c : cand) {
    int best = 127;
    for (const MidiPitch p : prev) {
      best = std::min(best, std::abs(static_cast<int>(c) - static_cast<int>(p)));
    }
    total += static_cast<double>(best);
  }
  for (const MidiPitch p : prev) {
    int best = 127;
    for (const MidiPitch c : cand) {
      best = std::min(best, std::abs(static_cast<int>(c) - static_cast<int>(p)));
    }
    total += static_cast<double>(best);
  }
  return total / static_cast<double>(cand.size() + prev.size());
}

[[nodiscard]] std::vector<LessonNote>
make_voice_led_skeleton_rh(const ChordSymbol& chord, KeySignature key,
                           std::optional<std::vector<MidiPitch>>& prev_rh_pitches) {
  const auto pcs = skeleton_pitch_classes(chord);
  std::vector<std::vector<MidiPitch>> candidates;

  for (int oct = 3; oct <= 4; ++oct) {
    for (const std::uint8_t bottom_pc : pcs) {
      const int bottom_midi = 12 * (oct + 1) + static_cast<int>(bottom_pc);
      std::vector<MidiPitch> voicing;
      voicing.reserve(pcs.size());
      for (const std::uint8_t pc : pcs) {
        const int delta = (static_cast<int>(pc) - static_cast<int>(bottom_pc) + 12) % 12;
        voicing.push_back(static_cast<MidiPitch>(bottom_midi + delta));
      }
      std::ranges::sort(voicing);
      if (!voicing.empty() && voicing.front() >= 57 && voicing.back() <= 76 &&
          (static_cast<int>(voicing.back()) - static_cast<int>(voicing.front())) <= 12) {
        candidates.push_back(std::move(voicing));
      }
    }
  }

  if (candidates.empty()) {
    int root_midi = 60 + static_cast<int>(chord.root_pc);
    std::vector<MidiPitch> fallback;
    for (const std::uint8_t pc : pcs) {
      const int delta = (static_cast<int>(pc) - static_cast<int>(chord.root_pc) + 12) % 12;
      fallback.push_back(static_cast<MidiPitch>(root_midi + delta));
    }
    std::ranges::sort(fallback);
    if (!fallback.empty() && fallback.back() > 76) {
      for (auto& m : fallback) {
        m = static_cast<MidiPitch>(std::max(48, static_cast<int>(m) - 12));
      }
    }
    candidates.push_back(std::move(fallback));
  }

  constexpr double kComfortCenterMidi = 65.5;
  std::size_t best_idx = 0U;
  double best_score = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& cand = candidates[i];
    double sum = 0.0;
    for (const MidiPitch m : cand) {
      sum += static_cast<double>(m);
    }
    const double centroid = sum / static_cast<double>(std::max<std::size_t>(1U, cand.size()));
    const double center_pen = std::abs(centroid - kComfortCenterMidi);
    double score = 0.0;
    if (!prev_rh_pitches.has_value()) {
      score = center_pen;
      if (!cand.empty() && (cand.front() % 12U) == chord.root_pc) {
        score -= 0.25;
      }
    } else {
      score = voice_leading_cost(cand, *prev_rh_pitches) + 0.15 * center_pen;
    }
    if (score < best_score) {
      best_score = score;
      best_idx = i;
    }
  }

  const auto& chosen = candidates[best_idx];
  prev_rh_pitches = chosen;
  const auto fingers = assign_skeleton_rh_fingerings(chosen);

  std::vector<LessonNote> notes;
  notes.reserve(chosen.size());
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    const MidiPitch m = chosen[i];
    const auto sp = spell_midi_in_key(m, key);
    notes.push_back(LessonNote{
        .spelled = sp.value_or(
            SpelledNote{.letter = NoteLetter::C, .accidental = Accidental::Natural, .octave = 4}),
        .midi = m,
        .finger = fingers[i],
    });
  }
  return notes;
}

void emit_skeleton_chord_segment(LessonMeasure& sk, std::span<const LessonNote> tr_template,
                                 std::span<const LessonNote> bs_template, int start_tick,
                                 int duration_ticks) {
  const auto vals = split_ticks_to_values(duration_ticks);
  int t = start_tick;
  for (std::size_t vi = 0; vi < vals.size(); ++vi) {
    const bool tie_s = (vi + 1U < vals.size());
    const bool tie_p = (vi > 0U);
    std::vector<LessonNote> tr_notes(tr_template.begin(), tr_template.end());
    std::vector<LessonNote> bs_notes(bs_template.begin(), bs_template.end());
    for (auto& n : tr_notes) {
      n.tie_start = tie_s;
      n.tie_stop = tie_p;
    }
    for (auto& n : bs_notes) {
      n.tie_start = tie_s;
      n.tie_stop = tie_p;
    }
    sk.treble_events.push_back(LessonEvent{.start_tick = t,
                                           .duration = vals[vi],
                                           .is_rest = false,
                                           .voice = 0,
                                           .notes = std::move(tr_notes)});
    sk.bass_events.push_back(LessonEvent{.start_tick = t,
                                         .duration = vals[vi],
                                         .is_rest = false,
                                         .voice = 0,
                                         .notes = std::move(bs_notes)});
    t += vals[vi].ticks();
  }
}

[[nodiscard]] std::vector<LessonMeasure>
build_chord_skeleton_measures(const LessonScore& score, std::span<const KeySignature> bar_keys,
                              std::span<const TimeSignature> bar_meters) {
  std::vector<LessonMeasure> skeleton_measures(score.measures.size());
  std::optional<ChordSymbol> last_chord;
  std::optional<std::vector<MidiPitch>> prev_rh_pitches;
  std::vector<LessonNote> last_tr_notes;
  std::vector<LessonNote> last_bs_notes;

  for (std::size_t b = 0; b < score.measures.size(); ++b) {
    const auto& src = score.measures[b];
    LessonMeasure sk{};
    sk.bar_number = src.bar_number;
    sk.pickup_ticks = src.pickup_ticks;
    sk.key_change = src.key_change;
    sk.meter_change = src.meter_change;
    sk.harmony = src.harmony;

    const int mticks = effective_measure_ticks(src, bar_meters[b]);
    std::vector<HarmonicAnnotation> sorted_ha = src.harmony;
    std::ranges::sort(sorted_ha,
                      [](const auto& a, const auto& c) { return a.start_tick < c.start_tick; });

    if (sorted_ha.empty() && last_chord.has_value()) {
      emit_skeleton_chord_segment(sk, last_tr_notes, last_bs_notes, 0, mticks);
    } else if (!sorted_ha.empty()) {
      for (std::size_t hi = 0; hi < sorted_ha.size(); ++hi) {
        const int seg_start = sorted_ha[hi].start_tick;
        const int seg_end = (hi + 1U < sorted_ha.size()) ? sorted_ha[hi + 1U].start_tick : mticks;
        const int seg_ticks = std::max(0, seg_end - seg_start);
        if (seg_ticks == 0) {
          continue;
        }
        const auto parsed = parse_chord_symbol(sorted_ha[hi].chord_symbol);
        if (parsed.has_value()) {
          last_chord = *parsed;
          last_tr_notes = make_voice_led_skeleton_rh(*parsed, bar_keys[b], prev_rh_pitches);
          last_bs_notes = make_skeleton_lh(*parsed, bar_keys[b]);
          emit_skeleton_chord_segment(sk, last_tr_notes, last_bs_notes, seg_start, seg_ticks);
        } else {
          int t = seg_start;
          for (const auto& rv : split_ticks_to_values(seg_ticks)) {
            sk.treble_events.push_back(LessonEvent{
                .start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
            sk.bass_events.push_back(LessonEvent{
                .start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
            t += rv.ticks();
          }
        }
      }
      if (sorted_ha.front().start_tick > 0) {
        int t = 0;
        for (const auto& rv : split_ticks_to_values(sorted_ha.front().start_tick)) {
          sk.treble_events.insert(
              sk.treble_events.begin(),
              LessonEvent{
                  .start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
          sk.bass_events.insert(
              sk.bass_events.begin(),
              LessonEvent{
                  .start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
          t += rv.ticks();
        }
      }
    } else {
      int t = 0;
      for (const auto& rv : split_ticks_to_values(mticks)) {
        sk.treble_events.push_back(
            LessonEvent{.start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
        sk.bass_events.push_back(
            LessonEvent{.start_tick = t, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
        t += rv.ticks();
      }
    }

    skeleton_measures[b] = std::move(sk);
  }
  return skeleton_measures;
}

[[nodiscard]] int compute_tied_note_duration_ticks(std::span<const LessonMeasure> measures,
                                                   std::size_t start_bar, int staff_id,
                                                   int start_tick, MidiPitch midi, int base_ticks,
                                                   bool tie_start) noexcept {
  int total = base_ticks;
  if (!tie_start) {
    return total;
  }
  for (std::size_t b = start_bar; b < measures.size(); ++b) {
    const auto& evs = (staff_id == 0) ? measures[b].treble_events : measures[b].bass_events;
    bool found_next = false;
    bool still_open = false;
    for (const auto& ev : evs) {
      if (ev.is_rest || (b == start_bar && ev.start_tick <= start_tick)) {
        continue;
      }
      for (const auto& n : ev.notes) {
        if (n.midi == midi && n.tie_stop) {
          total += ev.duration.ticks();
          found_next = true;
          still_open = n.tie_start;
          start_tick = ev.start_tick;
          break;
        }
      }
      if (found_next && !still_open) {
        return total;
      }
    }
    if (!still_open) {
      break;
    }
  }
  return total;
}

void build_slice_targets(const LessonScore& score, std::span<const LessonMeasure> skeleton_measures,
                         const LessonConfig& config, std::span<const TimeSignature> bar_meters,
                         std::size_t slice_start_bar, std::size_t slice_end_bar,
                         std::vector<int>& out_bar_start_ticks, std::vector<int>& out_bar_ticks,
                         std::vector<TimeSignature>& out_bar_meters,
                         std::vector<LessonTarget>& out_targets, int& out_total_ticks) {
  int acc_ticks = 0;
  for (std::size_t b = slice_start_bar; b <= slice_end_bar; ++b) {
    const int mticks = effective_measure_ticks(score.measures[b], bar_meters[b]);
    out_bar_start_ticks.push_back(acc_ticks);
    out_bar_ticks.push_back(mticks);
    out_bar_meters.push_back(bar_meters[b]);

    if (config.practice_mode == LessonPracticeMode::ChordSkeleton) {
      std::vector<HarmonicAnnotation> sorted_ha = score.measures[b].harmony;
      std::ranges::sort(sorted_ha,
                        [](const auto& a, const auto& c) { return a.start_tick < c.start_tick; });
      const auto& sk_m = skeleton_measures[b];
      for (const auto& ha : sorted_ha) {
        const auto parsed = parse_chord_symbol(ha.chord_symbol);
        if (!parsed.has_value()) {
          continue;
        }
        LessonTarget target{};
        target.bar_index = b;
        target.tick_in_bar = ha.start_tick;
        target.global_tick = acc_ticks + ha.start_tick;
        target.pitch_class_mask = parsed->pitch_class_mask;
        target.pitch_class_match = true;
        if (config.hand_mode == LessonHandMode::LeftHand ||
            config.hand_mode == LessonHandMode::HandsTogether) {
          for (const auto& ev : sk_m.bass_events) {
            if (!ev.is_rest && ev.start_tick == ha.start_tick) {
              for (const auto& n : ev.notes) {
                target.expected_midi.push_back(n.midi);
                target.expected_spelled.push_back(n.spelled);
                target.bass_midi.push_back(n.midi);
              }
            }
          }
        }
        if (config.hand_mode == LessonHandMode::RightHand ||
            config.hand_mode == LessonHandMode::HandsTogether) {
          for (const auto& ev : sk_m.treble_events) {
            if (!ev.is_rest && ev.start_tick == ha.start_tick) {
              for (const auto& n : ev.notes) {
                target.expected_midi.push_back(n.midi);
                target.expected_spelled.push_back(n.spelled);
                target.treble_midi.push_back(n.midi);
              }
            }
          }
        }
        target.treble_midi = normalize_midi_set(target.treble_midi);
        target.bass_midi = normalize_midi_set(target.bass_midi);
        target.expected_midi = normalize_midi_set(target.expected_midi);
        out_targets.push_back(std::move(target));
      }
    } else {
      const auto& lm = score.measures[b];
      struct CollectedNote {
        LessonNote note;
        int staff_id{0};
        int total_duration_ticks{480};
      };
      std::map<int, std::vector<CollectedNote>> tick_notes;
      auto collect = [&](std::span<const LessonEvent> evs, int staff_id) {
        for (const auto& ev : evs) {
          if (ev.is_rest) {
            continue;
          }
          const int ev_ticks = ev.duration.ticks();
          for (const auto& n : ev.notes) {
            if (!n.tie_stop) {
              const int full_dur = compute_tied_note_duration_ticks(
                  score.measures, b, staff_id, ev.start_tick, n.midi, ev_ticks, n.tie_start);
              tick_notes[ev.start_tick].push_back(CollectedNote{
                  .note = n,
                  .staff_id = staff_id,
                  .total_duration_ticks = full_dur,
              });
            }
          }
        }
      };
      if (config.hand_mode == LessonHandMode::RightHand ||
          config.hand_mode == LessonHandMode::HandsTogether) {
        collect(lm.treble_events, 0);
      }
      if (config.hand_mode == LessonHandMode::LeftHand ||
          config.hand_mode == LessonHandMode::HandsTogether) {
        collect(lm.bass_events, 1);
      }
      for (const auto& [tick, notes] : tick_notes) {
        if (notes.empty()) {
          continue;
        }
        LessonTarget target{};
        target.bar_index = b;
        target.tick_in_bar = tick;
        target.global_tick = acc_ticks + tick;
        for (const auto& cn : notes) {
          target.expected_midi.push_back(cn.note.midi);
          target.expected_spelled.push_back(cn.note.spelled);
          if (cn.staff_id == 0) {
            target.treble_midi.push_back(cn.note.midi);
          } else {
            target.bass_midi.push_back(cn.note.midi);
          }
          if (cn.total_duration_ticks >= kTicksPerQuarter * 2) {
            target.sustained_durations.emplace_back(cn.note.midi, cn.total_duration_ticks);
          }
        }
        target.treble_midi = normalize_midi_set(target.treble_midi);
        target.bass_midi = normalize_midi_set(target.bass_midi);
        target.expected_midi = normalize_midi_set(target.expected_midi);
        target.pitch_class_mask = midi_pitch_class_mask(target.expected_midi);
        target.pitch_class_match = false;
        out_targets.push_back(std::move(target));
      }
    }

    acc_ticks += mticks;
  }
  out_total_ticks = acc_ticks;
}

} // namespace

void LessonEngine::clamp_custom_bar_range() noexcept {
  if (!config_.custom_bar_range.has_value()) {
    return;
  }
  if (score_.measures.empty()) {
    config_.custom_bar_range.reset();
    return;
  }
  const std::size_t max_idx = score_.measures.size() - 1U;
  const std::size_t a = std::min(config_.custom_bar_range->first, config_.custom_bar_range->second);
  const std::size_t b = std::max(config_.custom_bar_range->first, config_.custom_bar_range->second);
  config_.custom_bar_range = {std::min(a, max_idx), std::min(b, max_idx)};
}

void LessonEngine::load_score(LessonScore score, LessonConfig cfg) {
  score_ = std::move(score);
  config_ = cfg;
  config_.ramp_step_bpm = std::clamp(config_.ramp_step_bpm, 2.0, 8.0);
  phrases_ = effective_phrases(score_);
  if (config_.phrase_index.has_value() && *config_.phrase_index >= phrases_.size()) {
    config_.phrase_index.reset();
  }
  clamp_custom_bar_range();
  if (config_.custom_bar_range.has_value()) {
    config_.phrase_index.reset();
  }
  rebuild_slice_and_targets();
  reset_transport_state();
}

void LessonEngine::configure(LessonConfig cfg) {
  config_ = cfg;
  config_.ramp_step_bpm = std::clamp(config_.ramp_step_bpm, 2.0, 8.0);
  if (config_.phrase_index.has_value() && *config_.phrase_index >= phrases_.size()) {
    config_.phrase_index.reset();
  }
  clamp_custom_bar_range();
  if (config_.custom_bar_range.has_value()) {
    config_.phrase_index.reset();
  }
  rebuild_slice_and_targets();
  reset_transport_state();
}

void LessonEngine::set_practice_mode(LessonPracticeMode mode) {
  if (config_.practice_mode != mode) {
    config_.practice_mode = mode;
    rebuild_slice_and_targets();
    reset_transport_state();
  }
}

void LessonEngine::set_hand_mode(LessonHandMode hand) {
  if (config_.hand_mode != hand) {
    config_.hand_mode = hand;
    rebuild_slice_and_targets();
    reset_transport_state();
  }
}

void LessonEngine::set_tempo_step(TempoLadderStep step, double custom_bpm) {
  config_.tempo_step = step;
  if (custom_bpm > 0.0) {
    config_.custom_bpm = custom_bpm;
  }
  consecutive_clean_loops_ = 0;
}

void LessonEngine::set_phrase_index(std::optional<std::size_t> phrase_index) {
  if (phrase_index.has_value() && *phrase_index >= phrases_.size()) {
    phrase_index.reset();
  }
  const bool had_custom = config_.custom_bar_range.has_value();
  config_.custom_bar_range.reset();
  if (config_.phrase_index != phrase_index || had_custom) {
    config_.phrase_index = phrase_index;
    rebuild_slice_and_targets();
    reset_transport_state();
  }
}

void LessonEngine::set_custom_bar_range(std::optional<std::pair<std::size_t, std::size_t>> range) {
  config_.custom_bar_range = range;
  clamp_custom_bar_range();
  if (config_.custom_bar_range.has_value()) {
    config_.phrase_index.reset();
  }
  rebuild_slice_and_targets();
  reset_transport_state();
}

void LessonEngine::set_loop_phrase(bool loop) {
  config_.loop_phrase = loop;
  if (!loop) {
    consecutive_clean_loops_ = 0;
  }
}

void LessonEngine::set_auto_tempo_ramp(bool enabled) noexcept {
  if (config_.auto_tempo_ramp != enabled) {
    config_.auto_tempo_ramp = enabled;
    consecutive_clean_loops_ = 0;
  }
}

void LessonEngine::set_ramp_step_bpm(double step_bpm) noexcept {
  config_.ramp_step_bpm = std::clamp(step_bpm, 2.0, 8.0);
}

void LessonEngine::set_play_other_hand_accomp(bool enabled) noexcept {
  if (config_.play_other_hand_accomp == enabled) {
    return;
  }
  config_.play_other_hand_accomp = enabled;
  if (playback_state_ == LessonPlaybackState::CountIn ||
      playback_state_ == LessonPlaybackState::Playing ||
      playback_state_ == LessonPlaybackState::Paused) {
    if (enabled && config_.hand_mode != LessonHandMode::HandsTogether) {
      scheduled_playback_notes_ =
          build_slice_tick_notes(config_.hand_mode == LessonHandMode::LeftHand,
                                 config_.hand_mode == LessonHandMode::RightHand, 66, 64);
      next_playback_note_idx_ = 0;
      const double cur_tick =
          (playback_state_ == LessonPlaybackState::CountIn) ? -1.0 : elapsed_ticks_;
      while (next_playback_note_idx_ < scheduled_playback_notes_.size() &&
             static_cast<double>(scheduled_playback_notes_[next_playback_note_idx_].start_tick) <=
                 cur_tick + 1e-6) {
        ++next_playback_note_idx_;
      }
    } else {
      scheduled_playback_notes_.clear();
      next_playback_note_idx_ = 0;
    }
  }
}

void LessonEngine::reset_transport_state() {
  playback_state_ = LessonPlaybackState::Stopped;
  active_target_index_ = 0;
  elapsed_ticks_ = 0.0;
  count_in_remaining_ = 0;
  count_in_elapsed_ticks_ = 0.0;
  count_in_target_ticks_ = 0.0;
  count_in_beat_accum_ms_ = 0.0;
  play_beat_accum_ticks_ = 0.0;
  current_pulse_in_bar_ = 0;
  hits_ = 0;
  partial_hits_ = 0;
  misses_ = 0;
  consecutive_clean_loops_ = 0;
  last_loop_accuracy_ = 0.0;
  melody_vel_sum_ = 0;
  melody_vel_count_ = 0;
  accomp_vel_sum_ = 0;
  accomp_vel_count_ = 0;
  hw_min_vel_ = 255;
  hw_max_vel_ = 0;
  active_sustained_.clear();
  sustained_checked_ = 0;
  sustained_held_ = 0;
  damper_pedal_used_ = false;
  last_pedal_up_tick_ = damper_pedal_down_ ? -1e9 : 0.0;
  next_pedal_check_idx_ = 0;
  pedal_checked_ = 0;
  pedal_clean_ = 0;
  scheduled_playback_notes_.clear();
  next_playback_note_idx_ = 0;
  for (auto& t : targets_) {
    t.status = LessonTargetStatus::Pending;
    t.played_midi.clear();
    t.missed_midi.clear();
  }
}

void LessonEngine::rebuild_slice_and_targets() {
  targets_.clear();
  skeleton_measures_.clear();
  display_measures_.clear();
  slice_bar_start_ticks_.clear();
  slice_bar_ticks_.clear();
  slice_bar_meters_.clear();
  pedal_check_ticks_.clear();
  total_slice_ticks_ = 0;
  slice_start_bar_ = 0;
  slice_end_bar_ = 0;

  if (score_.measures.empty()) {
    return;
  }

  if (config_.custom_bar_range.has_value()) {
    clamp_custom_bar_range();
    slice_start_bar_ = config_.custom_bar_range->first;
    slice_end_bar_ = config_.custom_bar_range->second;
  } else if (config_.phrase_index.has_value() && *config_.phrase_index < phrases_.size()) {
    slice_start_bar_ = phrases_[*config_.phrase_index].start_bar;
    slice_end_bar_ = std::min(score_.measures.size() - 1U, phrases_[*config_.phrase_index].end_bar);
  } else {
    slice_start_bar_ = 0;
    slice_end_bar_ = score_.measures.size() - 1U;
  }

  KeySignature cur_key = score_.meta.key;
  TimeSignature cur_meter = score_.meta.time_signature;
  std::vector<KeySignature> bar_keys(score_.measures.size(), cur_key);
  std::vector<TimeSignature> bar_meters(score_.measures.size(), cur_meter);
  for (std::size_t b = 0; b < score_.measures.size(); ++b) {
    if (score_.measures[b].key_change.has_value()) {
      cur_key = *score_.measures[b].key_change;
    }
    if (score_.measures[b].meter_change.has_value()) {
      cur_meter = *score_.measures[b].meter_change;
    }
    bar_keys[b] = cur_key;
    bar_meters[b] = cur_meter;
  }

  skeleton_measures_ = build_chord_skeleton_measures(score_, bar_keys, bar_meters);
  display_measures_ = (config_.practice_mode == LessonPracticeMode::ChordSkeleton)
                          ? skeleton_measures_
                          : score_.measures;

  build_slice_targets(score_, skeleton_measures_, config_, bar_meters, slice_start_bar_,
                      slice_end_bar_, slice_bar_start_ticks_, slice_bar_ticks_, slice_bar_meters_,
                      targets_, total_slice_ticks_);

  for (const auto& pd : score_.pedals) {
    if (pd.end_bar >= slice_start_bar_ && pd.end_bar <= slice_end_bar_) {
      const std::size_t loc_b = pd.end_bar - slice_start_bar_;
      if (loc_b < slice_bar_start_ticks_.size()) {
        const int global_end = slice_bar_start_ticks_[loc_b] + pd.end_tick;
        if (global_end > 0 && global_end < total_slice_ticks_ - 120) {
          pedal_check_ticks_.push_back(static_cast<double>(global_end));
        }
      }
    }
  }
  std::ranges::sort(pedal_check_ticks_);
}

namespace {

struct ExpressiveEventShape {
  std::uint8_t velocity{86};
  double gate_scale{1.0};
  bool rolled{false};
};

[[nodiscard]] DynamicLevel initial_dynamic_before(const LessonScore& score,
                                                  std::size_t slice_start_bar) noexcept {
  DynamicLevel dyn = DynamicLevel::MF;
  const std::size_t limit = std::min(slice_start_bar, score.measures.size());
  for (std::size_t b = 0; b < limit; ++b) {
    int best_tick = -1;
    for (const auto& d : score.measures[b].dynamics) {
      if (d.start_tick >= best_tick) {
        best_tick = d.start_tick;
        dyn = d.level;
      }
    }
  }
  return dyn;
}

[[nodiscard]] ExpressiveEventShape compute_expressive_shape(const LessonMeasure& score_meas,
                                                            const LessonEvent& ev, int staff_id,
                                                            bool is_accomp,
                                                            DynamicLevel base_dyn) noexcept {
  DynamicLevel active = base_dyn;
  int best_dyn_tick = -1;
  for (const auto& d : score_meas.dynamics) {
    if (d.start_tick <= ev.start_tick && d.start_tick >= best_dyn_tick) {
      best_dyn_tick = d.start_tick;
      active = d.level;
    }
  }

  int vel = static_cast<int>(dynamic_level_velocity(active));
  for (const auto& hp : score_meas.hairpins) {
    if (ev.start_tick >= hp.start_tick && ev.start_tick <= hp.end_tick &&
        hp.end_tick > hp.start_tick) {
      const double t = static_cast<double>(ev.start_tick - hp.start_tick) /
                       static_cast<double>(hp.end_tick - hp.start_tick);
      const int delta = static_cast<int>(std::lround(10.0 * t));
      vel += (hp.kind == HairpinKind::Crescendo) ? delta : -delta;
      break;
    }
  }

  if (staff_id == 1) {
    vel -= 10;
  }
  if (is_accomp) {
    const double base_ref = (staff_id == 1) ? 76.0 : 86.0;
    const double target_ref = (staff_id == 1) ? 64.0 : 66.0;
    vel = static_cast<int>(std::lround(static_cast<double>(vel) * (target_ref / base_ref)));
  }

  double gate_scale = 1.0;
  if (has_articulation(ev.articulations, ArticulationFlag::Accent)) {
    vel += 16;
  }
  if (has_articulation(ev.articulations, ArticulationFlag::Tenuto)) {
    vel += 5;
    gate_scale = 0.98;
  }
  if (has_articulation(ev.articulations, ArticulationFlag::Staccato)) {
    gate_scale = 0.45;
  }

  const int max_vel = is_accomp ? 92 : 124;
  return ExpressiveEventShape{
      .velocity = static_cast<std::uint8_t>(std::clamp(vel, 32, max_vel)),
      .gate_scale = gate_scale,
      .rolled = has_articulation(ev.articulations, ArticulationFlag::Arpeggiate),
  };
}

} // namespace

std::vector<LessonEngine::ScheduledTickNote>
LessonEngine::build_slice_tick_notes(bool include_treble, bool include_bass,
                                     std::uint8_t treble_vel, std::uint8_t /*bass_vel*/) const {
  std::vector<ScheduledTickNote> out;
  if (score_.measures.empty() || slice_bar_meters_.empty()) {
    return out;
  }

  const bool is_accomp = (treble_vel < 75U);
  DynamicLevel bar_dyn = initial_dynamic_before(score_, slice_start_bar_);

  struct OpenTie {
    int staff_id{0};
    MidiPitch midi{60};
    std::uint8_t velocity{84};
    int start_tick{0};
    int duration_ticks{0};
    double start_offset_ms{0.0};
  };
  std::vector<OpenTie> open_ties;

  const auto& source_measures = (config_.practice_mode == LessonPracticeMode::ChordSkeleton)
                                    ? skeleton_measures_
                                    : score_.measures;

  int bar_start_tick = 0;
  for (std::size_t local_b = 0; local_b < slice_bar_meters_.size(); ++local_b) {
    const std::size_t b = slice_start_bar_ + local_b;
    const auto& lm = source_measures[b];
    const auto& expr_lm = score_.measures[b];
    const TimeSignature meter = slice_bar_meters_[local_b];

    auto process_staff = [&](std::span<const LessonEvent> evs, int staff_id) {
      std::vector<const LessonEvent*> ordered;
      ordered.reserve(evs.size());
      for (const auto& ev : evs) {
        if (!ev.is_rest) {
          ordered.push_back(&ev);
        }
      }
      std::ranges::sort(ordered, [](const LessonEvent* a, const LessonEvent* c) {
        return a->start_tick < c->start_tick;
      });

      for (const LessonEvent* ev : ordered) {
        const auto shape = compute_expressive_shape(expr_lm, *ev, staff_id, is_accomp, bar_dyn);
        const int ev_start_tick = bar_start_tick + ev->start_tick;
        const int raw_dur_ticks = std::max(1, ev->duration.ticks());
        const int gated_dur_ticks = std::max(
            40,
            static_cast<int>(std::lround(static_cast<double>(raw_dur_ticks) * shape.gate_scale)));
        std::vector<LessonNote> notes = ev->notes;
        if (shape.rolled && notes.size() > 1U) {
          std::ranges::sort(
              notes, [](const LessonNote& a, const LessonNote& c) { return a.midi < c.midi; });
        }
        for (std::size_t ni = 0; ni < notes.size(); ++ni) {
          const auto& n = notes[ni];
          const double roll_off_ms = shape.rolled ? (static_cast<double>(ni) * 35.0) : 0.0;
          if (n.tie_stop) {
            auto it = std::ranges::find_if(open_ties, [&](const OpenTie& ot) {
              return ot.staff_id == staff_id && ot.midi == n.midi;
            });
            if (it != open_ties.end()) {
              it->duration_ticks += gated_dur_ticks;
              if (!n.tie_start) {
                out.push_back(ScheduledTickNote{
                    .midi = it->midi,
                    .velocity = it->velocity,
                    .start_tick = it->start_tick,
                    .duration_ticks = it->duration_ticks,
                    .start_offset_ms = it->start_offset_ms,
                    .channel = 0,
                });
                open_ties.erase(it);
              }
              continue;
            }
          }

          if (n.tie_start) {
            open_ties.push_back(OpenTie{
                .staff_id = staff_id,
                .midi = n.midi,
                .velocity = shape.velocity,
                .start_tick = ev_start_tick,
                .duration_ticks = gated_dur_ticks,
                .start_offset_ms = roll_off_ms,
            });
          } else {
            out.push_back(ScheduledTickNote{
                .midi = n.midi,
                .velocity = shape.velocity,
                .start_tick = ev_start_tick,
                .duration_ticks = gated_dur_ticks,
                .start_offset_ms = roll_off_ms,
                .channel = 0,
            });
          }
        }
      }
    };

    if (include_treble) {
      process_staff(lm.treble_events, 0);
    }
    if (include_bass) {
      process_staff(lm.bass_events, 1);
    }

    int last_tick = -1;
    for (const auto& d : expr_lm.dynamics) {
      if (d.start_tick >= last_tick) {
        last_tick = d.start_tick;
        bar_dyn = d.level;
      }
    }
    bar_start_tick += effective_measure_ticks(lm, meter);
  }

  for (const auto& ot : open_ties) {
    out.push_back(ScheduledTickNote{
        .midi = ot.midi,
        .velocity = ot.velocity,
        .start_tick = ot.start_tick,
        .duration_ticks = ot.duration_ticks,
        .start_offset_ms = ot.start_offset_ms,
        .channel = 0,
    });
  }

  std::ranges::sort(out, [](const ScheduledTickNote& a, const ScheduledTickNote& b) {
    if (a.start_tick != b.start_tick) {
      return a.start_tick < b.start_tick;
    }
    if (a.start_offset_ms != b.start_offset_ms) {
      return a.start_offset_ms < b.start_offset_ms;
    }
    return a.midi < b.midi;
  });
  return out;
}

double LessonEngine::slice_ticks_to_ms(int start_tick, int duration_ticks) const noexcept {
  if (duration_ticks <= 0 || slice_bar_meters_.empty()) {
    return 40.0;
  }
  const double ms_per_beat = 60000.0 / std::max(30.0, effective_bpm());
  const int end_tick = start_tick + duration_ticks;
  int covered_ticks = 0;
  double total_ms = 0.0;

  for (std::size_t i = 0; i < slice_bar_meters_.size(); ++i) {
    const int b0 = slice_bar_start_ticks_[i];
    const int b1 = b0 + slice_bar_ticks_[i];
    const int ov = std::max(0, std::min(end_tick, b1) - std::max(start_tick, b0));
    if (ov > 0) {
      const double beat_ticks_d =
          static_cast<double>(std::max(1, slice_bar_meters_[i].beat_ticks()));
      total_ms += (static_cast<double>(ov) / beat_ticks_d) * ms_per_beat;
      covered_ticks += ov;
    }
  }
  if (covered_ticks < duration_ticks) {
    const double beat_ticks_d =
        static_cast<double>(std::max(1, slice_bar_meters_.back().beat_ticks()));
    total_ms += (static_cast<double>(duration_ticks - covered_ticks) / beat_ticks_d) * ms_per_beat;
  }
  return std::max(40.0, total_ms);
}

void LessonEngine::emit_due_playback_notes(double up_to_tick,
                                           std::vector<LessonPlaybackNote>& out) {
  while (next_playback_note_idx_ < scheduled_playback_notes_.size() &&
         static_cast<double>(scheduled_playback_notes_[next_playback_note_idx_].start_tick) <=
             up_to_tick + 1e-6) {
    const auto& n = scheduled_playback_notes_[next_playback_note_idx_];
    out.push_back(LessonPlaybackNote{
        .midi = n.midi,
        .velocity = n.velocity,
        .start_ms = n.start_offset_ms,
        .duration_ms =
            std::max(40.0, slice_ticks_to_ms(n.start_tick, n.duration_ticks) - n.start_offset_ms),
        .channel = n.channel,
    });
    ++next_playback_note_idx_;
  }
}

LessonTickResult LessonEngine::start() {
  reset_transport_state();
  if (score_.measures.empty() || slice_bar_meters_.empty() || slice_bar_ticks_.empty()) {
    return {};
  }
  const int full_ticks = std::max(1, slice_bar_meters_.front().measure_ticks());
  const int bar0_ticks = std::max(1, slice_bar_ticks_.front());
  const int beat_ticks = std::max(1, slice_bar_meters_.front().beat_ticks());

  int ci_ticks = full_ticks;
  if (bar0_ticks < full_ticks) {
    ci_ticks = full_ticks - bar0_ticks;
    if (ci_ticks < beat_ticks) {
      ci_ticks += full_ticks;
    }
  }

  playback_state_ = LessonPlaybackState::CountIn;
  count_in_elapsed_ticks_ = 0.0;
  count_in_target_ticks_ = static_cast<double>(ci_ticks);
  count_in_remaining_ =
      static_cast<int>(std::ceil(count_in_target_ticks_ / static_cast<double>(beat_ticks)));
  count_in_beat_accum_ms_ = 0.0;
  if (config_.play_other_hand_accomp && config_.hand_mode != LessonHandMode::HandsTogether) {
    scheduled_playback_notes_ =
        build_slice_tick_notes(config_.hand_mode == LessonHandMode::LeftHand,
                               config_.hand_mode == LessonHandMode::RightHand, 66, 64);
    next_playback_note_idx_ = 0;
  }
  return LessonTickResult{.metronome_click = true, .metronome_accent = true};
}

LessonTickResult LessonEngine::start_demo() {
  reset_transport_state();
  if (score_.measures.empty() || slice_bar_meters_.empty() || slice_bar_ticks_.empty()) {
    return {};
  }
  const bool inc_tr = (config_.hand_mode == LessonHandMode::RightHand ||
                       config_.hand_mode == LessonHandMode::HandsTogether);
  const bool inc_bs = (config_.hand_mode == LessonHandMode::LeftHand ||
                       config_.hand_mode == LessonHandMode::HandsTogether);
  scheduled_playback_notes_ = build_slice_tick_notes(inc_tr, inc_bs, 90, 76);
  next_playback_note_idx_ = 0;
  playback_state_ = LessonPlaybackState::DemoPlaying;
  elapsed_ticks_ = 0.0;

  LessonTickResult out{};
  emit_due_playback_notes(0.0, out.playback_notes);
  return out;
}

void LessonEngine::stop_demo() {
  reset_transport_state();
}

std::vector<MidiPitch> LessonEngine::active_demo_midi_notes() const {
  if (playback_state_ != LessonPlaybackState::DemoPlaying) {
    return {};
  }
  std::vector<MidiPitch> active;
  for (const auto& n : scheduled_playback_notes_) {
    const double s = static_cast<double>(n.start_tick);
    const double e = static_cast<double>(n.start_tick + n.duration_ticks);
    if (elapsed_ticks_ + 1e-6 >= s && elapsed_ticks_ + 1e-6 < e) {
      active.push_back(n.midi);
    }
  }
  return normalize_midi_set(active);
}

void LessonEngine::pause() noexcept {
  if (playback_state_ == LessonPlaybackState::Playing ||
      playback_state_ == LessonPlaybackState::CountIn) {
    playback_state_ = LessonPlaybackState::Paused;
  }
}

void LessonEngine::resume() noexcept {
  if (playback_state_ == LessonPlaybackState::Paused) {
    playback_state_ =
        (count_in_remaining_ > 0) ? LessonPlaybackState::CountIn : LessonPlaybackState::Playing;
  }
}

void LessonEngine::stop() {
  reset_transport_state();
}

LessonTickResult LessonEngine::update(double dt_ms) {
  LessonTickResult out{};
  if (dt_ms <= 0.0 || slice_bar_meters_.empty()) {
    return out;
  }

  const double bpm = effective_bpm();
  const double ms_per_beat = 60000.0 / std::max(30.0, bpm);

  if (playback_state_ == LessonPlaybackState::CountIn) {
    const int beat_ticks = std::max(1, slice_bar_meters_.front().beat_ticks());
    const double beat_ticks_d = static_cast<double>(beat_ticks);
    const double ms_per_tick = ms_per_beat / beat_ticks_d;
    const double dt_ticks = dt_ms / ms_per_tick;

    const double old_elapsed = count_in_elapsed_ticks_;
    const double new_elapsed = old_elapsed + dt_ticks;
    const double clamped_new = std::min(new_elapsed, count_in_target_ticks_);

    const int prev_pulse = static_cast<int>(std::floor((old_elapsed + 1e-6) / beat_ticks_d));
    const int next_pulse = static_cast<int>(std::floor((clamped_new + 1e-6) / beat_ticks_d));
    const int pulse_count = std::max(1, slice_bar_meters_.front().pulse_count());
    for (int p = prev_pulse + 1; p <= next_pulse; ++p) {
      out.metronome_click = true;
      if ((p % pulse_count) == 0) {
        out.metronome_accent = true;
      }
    }

    count_in_elapsed_ticks_ = new_elapsed;
    count_in_remaining_ = std::max(
        0,
        static_cast<int>(std::ceil((count_in_target_ticks_ - new_elapsed - 1e-6) / beat_ticks_d)));

    if (new_elapsed + 1e-6 >= count_in_target_ticks_) {
      count_in_remaining_ = 0;
      playback_state_ = LessonPlaybackState::Playing;
      const double overshoot = std::max(0.0, new_elapsed - count_in_target_ticks_);
      elapsed_ticks_ = overshoot;

      const int full_ticks = slice_bar_meters_.front().measure_ticks();
      const int bar0_ticks = slice_bar_ticks_.empty() ? full_ticks : slice_bar_ticks_.front();
      const int pickup_offset = (bar0_ticks < full_ticks) ? (full_ticks - bar0_ticks) : 0;
      current_pulse_in_bar_ = (pickup_offset / beat_ticks) % pulse_count;
      play_beat_accum_ticks_ = static_cast<double>(pickup_offset % beat_ticks) + overshoot;
      emit_due_playback_notes(elapsed_ticks_, out.playback_notes);
    }
    return out;
  }

  if (playback_state_ == LessonPlaybackState::DemoPlaying) {
    const std::size_t local_bar = current_bar_in_slice();
    const TimeSignature cur_meter = slice_bar_meters_[local_bar];
    const double beat_ticks_d = static_cast<double>(std::max(1, cur_meter.beat_ticks()));
    const double ms_per_tick = ms_per_beat / beat_ticks_d;
    elapsed_ticks_ += dt_ms / ms_per_tick;
    emit_due_playback_notes(elapsed_ticks_, out.playback_notes);

    if (elapsed_ticks_ + 1e-6 >= static_cast<double>(total_slice_ticks_)) {
      if (config_.loop_phrase) {
        elapsed_ticks_ = std::max(0.0, elapsed_ticks_ - static_cast<double>(total_slice_ticks_));
        next_playback_note_idx_ = 0;
        emit_due_playback_notes(elapsed_ticks_, out.playback_notes);
      } else {
        reset_transport_state();
      }
    }
    return out;
  }

  if (playback_state_ != LessonPlaybackState::Playing) {
    return out;
  }

  const std::size_t local_bar = current_bar_in_slice();
  const TimeSignature cur_meter = slice_bar_meters_[local_bar];
  const double beat_ticks_d = static_cast<double>(std::max(1, cur_meter.beat_ticks()));
  const double ms_per_tick = ms_per_beat / beat_ticks_d;
  const double dt_ticks = dt_ms / ms_per_tick;

  elapsed_ticks_ += dt_ticks;
  play_beat_accum_ticks_ += dt_ticks;
  emit_due_playback_notes(elapsed_ticks_, out.playback_notes);

  if (!damper_pedal_down_) {
    last_pedal_up_tick_ = elapsed_ticks_;
  }
  for (auto& sn : active_sustained_) {
    if (sn.finished) {
      continue;
    }
    if (sn.finger_down || damper_pedal_down_) {
      sn.held_ticks = std::min(sn.target_duration_ticks, sn.held_ticks + dt_ticks);
    }
    if (elapsed_ticks_ + 1e-6 >= sn.start_tick + sn.target_duration_ticks) {
      sn.finished = true;
      ++sustained_checked_;
      if (sn.held_ticks >= 0.65 * sn.target_duration_ticks) {
        ++sustained_held_;
      }
    }
  }
  while (next_pedal_check_idx_ < pedal_check_ticks_.size() &&
         elapsed_ticks_ >= pedal_check_ticks_[next_pedal_check_idx_] + 240.0) {
    const double target_t = pedal_check_ticks_[next_pedal_check_idx_];
    ++pedal_checked_;
    if (last_pedal_up_tick_ >= target_t - 240.0) {
      ++pedal_clean_;
    }
    ++next_pedal_check_idx_;
  }

  while (play_beat_accum_ticks_ + 1e-6 >= beat_ticks_d) {
    play_beat_accum_ticks_ -= beat_ticks_d;
    ++current_pulse_in_bar_;
    const std::size_t b = current_bar_in_slice();
    if (current_pulse_in_bar_ >= slice_bar_meters_[b].pulse_count()) {
      current_pulse_in_bar_ = 0;
      out.metronome_accent = true;
    }
    out.metronome_click = true;
  }

  const double hit_window_ms = std::max(140.0, 0.38 * ms_per_beat);
  const double hit_window_ticks = hit_window_ms / ms_per_tick;
  while (active_target_index_ < targets_.size()) {
    auto& tgt = targets_[active_target_index_];
    if (tgt.status != LessonTargetStatus::Pending) {
      ++active_target_index_;
      continue;
    }
    if (static_cast<double>(tgt.global_tick) + hit_window_ticks < elapsed_ticks_) {
      tgt.status = LessonTargetStatus::Missed;
      ++misses_;
      ++active_target_index_;
    } else {
      break;
    }
  }

  if (elapsed_ticks_ + 1e-6 >= static_cast<double>(total_slice_ticks_)) {
    for (auto& sn : active_sustained_) {
      if (sn.finished) {
        continue;
      }
      const double elapsed_win =
          std::min(sn.target_duration_ticks, std::max(0.0, elapsed_ticks_ - sn.start_tick));
      sn.finished = true;
      if (elapsed_win >= 240.0) {
        ++sustained_checked_;
        if (sn.held_ticks >= 0.65 * elapsed_win) {
          ++sustained_held_;
        }
      }
    }
    while (next_pedal_check_idx_ < pedal_check_ticks_.size()) {
      const double target_t = pedal_check_ticks_[next_pedal_check_idx_];
      ++pedal_checked_;
      if (last_pedal_up_tick_ >= target_t - 240.0) {
        ++pedal_clean_;
      }
      ++next_pedal_check_idx_;
    }

    while (active_target_index_ < targets_.size()) {
      if (targets_[active_target_index_].status == LessonTargetStatus::Pending) {
        targets_[active_target_index_].status = LessonTargetStatus::Missed;
        ++misses_;
      }
      ++active_target_index_;
    }
    ++completed_runs_;
    out.phrase_completed = true;

    std::uint32_t loop_hits = 0U;
    std::uint32_t loop_partials = 0U;
    std::uint32_t loop_misses = 0U;
    for (const auto& t : targets_) {
      if (t.status == LessonTargetStatus::Hit) {
        ++loop_hits;
      } else if (t.status == LessonTargetStatus::PartialHit) {
        ++loop_partials;
      } else if (t.status == LessonTargetStatus::Missed) {
        ++loop_misses;
      }
    }
    const std::uint32_t loop_total = loop_hits + loop_partials + loop_misses;
    const double loop_acc =
        (loop_total == 0U)
            ? 0.0
            : (static_cast<double>(loop_hits) + 0.5 * static_cast<double>(loop_partials)) /
                  static_cast<double>(loop_total);
    last_loop_accuracy_ = loop_acc;
    out.loop_accuracy = loop_acc;

    if (config_.practice_mode == LessonPracticeMode::SteadyPulse && config_.loop_phrase &&
        config_.auto_tempo_ramp && loop_total > 0U) {
      const double step = std::clamp(config_.ramp_step_bpm, 2.0, 8.0);
      const double cur_bpm = effective_bpm();
      const double target_cap = std::clamp(score_.meta.target_bpm, 40.0, 220.0);
      const double floor_bpm = std::max(40.0, std::round(target_cap * 0.50));
      if (loop_acc >= 0.95 && loop_misses == 0U) {
        ++consecutive_clean_loops_;
        if (consecutive_clean_loops_ >= 2) {
          consecutive_clean_loops_ = 0;
          const double next_bpm = std::min(target_cap, cur_bpm + step);
          if (next_bpm > cur_bpm + 1e-6) {
            config_.tempo_step = TempoLadderStep::Custom;
            config_.custom_bpm = next_bpm;
            out.tempo_ramped_delta = next_bpm - cur_bpm;
          }
        }
      } else if (loop_acc < 0.80 || loop_misses >= 2U) {
        consecutive_clean_loops_ = 0;
        const double next_bpm = std::max(floor_bpm, cur_bpm - step);
        if (next_bpm < cur_bpm - 1e-6) {
          config_.tempo_step = TempoLadderStep::Custom;
          config_.custom_bpm = next_bpm;
          out.tempo_ramped_delta = next_bpm - cur_bpm;
        }
      } else {
        consecutive_clean_loops_ = 0;
      }
    }

    if (config_.loop_phrase) {
      elapsed_ticks_ = std::max(0.0, elapsed_ticks_ - static_cast<double>(total_slice_ticks_));
      const int full_ticks = slice_bar_meters_.front().measure_ticks();
      const int bar0_ticks = slice_bar_ticks_.empty() ? full_ticks : slice_bar_ticks_.front();
      const int beat_ticks = std::max(1, slice_bar_meters_.front().beat_ticks());
      const int pulse_count = std::max(1, slice_bar_meters_.front().pulse_count());
      const int pickup_offset = (bar0_ticks < full_ticks) ? (full_ticks - bar0_ticks) : 0;
      current_pulse_in_bar_ = (pickup_offset / beat_ticks) % pulse_count;
      play_beat_accum_ticks_ = static_cast<double>(pickup_offset % beat_ticks) + elapsed_ticks_;
      active_target_index_ = 0;
      next_playback_note_idx_ = 0;
      active_sustained_.clear();
      next_pedal_check_idx_ = 0;
      last_pedal_up_tick_ = damper_pedal_down_ ? -1e9 : elapsed_ticks_;
      emit_due_playback_notes(elapsed_ticks_, out.playback_notes);
      for (auto& t : targets_) {
        t.status = LessonTargetStatus::Pending;
        t.played_midi.clear();
        t.missed_midi.clear();
      }
    } else {
      elapsed_ticks_ = static_cast<double>(total_slice_ticks_);
      playback_state_ = LessonPlaybackState::Completed;
      out.run_completed = true;
    }
  }

  return out;
}

void LessonEngine::on_note_off(MidiPitch midi) noexcept {
  for (auto& sn : active_sustained_) {
    if (!sn.finished && sn.midi == midi) {
      sn.finger_down = false;
    }
  }
}

void LessonEngine::on_damper_pedal(bool down, bool from_hardware) noexcept {
  if (down && from_hardware && playback_state_ == LessonPlaybackState::Playing) {
    damper_pedal_used_ = true;
  }
  if (!down) {
    last_pedal_up_tick_ = elapsed_ticks_;
  }
  damper_pedal_down_ = down;
}

LessonSubmitResult LessonEngine::submit_played_notes(std::span<const MidiPitch> played_midi) {
  return submit_played_notes(played_midi, {}, false);
}

LessonSubmitResult
LessonEngine::submit_played_notes(std::span<const MidiPitch> played_midi,
                                  std::span<const std::uint8_t> played_velocities,
                                  bool from_hardware) {
  LessonSubmitResult res{};
  const auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty() || targets_.empty()) {
    return res;
  }

  auto record_musicality = [&](const LessonTarget& tgt) {
    if (from_hardware && played_velocities.size() == played_midi.size()) {
      auto lookup_vel = [&](MidiPitch p) -> std::optional<std::uint8_t> {
        for (std::size_t i = 0; i < played_midi.size(); ++i) {
          if (played_midi[i] == p) {
            return played_velocities[i];
          }
        }
        return std::nullopt;
      };
      for (const std::uint8_t v : played_velocities) {
        hw_min_vel_ = std::min(hw_min_vel_, v);
        hw_max_vel_ = std::max(hw_max_vel_, v);
      }
      if (!tgt.treble_midi.empty() && !tgt.bass_midi.empty()) {
        const MidiPitch mel_pitch = tgt.treble_midi.back();
        if (const auto mv = lookup_vel(mel_pitch); mv.has_value()) {
          melody_vel_sum_ += *mv;
          ++melody_vel_count_;
        }
        for (std::size_t k = 0; k + 1U < tgt.treble_midi.size(); ++k) {
          if (tgt.treble_midi[k] == mel_pitch) {
            continue;
          }
          if (const auto av = lookup_vel(tgt.treble_midi[k]); av.has_value()) {
            accomp_vel_sum_ += *av;
            ++accomp_vel_count_;
          }
        }
        for (const MidiPitch bp : tgt.bass_midi) {
          if (bp == mel_pitch) {
            continue;
          }
          if (const auto av = lookup_vel(bp); av.has_value()) {
            accomp_vel_sum_ += *av;
            ++accomp_vel_count_;
          }
        }
      } else if (tgt.treble_midi.size() >= 2U) {
        const MidiPitch mel_pitch = tgt.treble_midi.back();
        if (const auto mv = lookup_vel(mel_pitch); mv.has_value()) {
          melody_vel_sum_ += *mv;
          ++melody_vel_count_;
        }
        for (std::size_t k = 0; k + 1U < tgt.treble_midi.size(); ++k) {
          if (const auto av = lookup_vel(tgt.treble_midi[k]); av.has_value()) {
            accomp_vel_sum_ += *av;
            ++accomp_vel_count_;
          }
        }
      }
    }

    if (config_.practice_mode == LessonPracticeMode::SteadyPulse &&
        !tgt.sustained_durations.empty()) {
      for (const auto& [smidi, dur_ticks] : tgt.sustained_durations) {
        if (std::ranges::find(normalized, smidi) != normalized.end()) {
          active_sustained_.push_back(ActiveSustainedNote{
              .midi = smidi,
              .start_tick = elapsed_ticks_,
              .target_duration_ticks = static_cast<double>(dur_ticks),
              .held_ticks = 0.0,
              .finger_down = true,
              .finished = false,
          });
        }
      }
    }
  };

  auto matches_target = [&](const LessonTarget& tgt) noexcept {
    if (tgt.pitch_class_match) {
      const std::uint16_t played_mask = midi_pitch_class_mask(normalized);
      return tgt.pitch_class_mask != 0U &&
             (played_mask & tgt.pitch_class_mask) == tgt.pitch_class_mask;
    }
    return normalized == tgt.expected_midi;
  };

  if (config_.practice_mode == LessonPracticeMode::SteadyPulse) {
    if (playback_state_ != LessonPlaybackState::Playing) {
      return res;
    }
    const std::size_t local_bar = current_bar_in_slice();
    const TimeSignature cur_meter = slice_bar_meters_.empty()
                                        ? TimeSignature{.numerator = 4, .denominator = 4}
                                        : slice_bar_meters_[local_bar];
    const double ms_per_beat = 60000.0 / std::max(30.0, effective_bpm());
    const double ms_per_tick =
        ms_per_beat / static_cast<double>(std::max(1, cur_meter.beat_ticks()));
    const double hit_window_ticks = std::max(140.0, 0.38 * ms_per_beat) / ms_per_tick;

    std::optional<std::size_t> candidate_idx;
    double best_dist = std::numeric_limits<double>::max();
    for (std::size_t i = active_target_index_; i < targets_.size(); ++i) {
      if (targets_[i].status != LessonTargetStatus::Pending) {
        continue;
      }
      const double dist = std::abs(static_cast<double>(targets_[i].global_tick) - elapsed_ticks_);
      if (dist <= hit_window_ticks && dist < best_dist) {
        best_dist = dist;
        candidate_idx = i;
      }
      if (static_cast<double>(targets_[i].global_tick) - elapsed_ticks_ > hit_window_ticks) {
        break;
      }
    }

    if (!candidate_idx.has_value()) {
      return res;
    }

    auto& tgt = targets_[*candidate_idx];
    if (matches_target(tgt)) {
      tgt.status = LessonTargetStatus::Hit;
      tgt.played_midi = normalized;
      tgt.missed_midi.clear();
      ++hits_;
      record_musicality(tgt);
      while (active_target_index_ < targets_.size() &&
             targets_[active_target_index_].status != LessonTargetStatus::Pending) {
        ++active_target_index_;
      }
      res.matched = true;
    } else {
      bool all_expected = !tgt.pitch_class_match && tgt.expected_midi.size() >= 2U;
      if (all_expected) {
        for (const MidiPitch pm : normalized) {
          if (std::ranges::find(tgt.expected_midi, pm) == tgt.expected_midi.end()) {
            all_expected = false;
            break;
          }
        }
      }
      if (all_expected && normalized.size() * 2U >= tgt.expected_midi.size()) {
        tgt.status = LessonTargetStatus::PartialHit;
        tgt.played_midi = normalized;
        tgt.missed_midi.clear();
        for (const MidiPitch em : tgt.expected_midi) {
          if (std::ranges::find(normalized, em) == normalized.end()) {
            tgt.missed_midi.push_back(em);
          }
        }
        ++partial_hits_;
        record_musicality(tgt);
        while (active_target_index_ < targets_.size() &&
               targets_[active_target_index_].status != LessonTargetStatus::Pending) {
          ++active_target_index_;
        }
        res.partial_match = true;
      } else {
        tgt.status = LessonTargetStatus::Missed;
        tgt.played_midi = normalized;
        tgt.missed_midi.clear();
        ++misses_;
        while (active_target_index_ < targets_.size() &&
               targets_[active_target_index_].status != LessonTargetStatus::Pending) {
          ++active_target_index_;
        }
        res.wrong_note = true;
      }
    }
    return res;
  }

  // WaitForNote or ChordSkeleton (untimed sequential advance)
  if (active_target_index_ >= targets_.size()) {
    return res;
  }
  auto& tgt = targets_[active_target_index_];
  if (matches_target(tgt)) {
    tgt.status = LessonTargetStatus::Hit;
    tgt.played_midi = normalized;
    ++hits_;
    record_musicality(tgt);
    ++active_target_index_;
    res.matched = true;
    if (active_target_index_ >= targets_.size()) {
      ++completed_runs_;
      res.phrase_completed = true;
      std::uint32_t loop_hits = 0U;
      std::uint32_t loop_misses = 0U;
      for (const auto& t : targets_) {
        if (t.status == LessonTargetStatus::Hit) {
          ++loop_hits;
        } else if (t.status == LessonTargetStatus::Missed) {
          ++loop_misses;
        }
      }
      const std::uint32_t loop_total = loop_hits + loop_misses;
      last_loop_accuracy_ = (loop_total == 0U)
                                ? 0.0
                                : static_cast<double>(loop_hits) / static_cast<double>(loop_total);
      if (config_.loop_phrase) {
        active_target_index_ = 0;
        for (auto& t : targets_) {
          t.status = LessonTargetStatus::Pending;
          t.played_midi.clear();
        }
      } else {
        playback_state_ = LessonPlaybackState::Completed;
        res.run_completed = true;
      }
    }
  } else {
    tgt.status = LessonTargetStatus::Missed;
    tgt.played_midi = normalized;
    ++misses_;
    res.wrong_note = true;
  }
  return res;
}

std::vector<LessonPlaybackNote> LessonEngine::build_demo_playback() const {
  std::vector<LessonPlaybackNote> out;
  if (score_.measures.empty() || slice_bar_meters_.empty()) {
    return out;
  }

  const double ms_per_beat = 60000.0 / std::max(30.0, effective_bpm());
  struct OpenTie {
    int staff_id{0};
    MidiPitch midi{60};
    std::uint8_t velocity{84};
    double start_ms{0.0};
    double duration_ms{0.0};
  };
  std::vector<OpenTie> open_ties;

  const auto& source_measures = (config_.practice_mode == LessonPracticeMode::ChordSkeleton)
                                    ? skeleton_measures_
                                    : score_.measures;

  DynamicLevel bar_dyn = initial_dynamic_before(score_, slice_start_bar_);
  double bar_start_ms = 0.0;
  for (std::size_t local_b = 0; local_b < slice_bar_meters_.size(); ++local_b) {
    const std::size_t b = slice_start_bar_ + local_b;
    const auto& lm = source_measures[b];
    const auto& expr_lm = score_.measures[b];
    const TimeSignature meter = slice_bar_meters_[local_b];
    const double ms_per_tick = ms_per_beat / static_cast<double>(std::max(1, meter.beat_ticks()));

    auto process_staff = [&](std::span<const LessonEvent> evs, int staff_id) {
      std::vector<const LessonEvent*> ordered;
      ordered.reserve(evs.size());
      for (const auto& ev : evs) {
        if (!ev.is_rest) {
          ordered.push_back(&ev);
        }
      }
      std::ranges::sort(ordered, [](const LessonEvent* a, const LessonEvent* c) {
        return a->start_tick < c->start_tick;
      });

      for (const LessonEvent* ev : ordered) {
        const auto shape = compute_expressive_shape(expr_lm, *ev, staff_id, false, bar_dyn);
        const double ev_start_ms = bar_start_ms + static_cast<double>(ev->start_tick) * ms_per_tick;
        const double raw_dur_ms = static_cast<double>(ev->duration.ticks()) * ms_per_tick;
        const double gated_dur_ms = std::max(40.0, raw_dur_ms * shape.gate_scale);
        std::vector<LessonNote> notes = ev->notes;
        if (shape.rolled && notes.size() > 1U) {
          std::ranges::sort(
              notes, [](const LessonNote& a, const LessonNote& c) { return a.midi < c.midi; });
        }
        for (std::size_t ni = 0; ni < notes.size(); ++ni) {
          const auto& n = notes[ni];
          const double roll_off_ms = shape.rolled ? (static_cast<double>(ni) * 35.0) : 0.0;
          const double note_start_ms = ev_start_ms + roll_off_ms;
          const double note_dur_ms = std::max(45.0, gated_dur_ms - roll_off_ms);
          if (n.tie_stop) {
            auto it = std::ranges::find_if(open_ties, [&](const OpenTie& ot) {
              return ot.staff_id == staff_id && ot.midi == n.midi;
            });
            if (it != open_ties.end()) {
              it->duration_ms += raw_dur_ms;
              if (!n.tie_start) {
                out.push_back(LessonPlaybackNote{
                    .midi = it->midi,
                    .velocity = it->velocity,
                    .start_ms = it->start_ms,
                    .duration_ms = std::max(40.0, it->duration_ms),
                    .channel = 0,
                });
                open_ties.erase(it);
              }
              continue;
            }
          }

          if (n.tie_start) {
            open_ties.push_back(OpenTie{
                .staff_id = staff_id,
                .midi = n.midi,
                .velocity = shape.velocity,
                .start_ms = note_start_ms,
                .duration_ms = note_dur_ms,
            });
          } else {
            out.push_back(LessonPlaybackNote{
                .midi = n.midi,
                .velocity = shape.velocity,
                .start_ms = note_start_ms,
                .duration_ms = note_dur_ms,
                .channel = 0,
            });
          }
        }
      }
    };

    if (config_.hand_mode == LessonHandMode::RightHand ||
        config_.hand_mode == LessonHandMode::HandsTogether) {
      process_staff(lm.treble_events, 0);
    }
    if (config_.hand_mode == LessonHandMode::LeftHand ||
        config_.hand_mode == LessonHandMode::HandsTogether) {
      process_staff(lm.bass_events, 1);
    }

    int last_tick = -1;
    for (const auto& d : expr_lm.dynamics) {
      if (d.start_tick >= last_tick) {
        last_tick = d.start_tick;
        bar_dyn = d.level;
      }
    }
    bar_start_ms += static_cast<double>(effective_measure_ticks(lm, meter)) * ms_per_tick;
  }

  for (const auto& ot : open_ties) {
    out.push_back(LessonPlaybackNote{
        .midi = ot.midi,
        .velocity = ot.velocity,
        .start_ms = ot.start_ms,
        .duration_ms = std::max(40.0, ot.duration_ms),
        .channel = 0,
    });
  }

  std::ranges::sort(out, [](const LessonPlaybackNote& a, const LessonPlaybackNote& b) {
    if (a.start_ms != b.start_ms) {
      return a.start_ms < b.start_ms;
    }
    return a.midi < b.midi;
  });
  return out;
}

LessonRunSummary LessonEngine::build_run_summary() const {
  std::vector<BarDiagnostic> diagnostics;
  if (!score_.measures.empty() && slice_end_bar_ < score_.measures.size() &&
      slice_start_bar_ <= slice_end_bar_) {
    diagnostics.reserve(slice_end_bar_ - slice_start_bar_ + 1U);
    for (std::size_t b = slice_start_bar_; b <= slice_end_bar_; ++b) {
      BarDiagnostic diag{};
      diag.bar_index = b;
      diag.bar_number = score_.measures[b].bar_number;
      for (const auto& t : targets_) {
        if (t.bar_index != b) {
          continue;
        }
        if (t.status == LessonTargetStatus::Hit) {
          ++diag.hits;
        } else if (t.status == LessonTargetStatus::PartialHit) {
          ++diag.partial_hits;
        } else if (t.status == LessonTargetStatus::Missed) {
          ++diag.misses;
        }
      }
      diagnostics.push_back(diag);
    }
  }
  MusicalityMetrics mus{};
  if (melody_vel_count_ >= 2U && accomp_vel_count_ >= 2U && hw_max_vel_ > hw_min_vel_) {
    mus.voicing_available = true;
    mus.melody_avg_velocity =
        static_cast<double>(melody_vel_sum_) / static_cast<double>(melody_vel_count_);
    mus.accomp_avg_velocity =
        static_cast<double>(accomp_vel_sum_) / static_cast<double>(accomp_vel_count_);
    mus.voicing_delta = mus.melody_avg_velocity - mus.accomp_avg_velocity;
    mus.voicing_balanced = (mus.voicing_delta >= 6.0);
  }
  if (sustained_checked_ > 0U) {
    mus.sustain_available = true;
    mus.sustained_notes_checked = sustained_checked_;
    mus.sustained_notes_held = sustained_held_;
    mus.sustain_hold_rate =
        static_cast<double>(sustained_held_) / static_cast<double>(sustained_checked_);
  }
  if (damper_pedal_used_ && pedal_checked_ > 0U) {
    mus.pedal_available = true;
    mus.pedal_changes_checked = pedal_checked_;
    mus.pedal_changes_clean = pedal_clean_;
    mus.pedal_clean_rate = static_cast<double>(pedal_clean_) / static_cast<double>(pedal_checked_);
  }

  std::vector<std::string> tips;
  if (mus.voicing_available && !mus.voicing_balanced) {
    tips.push_back("Soften LH accompaniment so the melody sings");
  }
  if (mus.sustain_available && mus.sustain_hold_rate < 0.75) {
    tips.push_back("Hold half and tied notes longer");
  }
  if (mus.pedal_available && mus.pedal_clean_rate < 0.75) {
    tips.push_back("Lift the damper pedal cleanly on chord changes");
  }
  for (std::size_t i = 0; i < tips.size(); ++i) {
    if (i > 0U) {
      mus.coach_tip += " · ";
    }
    mus.coach_tip += tips[i];
  }

  return LessonRunSummary{
      .lesson_id = score_.meta.id,
      .practice_mode = config_.practice_mode,
      .hand_mode = config_.hand_mode,
      .is_full_piece = is_full_piece_selected(),
      .effective_bpm = effective_bpm(),
      .accuracy = accuracy(),
      .hits = hits_,
      .misses = misses_,
      .phrase_index = config_.phrase_index,
      .partial_hits = partial_hits_,
      .bar_diagnostics = std::move(diagnostics),
      .musicality = std::move(mus),
  };
}

double LessonEngine::effective_bpm() const noexcept {
  return compute_ladder_bpm(score_.meta.target_bpm, config_.tempo_step, config_.custom_bpm);
}

std::size_t LessonEngine::current_bar_in_slice() const noexcept {
  if (slice_bar_start_ticks_.empty()) {
    return 0;
  }
  for (std::size_t i = slice_bar_start_ticks_.size(); i-- > 0;) {
    if (elapsed_ticks_ + 1e-6 >= static_cast<double>(slice_bar_start_ticks_[i])) {
      return i;
    }
  }
  return 0;
}

double LessonEngine::playhead_beat_in_bar(std::size_t local_bar_idx) const noexcept {
  if (local_bar_idx >= slice_bar_start_ticks_.size()) {
    return 0.0;
  }
  const auto start = static_cast<double>(slice_bar_start_ticks_[local_bar_idx]);
  const double beat_ticks_d =
      static_cast<double>(std::max(1, slice_bar_meters_[local_bar_idx].beat_ticks()));
  return std::max(0.0, (elapsed_ticks_ - start) / beat_ticks_d);
}

double LessonEngine::playhead_global_beat() const noexcept {
  if (slice_bar_start_ticks_.empty()) {
    return 0.0;
  }
  double beats = 0.0;
  for (std::size_t i = 0; i < slice_bar_meters_.size(); ++i) {
    const auto start = static_cast<double>(slice_bar_start_ticks_[i]);
    const int bar_ticks =
        (i < slice_bar_ticks_.size()) ? slice_bar_ticks_[i] : slice_bar_meters_[i].measure_ticks();
    const auto len = static_cast<double>(std::max(1, bar_ticks));
    const double beat_ticks_d = static_cast<double>(std::max(1, slice_bar_meters_[i].beat_ticks()));
    const double bar_beats = len / beat_ticks_d;
    if (elapsed_ticks_ <= start + len || i + 1U == slice_bar_meters_.size()) {
      const double in_bar = std::clamp((elapsed_ticks_ - start) / beat_ticks_d, 0.0, bar_beats);
      return beats + in_bar;
    }
    beats += bar_beats;
  }
  return beats;
}

int LessonEngine::count_in_beats_remaining() const noexcept {
  return count_in_remaining_;
}

bool LessonEngine::is_mid_bar() const noexcept {
  if (playback_state_ != LessonPlaybackState::Playing) {
    return false;
  }
  const std::size_t b = current_bar_in_slice();
  if (b >= slice_bar_meters_.size()) {
    return false;
  }
  const double beat = playhead_beat_in_bar(b);
  const int bar_ticks =
      (b < slice_bar_ticks_.size()) ? slice_bar_ticks_[b] : slice_bar_meters_[b].measure_ticks();
  const double beat_ticks_d = static_cast<double>(std::max(1, slice_bar_meters_[b].beat_ticks()));
  const double pulses = static_cast<double>(bar_ticks) / beat_ticks_d;
  return beat > 0.25 && beat < (pulses - 0.15);
}

double LessonEngine::accuracy() const noexcept {
  const std::uint32_t total = hits_ + partial_hits_ + misses_;
  if (total == 0U) {
    return 0.0;
  }
  return (static_cast<double>(hits_) + 0.5 * static_cast<double>(partial_hits_)) /
         static_cast<double>(total);
}

} // namespace selfano::core

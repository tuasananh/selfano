#include "core/lesson_engine.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

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
              }
            }
          }
        }
        target.expected_midi = normalize_midi_set(target.expected_midi);
        out_targets.push_back(std::move(target));
      }
    } else {
      const auto& lm = score.measures[b];
      std::map<int, std::vector<LessonNote>> tick_notes;
      auto collect = [&](std::span<const LessonEvent> evs) {
        for (const auto& ev : evs) {
          if (ev.is_rest) {
            continue;
          }
          for (const auto& n : ev.notes) {
            if (!n.tie_stop) {
              tick_notes[ev.start_tick].push_back(n);
            }
          }
        }
      };
      if (config.hand_mode == LessonHandMode::RightHand ||
          config.hand_mode == LessonHandMode::HandsTogether) {
        collect(lm.treble_events);
      }
      if (config.hand_mode == LessonHandMode::LeftHand ||
          config.hand_mode == LessonHandMode::HandsTogether) {
        collect(lm.bass_events);
      }
      for (const auto& [tick, notes] : tick_notes) {
        if (notes.empty()) {
          continue;
        }
        LessonTarget target{};
        target.bar_index = b;
        target.tick_in_bar = tick;
        target.global_tick = acc_ticks + tick;
        for (const auto& n : notes) {
          target.expected_midi.push_back(n.midi);
          target.expected_spelled.push_back(n.spelled);
        }
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

void LessonEngine::load_score(LessonScore score, LessonConfig cfg) {
  score_ = std::move(score);
  config_ = cfg;
  phrases_ = effective_phrases(score_);
  if (config_.phrase_index.has_value() && *config_.phrase_index >= phrases_.size()) {
    config_.phrase_index.reset();
  }
  rebuild_slice_and_targets();
  reset_transport_state();
}

void LessonEngine::configure(LessonConfig cfg) {
  config_ = cfg;
  if (config_.phrase_index.has_value() && *config_.phrase_index >= phrases_.size()) {
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
}

void LessonEngine::set_phrase_index(std::optional<std::size_t> phrase_index) {
  if (phrase_index.has_value() && *phrase_index >= phrases_.size()) {
    phrase_index.reset();
  }
  if (config_.phrase_index != phrase_index) {
    config_.phrase_index = phrase_index;
    rebuild_slice_and_targets();
    reset_transport_state();
  }
}

void LessonEngine::set_loop_phrase(bool loop) {
  config_.loop_phrase = loop;
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
  misses_ = 0;
  scheduled_playback_notes_.clear();
  next_playback_note_idx_ = 0;
  for (auto& t : targets_) {
    t.status = LessonTargetStatus::Pending;
    t.played_midi.clear();
  }
}

void LessonEngine::rebuild_slice_and_targets() {
  targets_.clear();
  skeleton_measures_.clear();
  display_measures_.clear();
  slice_bar_start_ticks_.clear();
  slice_bar_ticks_.clear();
  slice_bar_meters_.clear();
  total_slice_ticks_ = 0;
  slice_start_bar_ = 0;
  slice_end_bar_ = 0;

  if (score_.measures.empty()) {
    return;
  }

  if (config_.phrase_index.has_value() && *config_.phrase_index < phrases_.size()) {
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
}

std::vector<LessonEngine::ScheduledTickNote>
LessonEngine::build_slice_tick_notes(bool include_treble, bool include_bass,
                                     std::uint8_t treble_vel, std::uint8_t bass_vel) const {
  std::vector<ScheduledTickNote> out;
  if (score_.measures.empty() || slice_bar_meters_.empty()) {
    return out;
  }

  struct OpenTie {
    int staff_id{0};
    MidiPitch midi{60};
    std::uint8_t velocity{84};
    int start_tick{0};
    int duration_ticks{0};
  };
  std::vector<OpenTie> open_ties;

  const auto& source_measures = (config_.practice_mode == LessonPracticeMode::ChordSkeleton)
                                    ? skeleton_measures_
                                    : score_.measures;

  int bar_start_tick = 0;
  for (std::size_t local_b = 0; local_b < slice_bar_meters_.size(); ++local_b) {
    const std::size_t b = slice_start_bar_ + local_b;
    const auto& lm = source_measures[b];
    const TimeSignature meter = slice_bar_meters_[local_b];

    auto process_staff = [&](std::span<const LessonEvent> evs, int staff_id, std::uint8_t vel) {
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
        const int ev_start_tick = bar_start_tick + ev->start_tick;
        const int ev_dur_ticks = std::max(1, ev->duration.ticks());
        for (const auto& n : ev->notes) {
          if (n.tie_stop) {
            auto it = std::ranges::find_if(open_ties, [&](const OpenTie& ot) {
              return ot.staff_id == staff_id && ot.midi == n.midi;
            });
            if (it != open_ties.end()) {
              it->duration_ticks += ev_dur_ticks;
              if (!n.tie_start) {
                out.push_back(ScheduledTickNote{
                    .midi = it->midi,
                    .velocity = it->velocity,
                    .start_tick = it->start_tick,
                    .duration_ticks = it->duration_ticks,
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
                .velocity = vel,
                .start_tick = ev_start_tick,
                .duration_ticks = ev_dur_ticks,
            });
          } else {
            out.push_back(ScheduledTickNote{
                .midi = n.midi,
                .velocity = vel,
                .start_tick = ev_start_tick,
                .duration_ticks = ev_dur_ticks,
                .channel = 0,
            });
          }
        }
      }
    };

    if (include_treble) {
      process_staff(lm.treble_events, 0, treble_vel);
    }
    if (include_bass) {
      process_staff(lm.bass_events, 1, bass_vel);
    }

    bar_start_tick += effective_measure_ticks(lm, meter);
  }

  for (const auto& ot : open_ties) {
    out.push_back(ScheduledTickNote{
        .midi = ot.midi,
        .velocity = ot.velocity,
        .start_tick = ot.start_tick,
        .duration_ticks = ot.duration_ticks,
        .channel = 0,
    });
  }

  std::ranges::sort(out, [](const ScheduledTickNote& a, const ScheduledTickNote& b) {
    if (a.start_tick != b.start_tick) {
      return a.start_tick < b.start_tick;
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
        .start_ms = 0.0,
        .duration_ms = slice_ticks_to_ms(n.start_tick, n.duration_ticks),
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
    while (active_target_index_ < targets_.size()) {
      if (targets_[active_target_index_].status == LessonTargetStatus::Pending) {
        targets_[active_target_index_].status = LessonTargetStatus::Missed;
        ++misses_;
      }
      ++active_target_index_;
    }
    ++completed_runs_;
    out.phrase_completed = true;
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
      emit_due_playback_notes(elapsed_ticks_, out.playback_notes);
      for (auto& t : targets_) {
        t.status = LessonTargetStatus::Pending;
        t.played_midi.clear();
      }
    } else {
      elapsed_ticks_ = static_cast<double>(total_slice_ticks_);
      playback_state_ = LessonPlaybackState::Completed;
      out.run_completed = true;
    }
  }

  return out;
}

LessonSubmitResult LessonEngine::submit_played_notes(std::span<const MidiPitch> played_midi) {
  LessonSubmitResult res{};
  const auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty() || targets_.empty()) {
    return res;
  }

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
      ++hits_;
      while (active_target_index_ < targets_.size() &&
             targets_[active_target_index_].status != LessonTargetStatus::Pending) {
        ++active_target_index_;
      }
      res.matched = true;
    } else {
      tgt.status = LessonTargetStatus::Missed;
      tgt.played_midi = normalized;
      ++misses_;
      while (active_target_index_ < targets_.size() &&
             targets_[active_target_index_].status != LessonTargetStatus::Pending) {
        ++active_target_index_;
      }
      res.wrong_note = true;
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
    ++active_target_index_;
    res.matched = true;
    if (active_target_index_ >= targets_.size()) {
      ++completed_runs_;
      res.phrase_completed = true;
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

  double bar_start_ms = 0.0;
  for (std::size_t local_b = 0; local_b < slice_bar_meters_.size(); ++local_b) {
    const std::size_t b = slice_start_bar_ + local_b;
    const auto& lm = source_measures[b];
    const TimeSignature meter = slice_bar_meters_[local_b];
    const double ms_per_tick = ms_per_beat / static_cast<double>(std::max(1, meter.beat_ticks()));

    auto process_staff = [&](std::span<const LessonEvent> evs, int staff_id, std::uint8_t vel) {
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
        const double ev_start_ms = bar_start_ms + static_cast<double>(ev->start_tick) * ms_per_tick;
        const double ev_dur_ms = static_cast<double>(ev->duration.ticks()) * ms_per_tick;
        for (const auto& n : ev->notes) {
          if (n.tie_stop) {
            auto it = std::ranges::find_if(open_ties, [&](const OpenTie& ot) {
              return ot.staff_id == staff_id && ot.midi == n.midi;
            });
            if (it != open_ties.end()) {
              it->duration_ms += ev_dur_ms;
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
                .velocity = vel,
                .start_ms = ev_start_ms,
                .duration_ms = ev_dur_ms,
            });
          } else {
            out.push_back(LessonPlaybackNote{
                .midi = n.midi,
                .velocity = vel,
                .start_ms = ev_start_ms,
                .duration_ms = std::max(40.0, ev_dur_ms),
                .channel = 0,
            });
          }
        }
      }
    };

    if (config_.hand_mode == LessonHandMode::RightHand ||
        config_.hand_mode == LessonHandMode::HandsTogether) {
      process_staff(lm.treble_events, 0, 92);
    }
    if (config_.hand_mode == LessonHandMode::LeftHand ||
        config_.hand_mode == LessonHandMode::HandsTogether) {
      process_staff(lm.bass_events, 1, 78);
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
  const std::uint32_t total = hits_ + misses_;
  if (total == 0U) {
    return 0.0;
  }
  return static_cast<double>(hits_) / static_cast<double>(total);
}

} // namespace selfano::core

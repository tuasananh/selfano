#include "core/practice_transforms.hpp"

#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

void emit_split_event(std::vector<LessonEvent>& out, int start_tick, int total_ticks, bool is_rest,
                      std::uint8_t voice, std::uint8_t articulations,
                      std::span<const LessonNote> notes_template) {
  const auto vals = split_ticks_to_values(total_ticks);
  int t = start_tick;
  for (std::size_t i = 0; i < vals.size(); ++i) {
    LessonEvent ev{};
    ev.start_tick = t;
    ev.duration = vals[i];
    ev.is_rest = is_rest;
    ev.voice = voice;
    ev.articulations = articulations;
    if (!is_rest) {
      ev.notes.assign(notes_template.begin(), notes_template.end());
      const bool tie_s = (i + 1U < vals.size());
      const bool tie_p = (i > 0U);
      for (auto& n : ev.notes) {
        n.tie_start = tie_s;
        n.tie_stop = tie_p;
      }
    }
    out.push_back(std::move(ev));
    t += vals[i].ticks();
  }
}

void reconcile_ties_and_voice0(std::vector<LessonMeasure>& measures, TimeSignature initial_meter) {
  auto reconcile_staff = [&](int staff_id) {
    struct TieRef {
      std::size_t m_idx{0};
      std::size_t e_idx{0};
      std::size_t n_idx{0};
      MidiPitch midi{60};
    };
    std::vector<TieRef> open_ties;
    for (std::size_t mi = 0; mi < measures.size(); ++mi) {
      auto& evs = (staff_id == 0) ? measures[mi].treble_events : measures[mi].bass_events;
      std::vector<std::size_t> order(evs.size());
      for (std::size_t i = 0; i < evs.size(); ++i) {
        order[i] = i;
      }
      std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
        if (evs[a].start_tick != evs[b].start_tick) {
          return evs[a].start_tick < evs[b].start_tick;
        }
        return evs[a].voice < evs[b].voice;
      });
      for (const std::size_t ei : order) {
        auto& ev = evs[ei];
        for (std::size_t ni = 0; ni < ev.notes.size(); ++ni) {
          auto& n = ev.notes[ni];
          if (n.tie_stop) {
            const auto it = std::ranges::find_if(
                open_ties, [&](const TieRef& tr) { return tr.midi == n.midi; });
            if (it == open_ties.end()) {
              n.tie_stop = false;
            } else {
              open_ties.erase(it);
            }
          }
        }
        for (std::size_t ni = 0; ni < ev.notes.size(); ++ni) {
          if (ev.notes[ni].tie_start) {
            open_ties.push_back(TieRef{mi, ei, ni, ev.notes[ni].midi});
          }
        }
      }
    }
    for (const auto& tr : open_ties) {
      auto& evs =
          (staff_id == 0) ? measures[tr.m_idx].treble_events : measures[tr.m_idx].bass_events;
      evs[tr.e_idx].notes[tr.n_idx].tie_start = false;
    }
  };

  TimeSignature cur_meter = initial_meter;
  for (auto& m : measures) {
    if (m.meter_change.has_value()) {
      cur_meter = *m.meter_change;
    }
    const int mticks = effective_measure_ticks(m, cur_meter);
    auto ensure_voice0 = [&](std::vector<LessonEvent>& evs) {
      bool has_v0 = false;
      for (const auto& ev : evs) {
        if (ev.voice == 0U) {
          has_v0 = true;
          break;
        }
      }
      if (!has_v0) {
        emit_split_event(evs, 0, mticks, true, 0U, 0U, {});
      }
    };
    ensure_voice0(m.treble_events);
    ensure_voice0(m.bass_events);
  }

  reconcile_staff(0);
  reconcile_staff(1);
}

struct TimelineEvent {
  int start_tick{0};
  int duration_ticks{480};
  bool is_rest{false};
  std::uint8_t voice{0};
  std::uint8_t articulations{0};
  std::vector<LessonNote> notes;
};

[[nodiscard]] std::vector<TimelineEvent>
transform_dotted_voice(std::span<const LessonEvent> voice_events, int scale, bool long_short) {
  std::vector<TimelineEvent> out;
  out.reserve(voice_events.size());
  for (const auto& ev : voice_events) {
    out.push_back(TimelineEvent{
        .start_tick = ev.start_tick * scale,
        .duration_ticks = ev.event_ticks() * scale,
        .is_rest = ev.is_rest,
        .voice = ev.voice,
        .articulations = ev.articulations,
        .notes = ev.notes,
    });
  }
  std::ranges::sort(out, [](const TimelineEvent& a, const TimelineEvent& b) {
    return a.start_tick < b.start_tick;
  });

  const int target_pair_dur = (scale == 2) ? 240 : 240; // 16th*2 = 240, 8th*1 = 240
  bool altered = false;
  for (std::size_t i = 0; i + 1U < out.size();) {
    auto& a = out[i];
    auto& b = out[i + 1U];
    if (!a.is_rest && !b.is_rest && a.duration_ticks == target_pair_dur &&
        b.duration_ticks == target_pair_dur && a.start_tick + a.duration_ticks == b.start_tick) {
      const int long_dur = (target_pair_dur * 3) / 2;
      const int short_dur = target_pair_dur / 2;
      if (long_short) {
        a.duration_ticks = long_dur;
        b.start_tick = a.start_tick + long_dur;
        b.duration_ticks = short_dur;
      } else {
        a.duration_ticks = short_dur;
        b.start_tick = a.start_tick + short_dur;
        b.duration_ticks = long_dur;
      }
      altered = true;
      i += 2U;
    } else {
      ++i;
    }
  }

  // Fallback for 2x grid: also dot 8th pairs (480+480 -> 720+240) if no 16ths were paired
  if (!altered && scale == 1) {
    for (std::size_t i = 0; i + 1U < out.size();) {
      auto& a = out[i];
      auto& b = out[i + 1U];
      if (!a.is_rest && !b.is_rest && a.duration_ticks == 480 && b.duration_ticks == 480 &&
          a.start_tick + 480 == b.start_tick) {
        if (long_short) {
          a.duration_ticks = 720;
          b.start_tick = a.start_tick + 720;
          b.duration_ticks = 240;
        } else {
          a.duration_ticks = 240;
          b.start_tick = a.start_tick + 240;
          b.duration_ticks = 720;
        }
        i += 2U;
      } else {
        ++i;
      }
    }
  } else if (!altered && scale == 2) {
    for (std::size_t i = 0; i + 1U < out.size();) {
      auto& a = out[i];
      auto& b = out[i + 1U];
      if (!a.is_rest && !b.is_rest && a.duration_ticks == 480 && b.duration_ticks == 480 &&
          a.start_tick + 480 == b.start_tick) {
        if (long_short) {
          a.duration_ticks = 720;
          b.start_tick = a.start_tick + 720;
          b.duration_ticks = 240;
        } else {
          a.duration_ticks = 240;
          b.start_tick = a.start_tick + 240;
          b.duration_ticks = 720;
        }
        i += 2U;
      } else {
        ++i;
      }
    }
  }
  return out;
}

void split_timeline_into_bars(std::span<const TimelineEvent> events, int mticks,
                              std::vector<LessonEvent>& bar0_out,
                              std::vector<LessonEvent>& bar1_out) {
  for (const auto& te : events) {
    const int s = te.start_tick;
    const int e = te.start_tick + te.duration_ticks;
    if (e <= mticks) {
      emit_split_event(bar0_out, s, te.duration_ticks, te.is_rest, te.voice, te.articulations,
                       te.notes);
    } else if (s >= mticks) {
      emit_split_event(bar1_out, s - mticks, te.duration_ticks, te.is_rest, te.voice,
                       te.articulations, te.notes);
    } else {
      const int left_dur = mticks - s;
      const int right_dur = e - mticks;
      emit_split_event(bar0_out, s, left_dur, te.is_rest, te.voice, te.articulations, te.notes);
      emit_split_event(bar1_out, 0, right_dur, te.is_rest, te.voice, te.articulations, te.notes);
      if (!te.is_rest && !bar0_out.empty() && !bar1_out.empty()) {
        for (auto& n : bar0_out.back().notes) {
          n.tie_start = true;
        }
        for (auto& n : bar1_out.back().notes) {
          n.tie_stop = true;
        }
      }
    }
  }
}

[[nodiscard]] TransformedSlice apply_dotted(std::span<const LessonMeasure> src,
                                            TimeSignature initial_meter, bool long_short) {
  TransformedSlice res{};
  TimeSignature cur_meter = initial_meter;
  std::uint32_t next_bar_num = src.empty() ? 1U : src.front().bar_number;

  for (std::size_t si = 0; si < src.size(); ++si) {
    const auto& m = src[si];
    if (m.meter_change.has_value()) {
      cur_meter = *m.meter_change;
    }
    const int mticks = effective_measure_ticks(m, cur_meter);

    auto has_adjacent_16ths = [](std::span<const LessonEvent> evs) noexcept {
      for (std::size_t i = 0; i + 1U < evs.size(); ++i) {
        if (!evs[i].is_rest && !evs[i + 1U].is_rest && evs[i].voice == evs[i + 1U].voice &&
            evs[i].duration.base == NoteBaseDuration::Sixteenth &&
            evs[i + 1U].duration.base == NoteBaseDuration::Sixteenth) {
          return true;
        }
      }
      return false;
    };

    const bool need_2x = has_adjacent_16ths(m.treble_events) || has_adjacent_16ths(m.bass_events);
    const int scale = need_2x ? 2 : 1;

    auto process_staff = [&](std::span<const LessonEvent> evs, std::vector<LessonEvent>& out0,
                             std::vector<LessonEvent>* out1) {
      std::map<std::uint8_t, std::vector<LessonEvent>> by_voice;
      for (const auto& ev : evs) {
        by_voice[ev.voice].push_back(ev);
      }
      for (const auto& [v, vevs] : by_voice) {
        (void)v;
        const auto tl = transform_dotted_voice(vevs, scale, long_short);
        if (scale == 1) {
          for (const auto& te : tl) {
            emit_split_event(out0, te.start_tick, te.duration_ticks, te.is_rest, te.voice,
                             te.articulations, te.notes);
          }
        } else if (out1 != nullptr) {
          split_timeline_into_bars(tl, mticks, out0, *out1);
        }
      }
    };

    if (scale == 1) {
      LessonMeasure out_m = m;
      out_m.bar_number = next_bar_num++;
      out_m.treble_events.clear();
      out_m.bass_events.clear();
      process_staff(m.treble_events, out_m.treble_events, nullptr);
      process_staff(m.bass_events, out_m.bass_events, nullptr);
      res.measures.push_back(std::move(out_m));
      res.source_bar_offsets.push_back(si);
    } else {
      LessonMeasure m0{};
      m0.bar_number = next_bar_num++;
      m0.pickup_ticks = m.pickup_ticks;
      m0.key_change = m.key_change;
      m0.meter_change = m.meter_change;

      LessonMeasure m1{};
      m1.bar_number = next_bar_num++;
      m1.pickup_ticks = m.pickup_ticks;

      process_staff(m.treble_events, m0.treble_events, &m1.treble_events);
      process_staff(m.bass_events, m0.bass_events, &m1.bass_events);

      for (const auto& ha : m.harmony) {
        const int dt = ha.start_tick * 2;
        if (dt < mticks) {
          m0.harmony.push_back({dt, ha.chord_symbol, ha.roman_numeral});
        } else {
          m1.harmony.push_back(
              {std::min(mticks - 1, dt - mticks), ha.chord_symbol, ha.roman_numeral});
        }
      }
      for (const auto& dyn : m.dynamics) {
        const int dt = dyn.start_tick * 2;
        if (dt < mticks) {
          m0.dynamics.push_back({dt, dyn.level});
        } else {
          m1.dynamics.push_back({std::min(mticks - 1, dt - mticks), dyn.level});
        }
      }
      res.measures.push_back(std::move(m0));
      res.source_bar_offsets.push_back(si);
      res.measures.push_back(std::move(m1));
      res.source_bar_offsets.push_back(si);
    }
  }

  reconcile_ties_and_voice0(res.measures, initial_meter);
  return res;
}

[[nodiscard]] TransformedSlice apply_burst_4_plus_1(std::span<const LessonMeasure> src) {
  TransformedSlice res{};
  bool has_16ths = false;
  for (const auto& m : src) {
    for (const auto* evs : {&m.treble_events, &m.bass_events}) {
      for (const auto& ev : *evs) {
        if (!ev.is_rest && ev.duration.base == NoteBaseDuration::Sixteenth) {
          has_16ths = true;
          break;
        }
      }
    }
    if (has_16ths) {
      break;
    }
  }

  const NoteBaseDuration fast_base =
      has_16ths ? NoteBaseDuration::Sixteenth : NoteBaseDuration::Eighth;
  const int sub_ticks = has_16ths ? 120 : 240;

  struct Onset {
    std::size_t src_idx{0};
    std::vector<LessonNote> treble;
    std::vector<LessonNote> bass;
    std::optional<HarmonicAnnotation> harmony;
  };
  std::vector<Onset> onsets;

  for (std::size_t si = 0; si < src.size(); ++si) {
    const auto& m = src[si];
    std::map<int, Onset> bar_map;
    for (const auto& ev : m.treble_events) {
      if (ev.is_rest || ev.voice != 0U) {
        continue;
      }
      for (const auto& n : ev.notes) {
        if (!n.tie_stop) {
          LessonNote clean = n;
          clean.tie_start = false;
          clean.tie_stop = false;
          bar_map[ev.start_tick].src_idx = si;
          bar_map[ev.start_tick].treble.push_back(clean);
        }
      }
    }
    for (const auto& ev : m.bass_events) {
      if (ev.is_rest || ev.voice != 0U) {
        continue;
      }
      for (const auto& n : ev.notes) {
        if (!n.tie_stop) {
          LessonNote clean = n;
          clean.tie_start = false;
          clean.tie_stop = false;
          bar_map[ev.start_tick].src_idx = si;
          bar_map[ev.start_tick].bass.push_back(clean);
        }
      }
    }
    for (const auto& ha : m.harmony) {
      if (auto it = bar_map.find(ha.start_tick); it != bar_map.end()) {
        it->second.harmony = ha;
      }
    }
    for (auto& [tick, on] : bar_map) {
      (void)tick;
      if (!on.treble.empty() || !on.bass.empty()) {
        onsets.push_back(std::move(on));
      }
    }
  }

  if (onsets.empty()) {
    res.measures.assign(src.begin(), src.end());
    for (std::size_t i = 0; i < src.size(); ++i) {
      res.source_bar_offsets.push_back(i);
    }
    return res;
  }

  const TimeSignature meter44{.numerator = 4, .denominator = 4};
  std::uint32_t bar_num = 1U;
  for (std::size_t idx = 0; idx < onsets.size();) {
    const std::size_t take = std::min<std::size_t>(5U, onsets.size() - idx);
    LessonMeasure bm{};
    bm.bar_number = bar_num++;
    if (idx == 0U) {
      bm.meter_change = meter44;
    }
    if (idx == 0U && !src.empty() && src.front().key_change.has_value()) {
      bm.key_change = src.front().key_change;
    }

    int cur_tick = 0;
    for (std::size_t k = 0; k < take; ++k) {
      const auto& on = onsets[idx + k];
      const bool is_arrival = (k + 1U == take);
      const RhythmicValue rv = is_arrival ? RhythmicValue{NoteBaseDuration::Quarter, false}
                                          : RhythmicValue{fast_base, false};

      if (k == 0U && on.harmony.has_value()) {
        bm.harmony.push_back({0, on.harmony->chord_symbol, on.harmony->roman_numeral});
      }
      if (!on.treble.empty()) {
        bm.treble_events.push_back(LessonEvent{.start_tick = cur_tick,
                                               .duration = rv,
                                               .is_rest = false,
                                               .voice = 0,
                                               .notes = on.treble});
      } else {
        bm.treble_events.push_back(LessonEvent{
            .start_tick = cur_tick, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
      }
      if (!on.bass.empty()) {
        bm.bass_events.push_back(LessonEvent{.start_tick = cur_tick,
                                             .duration = rv,
                                             .is_rest = false,
                                             .voice = 0,
                                             .notes = on.bass});
      } else {
        bm.bass_events.push_back(LessonEvent{
            .start_tick = cur_tick, .duration = rv, .is_rest = true, .voice = 0, .notes = {}});
      }
      cur_tick += rv.ticks();
    }

    const int rem = meter44.measure_ticks() - cur_tick;
    if (rem > 0) {
      emit_split_event(bm.treble_events, cur_tick, rem, true, 0U, 0U, {});
      emit_split_event(bm.bass_events, cur_tick, rem, true, 0U, 0U, {});
    }

    res.measures.push_back(std::move(bm));
    res.source_bar_offsets.push_back(onsets[idx].src_idx);
    idx += take;
    if (take < 5U) {
      break;
    }
  }
  (void)sub_ticks;
  return res;
}

void assign_blocked_fingers(std::vector<LessonNote>& notes, bool treble) {
  std::ranges::sort(notes,
                    [](const LessonNote& a, const LessonNote& b) { return a.midi < b.midi; });
  const std::size_t n = notes.size();
  if (n == 0U) {
    return;
  }
  if (n == 1U) {
    notes[0].finger = treble ? 3U : 3U;
    return;
  }
  if (n == 2U) {
    notes[0].finger = treble ? 1U : 5U;
    notes[1].finger = treble ? 5U : 1U;
    return;
  }
  if (n == 3U) {
    if (treble) {
      notes[0].finger = 1U;
      notes[1].finger =
          (notes[1].midi - notes[0].midi <= 4 && notes[2].midi - notes[1].midi >= 5) ? 2U : 3U;
      notes[2].finger = 5U;
    } else {
      notes[0].finger = 5U;
      notes[1].finger =
          (notes[2].midi - notes[1].midi <= 4 && notes[1].midi - notes[0].midi >= 5) ? 2U : 3U;
      notes[2].finger = 1U;
    }
    return;
  }
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t f = static_cast<std::uint8_t>(
        std::clamp<std::size_t>(1U + (i * 4U) / std::max<std::size_t>(1U, n - 1U), 1U, 5U));
    notes[i].finger = treble ? f : static_cast<std::uint8_t>(6U - f);
  }
}

void block_staff_measure(std::span<const LessonEvent> src_evs, std::span<const int> splits,
                         bool treble, std::vector<LessonEvent>& out_evs) {
  for (const auto& ev : src_evs) {
    if (ev.voice > 0U) {
      out_evs.push_back(ev);
    }
  }

  for (std::size_t si = 0; si + 1U < splits.size(); ++si) {
    const int seg_s = splits[si];
    const int seg_e = splits[si + 1U];
    const int seg_dur = seg_e - seg_s;
    if (seg_dur <= 0) {
      continue;
    }

    std::vector<const LessonEvent*> seg_notes;
    for (const auto& ev : src_evs) {
      if (ev.voice == 0U && !ev.is_rest && ev.start_tick >= seg_s && ev.start_tick < seg_e) {
        seg_notes.push_back(&ev);
      }
    }

    if (seg_notes.size() <= 1U) {
      if (seg_notes.empty()) {
        emit_split_event(out_evs, seg_s, seg_dur, true, 0U, 0U, {});
      } else {
        if (seg_notes.front()->start_tick > seg_s) {
          emit_split_event(out_evs, seg_s, seg_notes.front()->start_tick - seg_s, true, 0U, 0U, {});
        }
        const int rem = seg_e - seg_notes.front()->start_tick;
        emit_split_event(out_evs, seg_notes.front()->start_tick, rem, false, 0U,
                         seg_notes.front()->articulations, seg_notes.front()->notes);
      }
      continue;
    }

    std::vector<LessonNote> block;
    std::array<bool, 12> seen_pc{};
    int min_m = 127;
    int max_m = 0;
    for (const LessonEvent* ev : seg_notes) {
      for (const auto& n : ev->notes) {
        const int m = static_cast<int>(n.midi);
        const int pc = m % 12;
        if (seen_pc[static_cast<std::size_t>(pc)]) {
          continue;
        }
        const int cand_min = block.empty() ? m : std::min(min_m, m);
        const int cand_max = block.empty() ? m : std::max(max_m, m);
        if (cand_max - cand_min > 12) {
          continue;
        }
        seen_pc[static_cast<std::size_t>(pc)] = true;
        min_m = cand_min;
        max_m = cand_max;
        LessonNote clean = n;
        clean.tie_start = false;
        clean.tie_stop = false;
        block.push_back(clean);
      }
    }
    assign_blocked_fingers(block, treble);
    emit_split_event(out_evs, seg_s, seg_dur, block.empty(), 0U, 0U, block);
  }
}

[[nodiscard]] TransformedSlice apply_hand_blocking(std::span<const LessonMeasure> src,
                                                   TimeSignature initial_meter) {
  TransformedSlice res{};
  TimeSignature cur_meter = initial_meter;
  for (std::size_t si = 0; si < src.size(); ++si) {
    const auto& m = src[si];
    if (m.meter_change.has_value()) {
      cur_meter = *m.meter_change;
    }
    const int mticks = effective_measure_ticks(m, cur_meter);
    std::vector<int> splits{0, mticks};
    if (mticks >= 960 && mticks % 2 == 0) {
      splits.push_back(mticks / 2);
    }
    for (const auto& ha : m.harmony) {
      if (ha.start_tick > 0 && ha.start_tick < mticks) {
        splits.push_back(ha.start_tick);
      }
    }
    std::ranges::sort(splits);
    splits.erase(std::unique(splits.begin(), splits.end()), splits.end());

    LessonMeasure out_m = m;
    out_m.treble_events.clear();
    out_m.bass_events.clear();
    block_staff_measure(m.treble_events, splits, true, out_m.treble_events);
    block_staff_measure(m.bass_events, splits, false, out_m.bass_events);
    res.measures.push_back(std::move(out_m));
    res.source_bar_offsets.push_back(si);
  }
  reconcile_ties_and_voice0(res.measures, initial_meter);
  return res;
}

} // namespace

TransformedSlice apply_practice_transform(std::span<const LessonMeasure> source_measures,
                                          TimeSignature initial_meter,
                                          PracticeTransform transform) {
  switch (transform) {
  case PracticeTransform::Straight: {
    TransformedSlice res{};
    res.measures.assign(source_measures.begin(), source_measures.end());
    res.source_bar_offsets.reserve(source_measures.size());
    for (std::size_t i = 0; i < source_measures.size(); ++i) {
      res.source_bar_offsets.push_back(i);
    }
    return res;
  }
  case PracticeTransform::DottedLongShort:
    return apply_dotted(source_measures, initial_meter, true);
  case PracticeTransform::DottedShortLong:
    return apply_dotted(source_measures, initial_meter, false);
  case PracticeTransform::Burst4Plus1:
    return apply_burst_4_plus_1(source_measures);
  case PracticeTransform::HandBlocking:
    return apply_hand_blocking(source_measures, initial_meter);
  }
  return {};
}

} // namespace selfano::core

#include "core/staff_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace selfano::core {

std::vector<int> compute_ledger_line_steps(int step, Clef clef) {
  std::vector<int> steps;
  const int bottom = staff_bottom_step(clef);
  const int top = staff_top_step(clef);

  if (step <= bottom - 2) {
    for (int s = bottom - 2; s >= step; s -= 2) {
      steps.push_back(s);
    }
  } else if (step >= top + 2) {
    for (int s = top + 2; s <= step; s += 2) {
      steps.push_back(s);
    }
  }
  return steps;
}

MeasureAccidentalState::MeasureAccidentalState(KeySignature key) : key_{key} {
}

void MeasureAccidentalState::reset(KeySignature key) {
  key_ = key;
  active_overrides_.clear();
}

void MeasureAccidentalState::reset_measure() {
  active_overrides_.clear();
}

std::optional<Accidental> MeasureAccidentalState::resolve_and_record(const SpelledNote& note) {
  const int step = diatonic_step(note);
  Accidental active = key_accidental_for(key_, note.letter);

  auto it = std::find_if(active_overrides_.begin(), active_overrides_.end(),
                         [step](const auto& entry) { return entry.first == step; });
  if (it != active_overrides_.end()) {
    active = it->second;
  }

  if (note.accidental == active) {
    return std::nullopt;
  }

  if (it != active_overrides_.end()) {
    it->second = note.accidental;
  } else {
    active_overrides_.emplace_back(step, note.accidental);
  }
  return note.accidental;
}

ChordLayout layout_chord(std::span<const ChordNoteLayoutInput> notes, Clef clef,
                         float notehead_width_spaces) {
  ChordLayout result{};
  if (notes.empty()) {
    result.min_notehead_x_spaces = 0.0f;
    result.max_notehead_x_spaces = 0.0f;
    return result;
  }

  // 1. Sort ascending by diatonic step (stable tie-break on source_index).
  std::vector<ChordNoteLayoutInput> sorted(notes.begin(), notes.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    if (a.step != b.step) {
      return a.step < b.step;
    }
    return a.source_index < b.source_index;
  });

  // 2. Determine stem direction relative to staff middle line.
  const int mid = staff_middle_step(clef);
  const int dist_below = mid - sorted.front().step;
  const int dist_above = sorted.back().step - mid;
  if (dist_above > dist_below) {
    result.stem_direction = StemDirection::Down;
  } else if (dist_below > dist_above) {
    result.stem_direction = StemDirection::Up;
  } else {
    int signed_sum = 0;
    for (const auto& n : sorted) {
      signed_sum += (n.step - mid);
    }
    result.stem_direction = (signed_sum >= 0) ? StemDirection::Down : StemDirection::Up;
  }

  // 3. Populate noteheads and resolve 2nd-interval offsets.
  result.noteheads.reserve(sorted.size());
  for (const auto& n : sorted) {
    result.noteheads.push_back(NoteheadLayout{
        .source_index = n.source_index,
        .step = n.step,
        .x_offset_spaces = 0.0f,
        .displayed_accidental = n.displayed_accidental,
        .accidental_column = 0,
    });
  }

  const float second_offset = notehead_width_spaces * kSecondOffsetRatio;
  if (result.stem_direction == StemDirection::Up) {
    for (std::size_t i = 0; i + 1 < result.noteheads.size(); ++i) {
      if (result.noteheads[i + 1].step - result.noteheads[i].step <= 1 &&
          result.noteheads[i].x_offset_spaces == 0.0f) {
        result.noteheads[i + 1].x_offset_spaces = second_offset;
      }
    }
  } else {
    for (std::size_t i = result.noteheads.size(); i-- > 1;) {
      if (result.noteheads[i].step - result.noteheads[i - 1].step <= 1 &&
          result.noteheads[i].x_offset_spaces == 0.0f) {
        result.noteheads[i - 1].x_offset_spaces = -second_offset;
      }
    }
  }

  // 4. Compute bounding horizontal extent of noteheads.
  result.min_notehead_x_spaces = result.noteheads[0].x_offset_spaces;
  result.max_notehead_x_spaces = result.noteheads[0].x_offset_spaces + notehead_width_spaces;
  for (const auto& nh : result.noteheads) {
    result.min_notehead_x_spaces = std::min(result.min_notehead_x_spaces, nh.x_offset_spaces);
    result.max_notehead_x_spaces =
        std::max(result.max_notehead_x_spaces, nh.x_offset_spaces + notehead_width_spaces);
  }

  // 5. Pack accidental columns (top-down, staggering when < 5 diatonic steps apart).
  std::vector<int> column_last_step;
  for (std::size_t i = result.noteheads.size(); i-- > 0;) {
    auto& nh = result.noteheads[i];
    if (!nh.displayed_accidental.has_value()) {
      continue;
    }
    int assigned_col = -1;
    for (std::size_t col = 0; col < column_last_step.size(); ++col) {
      if (std::abs(column_last_step[col] - nh.step) >= 5) {
        assigned_col = static_cast<int>(col);
        column_last_step[col] = nh.step;
        break;
      }
    }
    if (assigned_col < 0) {
      assigned_col = static_cast<int>(column_last_step.size());
      column_last_step.push_back(nh.step);
    }
    nh.accidental_column = assigned_col;
  }

  // 6. Union of ledger lines across all notes in chord, sorted ascending.
  for (const auto& nh : result.noteheads) {
    const auto lines = compute_ledger_line_steps(nh.step, clef);
    result.ledger_line_steps.insert(result.ledger_line_steps.end(), lines.begin(), lines.end());
  }
  std::sort(result.ledger_line_steps.begin(), result.ledger_line_steps.end());
  result.ledger_line_steps.erase(
      std::unique(result.ledger_line_steps.begin(), result.ledger_line_steps.end()),
      result.ledger_line_steps.end());

  // 7. Stem endpoints (1 octave past furthest note, or reaching staff middle line).
  if (result.stem_direction == StemDirection::Up) {
    result.stem_start_step = result.noteheads.front().step;
    result.stem_end_step = std::max(result.noteheads.back().step + 7, mid);
  } else {
    result.stem_start_step = result.noteheads.back().step;
    result.stem_end_step = std::min(result.noteheads.front().step - 7, mid);
  }

  return result;
}

namespace {

[[nodiscard]] constexpr bool is_beamable(const BeamEventInput& ev) noexcept {
  return !ev.is_rest && (ev.base_duration == NoteBaseDuration::Eighth ||
                         ev.base_duration == NoteBaseDuration::Sixteenth);
}

[[nodiscard]] int beam_bucket_ticks(TimeSignature meter, std::span<const BeamEventInput> events,
                                    int start_tick) noexcept {
  if (meter.is_compound()) {
    return meter.beat_ticks(); // 720 ticks (3 eighths in 6/8)
  }
  if (meter.numerator == 4 && meter.denominator == 4) {
    // Half-bar grouping [0, 960) and [960, 1920), unless 16ths appear in that half-bar,
    // in which case split per quarter-note beat (480) for crisp classical readability.
    const int half_idx = start_tick / (kTicksPerQuarter * 2);
    const int half_lo = half_idx * kTicksPerQuarter * 2;
    const int half_hi = half_lo + kTicksPerQuarter * 2;
    for (const auto& ev : events) {
      if (ev.start_tick >= half_lo && ev.start_tick < half_hi &&
          ev.base_duration == NoteBaseDuration::Sixteenth) {
        return kTicksPerQuarter;
      }
    }
    return kTicksPerQuarter * 2;
  }
  if (meter.numerator == 6 && meter.denominator == 4) {
    const int half_idx = start_tick / (kTicksPerQuarter * 3);
    const int half_lo = half_idx * kTicksPerQuarter * 3;
    const int half_hi = half_lo + kTicksPerQuarter * 3;
    for (const auto& ev : events) {
      if (ev.start_tick >= half_lo && ev.start_tick < half_hi &&
          ev.base_duration == NoteBaseDuration::Sixteenth) {
        return kTicksPerQuarter;
      }
    }
    return kTicksPerQuarter * 3;
  }
  return std::max(1, meter.beat_ticks());
}

[[nodiscard]] BeamGroupLayout build_beam_group(std::span<const BeamEventInput> run, Clef clef,
                                               float notehead_width_spaces) {
  BeamGroupLayout out{};
  const int bottom = staff_bottom_step(clef);
  const int mid = staff_middle_step(clef);

  auto step_to_spaces = [bottom](int step) noexcept {
    return static_cast<float>(step - bottom) * 0.5f;
  };

  int min_step = std::numeric_limits<int>::max();
  int max_step = std::numeric_limits<int>::min();
  int signed_sum = 0;
  for (const auto& ev : run) {
    min_step = std::min(min_step, ev.min_step);
    max_step = std::max(max_step, ev.max_step);
    signed_sum += (ev.min_step - mid) + (ev.max_step - mid);
    out.event_indices.push_back(ev.event_index);
  }

  const int dist_below = mid - min_step;
  const int dist_above = max_step - mid;
  if (dist_above > dist_below) {
    out.stem_direction = StemDirection::Down;
  } else if (dist_below > dist_above) {
    out.stem_direction = StemDirection::Up;
  } else {
    out.stem_direction = (signed_sum >= 0) ? StemDirection::Down : StemDirection::Up;
  }

  const float stem_x_off = (out.stem_direction == StemDirection::Up) ? notehead_width_spaces : 0.0f;
  auto stem_x = [&](std::size_t i) noexcept { return run[i].x_spaces + stem_x_off; };

  out.start_x_spaces = stem_x(0);
  out.end_x_spaces = stem_x(run.size() - 1U);
  const float dx = out.end_x_spaces - out.start_x_spaces;

  const float y_first_ideal = (out.stem_direction == StemDirection::Up)
                                  ? std::max(2.0f, step_to_spaces(run.front().max_step) + 3.25f)
                                  : std::min(2.0f, step_to_spaces(run.front().min_step) - 3.25f);
  const float y_last_ideal = (out.stem_direction == StemDirection::Up)
                                 ? std::max(2.0f, step_to_spaces(run.back().max_step) + 3.25f)
                                 : std::min(2.0f, step_to_spaces(run.back().min_step) - 3.25f);

  float slope = (dx > 1e-4f) ? ((y_last_ideal - y_first_ideal) / dx) : 0.0f;
  slope = std::clamp(slope, -0.22f, 0.22f);
  if (dx > 1e-4f && std::abs(slope * dx) > 1.2f) {
    slope = (slope >= 0.0f ? 1.2f : -1.2f) / dx;
  }

  float base_y0 = y_first_ideal;
  if (out.stem_direction == StemDirection::Up) {
    for (std::size_t i = 0; i < run.size(); ++i) {
      const float req_y = std::max(2.0f, step_to_spaces(run[i].max_step) + 2.65f);
      const float line_y = base_y0 + slope * (stem_x(i) - out.start_x_spaces);
      if (line_y < req_y) {
        base_y0 += (req_y - line_y);
      }
    }
  } else {
    for (std::size_t i = 0; i < run.size(); ++i) {
      const float req_y = std::min(2.0f, step_to_spaces(run[i].min_step) - 2.65f);
      const float line_y = base_y0 + slope * (stem_x(i) - out.start_x_spaces);
      if (line_y > req_y) {
        base_y0 -= (line_y - req_y);
      }
    }
  }

  out.start_y_spaces = base_y0;
  out.end_y_spaces = base_y0 + slope * dx;

  auto beam_y_at = [&](float x) noexcept { return base_y0 + slope * (x - out.start_x_spaces); };

  out.stem_tip_y_spaces.reserve(run.size());
  for (std::size_t i = 0; i < run.size(); ++i) {
    out.stem_tip_y_spaces.push_back(beam_y_at(stem_x(i)));
  }

  // Secondary 16th beams (contiguous 16ths or short fractional hooks)
  const float sec_y_shift = (out.stem_direction == StemDirection::Up) ? -0.75f : 0.75f;
  std::size_t i = 0;
  while (i < run.size()) {
    if (run[i].base_duration != NoteBaseDuration::Sixteenth) {
      ++i;
      continue;
    }
    std::size_t j = i + 1U;
    while (j < run.size() && run[j].base_duration == NoteBaseDuration::Sixteenth) {
      ++j;
    }
    if (j - i >= 2U) {
      const float x0 = stem_x(i);
      const float x1 = stem_x(j - 1U);
      out.secondary_beams.push_back(SecondaryBeamSegment{
          .x0_spaces = x0,
          .x1_spaces = x1,
          .y0_spaces = beam_y_at(x0) + sec_y_shift,
          .y1_spaces = beam_y_at(x1) + sec_y_shift,
          .is_hook = false,
      });
    } else {
      const float xk = stem_x(i);
      float x_other = xk;
      if (i == 0U) {
        const float span = std::max(0.0f, stem_x(1U) - xk);
        x_other = xk + std::min(0.95f, span * 0.45f);
      } else {
        const float span = std::max(0.0f, xk - stem_x(i - 1U));
        x_other = xk - std::min(0.95f, span * 0.45f);
      }
      const float x0 = std::min(xk, x_other);
      const float x1 = std::max(xk, x_other);
      out.secondary_beams.push_back(SecondaryBeamSegment{
          .x0_spaces = x0,
          .x1_spaces = x1,
          .y0_spaces = beam_y_at(x0) + sec_y_shift,
          .y1_spaces = beam_y_at(x1) + sec_y_shift,
          .is_hook = true,
      });
    }
    i = j;
  }

  return out;
}

[[nodiscard]] constexpr TieDirection choose_tie_direction(const TieEndpointRef& ref) noexcept {
  if (ref.chord_note_count <= 1U) {
    return (ref.stem_direction == StemDirection::Up) ? TieDirection::Below : TieDirection::Above;
  }
  return (ref.index_in_chord < ref.chord_note_count / 2U) ? TieDirection::Below
                                                          : TieDirection::Above;
}

} // namespace

std::vector<BeamGroupLayout> compute_beam_groups(std::span<const BeamEventInput> events,
                                                 TimeSignature meter, Clef clef,
                                                 float notehead_width_spaces) {
  std::vector<BeamGroupLayout> groups;
  if (events.size() < 2U) {
    return groups;
  }

  std::size_t idx = 0;
  while (idx < events.size()) {
    if (!is_beamable(events[idx])) {
      ++idx;
      continue;
    }
    const int bucket_size = std::max(1, beam_bucket_ticks(meter, events, events[idx].start_tick));
    const int bucket_id = events[idx].start_tick / bucket_size;

    std::vector<BeamEventInput> run;
    run.push_back(events[idx]);
    std::size_t j = idx + 1U;
    while (j < events.size()) {
      if (!is_beamable(events[j])) {
        break;
      }
      const int next_bucket_size =
          std::max(1, beam_bucket_ticks(meter, events, events[j].start_tick));
      if (next_bucket_size != bucket_size ||
          (events[j].start_tick / next_bucket_size) != bucket_id) {
        break;
      }
      run.push_back(events[j]);
      ++j;
    }

    if (run.size() >= 2U) {
      groups.push_back(build_beam_group(run, clef, notehead_width_spaces));
    }
    idx = j;
  }

  return groups;
}

std::vector<TieSpan> resolve_tie_spans(std::span<const TieEndpointRef> system_notes) {
  std::vector<TieSpan> spans;
  std::vector<TieEndpointRef> open_starts;

  for (const auto& ref : system_notes) {
    if (ref.tie_stop) {
      const auto it = std::find_if(open_starts.begin(), open_starts.end(),
                                   [&](const TieEndpointRef& s) { return s.midi == ref.midi; });
      if (it != open_starts.end()) {
        spans.push_back(TieSpan{
            .kind = TieSpanKind::Complete,
            .direction = choose_tie_direction(*it),
            .start_step = it->step,
            .end_step = ref.step,
            .start_x_spaces = it->x_spaces,
            .end_x_spaces = ref.x_spaces,
            .start_measure_index = it->measure_index,
            .end_measure_index = ref.measure_index,
        });
        open_starts.erase(it);
      } else {
        spans.push_back(TieSpan{
            .kind = TieSpanKind::IncomingHalf,
            .direction = choose_tie_direction(ref),
            .start_step = ref.step,
            .end_step = ref.step,
            .start_x_spaces = std::max(0.0f, ref.x_spaces - 2.0f),
            .end_x_spaces = ref.x_spaces,
            .start_measure_index = ref.measure_index,
            .end_measure_index = ref.measure_index,
        });
      }
    }
    if (ref.tie_start) {
      open_starts.push_back(ref);
    }
  }

  for (const auto& open : open_starts) {
    spans.push_back(TieSpan{
        .kind = TieSpanKind::OutgoingHalf,
        .direction = choose_tie_direction(open),
        .start_step = open.step,
        .end_step = open.step,
        .start_x_spaces = open.x_spaces,
        .end_x_spaces = open.x_spaces + 2.0f,
        .start_measure_index = open.measure_index,
        .end_measure_index = open.measure_index,
    });
  }

  return spans;
}

} // namespace selfano::core

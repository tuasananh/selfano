#include "core/staff_layout.hpp"

#include <algorithm>
#include <cmath>

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

} // namespace selfano::core

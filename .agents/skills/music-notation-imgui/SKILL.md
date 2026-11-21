---
name: music-notation-imgui
description: >-
  Domain reference for Selfano's hybrid procedural ImDrawList + SMuFL music
  notation renderer. Use when implementing or modifying diatonic staff-step
  math, key signatures, ledger lines, stacked chord 2nd-interval offsets, SMuFL
  font loading, or color-coded MIDI feedback overlays.
---

# Hybrid Procedural `ImDrawList` + SMuFL Notation Guide

Selfano renders sheet music at >= 60 FPS with zero raster image assets by combining:
1. **`ImDrawList` vector geometry** for 5-line staves, braces/brackets, barlines, ledger lines, stems, beams, playhead cursor, and highlight overlays.
2. **Bundled OFL-licensed SMuFL font** (e.g., *Leland* or *Bravura*) loaded into Dear ImGui's font atlas for publication-grade clefs, accidentals, noteheads, and rests.

---

## 1. Diatonic Staff-Step Coordinate System (`piano_core`)

Keep all notation positioning pure and testable in `piano_core` using **diatonic steps** (not raw MIDI semitones):

- Define `diatonic_step(NoteLetter, octave) = octave * 7 + letter_index`, where `C=0, D=1, E=2, F=3, G=4, A=5, B=6`.
- Anchor **Middle C4** at `step = 4 * 7 + 0 = 28`.
- **Treble Staff (5 lines):** Bottom line `E4` (`30`), lines at `30, 32, 34, 36, 38` (`E4, G4, B4, D5, F5`). Anchor: **G4 = 32** (2nd line from bottom).
- **Bass Staff (5 lines):** Bottom line `G2` (`18`), lines at `18, 20, 22, 24, 26` (`G2, B2, D3, F3, A3`). Anchor: **F3 = 24** (4th line from bottom).
- **Vertical pixel mapping:** Let `staff_space` (px) be the distance between two adjacent staff lines (`half_space = staff_space * 0.5f`).
  - Moving **up** 1 diatonic step decreases screen `y` by `half_space`.
  - `y = bottom_line_y - (step - bottom_line_step) * half_space`.

## 2. Ledger Lines (Pure Geometry Math)

Given `bottom_line_step` and `top_line_step = bottom_line_step + 8`:
- If `step < bottom_line_step - 1`: draw ledger lines on every **even** step offset below `bottom_line_step` down to `step` (e.g., Middle C4 on Treble = `28`, E2 on Bass = `16`).
- If `step > top_line_step + 1`: draw ledger lines on every **even** step offset above `top_line_step` up to `step` (e.g., A5 on Treble = `40`).
- Ledger line width should extend ~`0.4 * notehead_width` past both left and right edges of any offset noteheads in the chord.

## 3. Key Signature Placement Tables (`constexpr`)

Order of sharps: `F C G D A E B`. Order of flats: `B E A D G C F`.
- **Treble Sharps:** `F5, C5, G5, D5, A4, E5, B4`
- **Treble Flats:** `B4, E5, A4, D5, G4, C5, F4`
- **Bass Sharps:** `F3, C3, G3, D3, A2, E3, B2`
- **Bass Flats:** `B2, E3, A2, D3, G2, C3, F2`

Always track active measure accidentals so courtesy/cancellation naturals (`♮`) appear only when needed relative to the key signature and prior notes in the same bar.

## 4. Stacked Chord Layout & Adjacent 2nd Offsets

When rendering simultaneous notes (dyads, triads, inversions) on the same staff:
1. Sort chord notes ascending by `diatonic_step`.
2. Determine stem direction: compare average/furthest distance from the staff middle line (`B4 = 34` in Treble, `D3 = 22` in Bass). Stem **up** if below middle line, **down** if at or above middle line.
3. **2nd-interval collision avoidance:** Walk adjacent pairs `(n[i], n[i+1])`:
   - If `n[i+1].step - n[i].step == 1` and `n[i]` is not offset:
     - Stem **up**: offset the **upper** note (`n[i+1]`) to the **right** by `+notehead_width * 0.95f`.
     - Stem **down**: offset the **lower** note (`n[i]`) to the **left** by `-notehead_width * 0.95f`.
4. **Accidental column packing:** Place accidentals to the left of the leftmost notehead, staggering into a second column if two accidentals are within 5 diatonic steps vertically.

## 5. Key SMuFL Codepoints (Standard Music Font Layout)

Load the SMuFL `.ttf`/`.otf` font into Dear ImGui with explicit glyph range `0xE000–0xE29F` (small atlas footprint, fast startup, works identically on Emscripten):

| Symbol | SMuFL Codepoint | UTF-8 Literal |
| :--- | :--- | :--- |
| G Clef (Treble) | `U+E050` | `"\xEE\x81\x90"` |
| F Clef (Bass) | `U+E062` | `"\xEE\x81\xA2"` |
| Notehead Whole | `U+E0A2` | `"\xEE\x82\xA2"` |
| Notehead Half | `U+E0A3` | `"\xEE\x82\xA3"` |
| Notehead Black | `U+E0A4` | `"\xEE\x82\xA4"` |
| Accidental Flat (`b`) | `U+E260` | `"\xEE\x89\xA0"` |
| Accidental Natural (`♮`) | `U+E261` | `"\xEE\x89\xA1"` |
| Accidental Sharp (`#`) | `U+E262` | `"\xEE\x89\xA2"` |
| Quarter Rest | `U+E4E5` | `"\xEE\x93\xA5"` *(extend range to `0xE4FF` if rendering rests)* |

*Tip:* SMuFL fonts define `1 em = 4 * staff_space` (the full height of a 5-line staff). Render SMuFL glyphs at `font_size = 4.0f * staff_space` and align vertically to each glyph's musical origin (G4 line for G clef, F3 line for F clef, note center for noteheads). Verify baseline alignment visually in Phase 1!

## 6. Feedback Color Coding

- **Neutral (Pending):** High-contrast light/dark ink matching active theme.
- **Green (Correct):** Crisp emerald highlight on matched notehead(s).
- **Red (Incorrect):** Coral/crimson highlight + compact inline label showing `Target -> Played` (e.g., `F#4 -> F4`) without overlapping adjacent staves.

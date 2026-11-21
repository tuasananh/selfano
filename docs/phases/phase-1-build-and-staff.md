# Phase 1: Build System, Core Theory Foundation & Static Grand Staff

**Goal:** Stand up the clean C++23 CMake + pure `FetchContent` project (`selfano`), implement the deterministic music theory & staff-projection core with `Catch2` TDD, and render a crisp static Grand Staff using `ImDrawList` + a bundled OFL SMuFL music font.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `verification-and-review`
**MCP Check:** Query `context7` for exact CMake `FetchContent` targets for `SDL3`, `Dear ImGui` (`imgui_impl_sdl3` + `imgui_impl_sdlrenderer3`), `libremidi`, `nlohmann/json`, and `Catch2` v3.

---

## Deliverables & Vertical Slices

### Slice 1.1 — CMake C++23 + Pure `FetchContent` + Presets
- Create `CMakeLists.txt` and `CMakePresets.json`:
  - `native-debug` (Ninja, Debug, `-Wall -Wextra -Wpedantic`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`)
  - `native-release` (Ninja, Release)
  - `web-release` (Emscripten preset stub ready for Phase 6)
- Fetch pinned versions via `FetchContent`: `SDL3`, `imgui`, `libremidi`, `nlohmann_json`, `Catch2`.
- Define CMake targets matching `AGENTS.md`:
  - `piano_core` (STATIC library, zero UI/SDL/MIDI link deps)
  - `piano_midi` (STATIC library, stubbed ready for Phase 2)
  - `piano_ui` (STATIC library, links `piano_core`, `imgui`, `SDL3::SDL3`)
  - `selfano` (executable, `src/main.cpp`)
  - `selfano_tests` (`Catch2::Catch2WithMain`, registered with `catch_discover_tests`)

### Slice 1.2 — Pure Music Theory Core (`src/core/` via `cpp23-tdd`)
- `pitch.hpp/.cpp`: MIDI pitch (`0..127`), `NoteLetter` (`C..B`), `Accidental` (`DoubleFlat..DoubleSharp`), `SpelledNote { NoteLetter, Accidental, int octave }`, `to_midi()`, `diatonic_step()`, `format_note()` (`std::format`).
- `key_signature.hpp/.cpp`: All 15 major + 15 relative minor key signatures (`-7..+7` fifths), key-appropriate enharmonic spelling (`spell_midi_in_key()`), `constexpr` treble/bass key-signature sharp/flat placement tables.
- `theory.hpp/.cpp`: Intervals (unison through octave), scales (major, natural/harmonic minor), diatonic triads & inversions (root, 1st, 2nd), chord spelling in key.
- `staff_layout.hpp/.cpp`: Pure geometry math (no ImGui!) computing:
  - Diatonic staff-step relative to Treble (`E4..F5`) and Bass (`G2..A3`)
  - Ledger line step lists (e.g., Middle C4, high A5, low E2)
  - Stem direction & adjacent 2nd-interval horizontal notehead offsets
  - Accidental column stagger offsets

### Slice 1.3 — Bundled OFL SMuFL Font + SDL3/ImGui Shell
- Add a clean OFL-licensed SMuFL font (e.g., *Leland* or *Bravura*) under `assets/fonts/` with its `OFL.txt` license file, bundled/embedded cleanly so both native and Emscripten builds find it reliably.
- `src/main.cpp`: SDL3 window + `SDL_Renderer` + Dear ImGui (`imgui_impl_sdl3` + `imgui_impl_sdlrenderer3`) main loop with HiDPI scaling and merged SMuFL glyph range (`0xE000–0xE4FF`).

### Slice 1.4 — Static Grand Staff Renderer (`src/ui/staff_renderer.*`)
- Render **Treble Clef**, **Bass Clef**, and **Grand Staff** (brace + joined barlines) via `ImDrawList` + SMuFL glyphs.
- Render key signatures, ledger lines, accidentals, barlines, stems, and stacked harmonic chords with 2nd-interval offsets.
- Build a small Phase 1 **Staff Showcase Panel** allowing visual inspection of key signatures (C, G, D, Bb, Eb, F#), ledger-line extremes (E2, C4, A5), and stacked triads/2nd clusters.

---

## Exit Gate (Verification Criteria)

1. `cmake --preset native-debug && cmake --build --preset native-debug` succeeds with **zero compiler warnings**.
2. `ctest --preset native-debug --output-on-failure` passes 100% of `piano_core` tests (pitch, key signatures, enharmonic spelling, triads/inversions, ledger lines, 2nd-interval offset math).
3. `grep -rnE '#include\s*[<"](imgui|SDL|libremidi|emscripten)' src/core/` returns zero matches.
4. Launching `./build/native-debug/selfano` renders a crisp Grand Staff at >= 60 FPS with properly aligned SMuFL clefs, key signatures, ledger lines, and non-overlapping 2nd-interval noteheads.

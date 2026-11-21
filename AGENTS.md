# Selfano — Project Agent Rules

Selfano is an offline, zero-latency, cross-platform **C++23** piano trainer (Linux/macOS/Windows native + Emscripten WebAssembly) built with **SDL3 + SDL_Renderer**, **Dear ImGui**, **`libremidi`**, **`nlohmann/json`**, and **`Catch2`**.

Source of truth:
- MVP spec: `self_taught_piano_roadmap_c_23_app_mvp_spec.md`
- Master roadmap: `docs/ROADMAP.md`
- Current progress & active phase: `docs/STATE.md`

---

## 1. Workflow & Session Discipline

1. **Always orient from state first:** Read `docs/STATE.md` and the active phase packet in `docs/phases/` before planning or editing code.
2. **Use the curated skills in `.agents/skills/`:**
   - `spec-phase-workflow` when starting, advancing, or handing off a phase.
   - `cpp23-tdd` whenever implementing or modifying `piano_core` or testable `piano_midi` logic.
   - `systematic-debugging` whenever a build, test, MIDI/threading, rendering, or Emscripten bug occurs.
   - `verification-and-review` before claiming any task or phase complete.
   - `music-notation-imgui` when working on staff geometry, SMuFL glyphs, key signatures, or notehead layout.
   - `midi-emscripten-interop` when working on `libremidi`, SPSC queues, `SDL_AudioStream`, WebMIDI, or `IDBFS`.
3. **Update `docs/STATE.md` at phase completion** (or before a handoff) with exact verification evidence and the next concrete step.
4. **Atomic commits:** Keep git history clean with focused commits per vertical slice/phase.

---

## 2. Hard Architecture Boundaries

```text
selfano (src/main.cpp)
  ├── piano_ui   (src/ui/)   -> depends on: piano_core, Dear ImGui, SDL3
  ├── piano_midi (src/midi/) -> depends on: piano_core, libremidi, SDL3 (audio fallback)
  └── piano_core (src/core/) -> PURE C++23 ONLY (zero UI, zero SDL, zero libremidi)
```

- **`src/core/` (`piano_core`) MUST remain 100% hardware- and UI-agnostic.** Never include `<SDL3/...>`, `<imgui.h>`, or `<libremidi/...>` anywhere in `src/core/`. See `src/core/AGENTS.md`.
- **Real-time MIDI / audio thread safety:** Inside `libremidi` input callbacks and `SDL_AudioStream` callbacks:
  - **NEVER** acquire a mutex, allocate/free heap memory, log to stdout/stderr, or touch ImGui/UI state.
  - Communicate strictly via the lock-free SPSC ring buffer (`piano_midi`), drained once per UI frame on the main thread.
- **Pure `FetchContent` dependencies:** Use CMake `FetchContent` with pinned versions/tags for `SDL3`, `imgui`, `libremidi`, `nlohmann_json`, and `Catch2` so native desktop and Emscripten builds use identical dependency versions.
- **Local persistence:** Store `profile.json` via `SDL_GetPrefPath("selfano", "selfano")` on desktop (overridable via `--profile <path>`), and via Emscripten `IDBFS` mounted at `/persist` on Web builds.

---

## 3. C++23 Coding Standards

- Standard: `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`.
- Warnings: Compile all project targets warning-free under `-Wall -Wextra -Wpedantic` (GCC/Clang) or `/W4` (MSVC). Do not suppress warnings globally; fix the root cause.
- Memory & Types:
  - Zero raw owning `new`/`delete` or `malloc`/`free`. Prefer value semantics, `std::unique_ptr`, `std::vector`, `std::array`, and `std::span`.
  - Use `std::expected<T, E>` for recoverable errors (MIDI port open, JSON parse/save, chord parsing).
  - Use `std::format` for formatting note names, hints, and telemetry strings.
  - Use `constexpr` tables for static music theory data (key signatures, diatonic steps, interval names, SMuFL codepoints).
- Formatting: Match `.clang-format`; run `clang-format` on edited files before finishing a phase.

---

## 4. Standard Build & Verification Commands

Once CMake presets are configured (Phase 1+):

```bash
# Native Debug configure, build, and test
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure

# Emscripten WebGUI build (Phase 6+)
emcmake cmake --preset web-release
cmake --build --preset web-release
```

Boundary check (must return zero matches):
```bash
grep -rnE '#include\s*[<"](imgui|SDL|libremidi)' src/core/ || true
```

---

## 5. Global MCP Server Usage Policy

Do not add redundant project-local MCP servers; use the configured global MCP servers intentionally:

- **`context7` (`resolve-library-id`, `query-docs`):** Query authoritative docs before writing non-trivial `SDL3` (especially `SDL_AudioStream`, `SDL_Renderer`, `SDL_GetPrefPath`), `Dear ImGui` (`imgui_impl_sdl3`, `imgui_impl_sdlrenderer3`, font atlas merging), `libremidi` (ALSA/CoreMIDI/WinMM/WebMIDI), `Catch2`, or `nlohmann/json` API calls. Never guess SDL3 signatures from SDL2 memory.
- **`chrome-devtools`:** Use in Phase 6 (and on any WebGUI changes) to verify the Emscripten build in a real browser—checking console errors, WebGL canvas rendering, WebMIDI availability, and `IDBFS` reload persistence.
- **`sequential-thinking`:** Use when designing subtle algorithms (enharmonic note spelling, voice-leading constraints in procedural etudes, flexible Companion Mode chord matching, SPSC memory ordering).
- **`memory`:** Optional persistence for hardware/machine-specific facts (e.g., detected Korg LP-380 MIDI device strings).

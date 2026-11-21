# Phase 6: Emscripten WebAssembly Packaging, WebMIDI & Browser Verification

**Goal:** Cross-compile the exact same C++23 codebase via `emcmake cmake` so Selfano runs 100% offline in a local browser with WebMIDI, `SDL_AudioStream` WebAudio fallback, and persistent `IDBFS` storage.

**Required Skills:** `spec-phase-workflow`, `midi-emscripten-interop`, `systematic-debugging`, `verification-and-review`
**MCP / Plugin Check:** Use `context7` for SDL3 + Emscripten CMake flags and `chrome-devtools` plugin to verify live browser rendering, console cleanliness, and `IDBFS` persistence.

---

## Deliverables & Vertical Slices

### Slice 6.1 — Emscripten CMake & Main Loop Integration
- Finalize `web-release` preset in `CMakePresets.json` and `#ifdef __EMSCRIPTEN__` targets in `CMakeLists.txt`:
  - Pure `FetchContent` compilation of SDL3, Dear ImGui, `libremidi` (WebMIDI backend), and `nlohmann_json` (skip `Catch2` tests on WASM build).
  - Embed or `--preload-file` the bundled SMuFL font cleanly.
  - Link with `-lidbfs.js`, `-sALLOW_MEMORY_GROWTH=1`, and clean minimal HTML shell (`web/shell.html`) with full-viewport responsive canvas and crisp dark styling.
- Update `src/main.cpp` to run `emscripten_set_main_loop_arg` cleanly under `#ifdef __EMSCRIPTEN__` and unlock WebAudio on first user gesture.

### Slice 6.2 — Emscripten `IDBFS` Persistence Sync
- Mount `IDBFS` at `/persist` at startup, execute `FS.syncfs(true, ...)` before loading `/persist/profile.json`, and trigger `FS.syncfs(false, ...)` after every profile save.
- Gracefully handle first-boot empty `IDBFS` and WebMIDI unavailable/denied permissions (automatically activating Virtual Piano + QWERTY + `SDL_AudioStream` synth without errors).

### Slice 6.3 — Browser Verification via `chrome-devtools` & Docs
- Build with `emcmake cmake --preset web-release && cmake --build --preset web-release`.
- Serve `build-web/` locally (e.g., `python3 -m http.server`) and inspect via `chrome-devtools` / browser verification:
  - Zero uncaught JS/WASM console errors.
  - Grand Staff + SMuFL glyphs render sharply at browser DPI.
  - Virtual piano & QWERTY input trigger live Green/Red feedback and WebAudio synthesis.
  - Adding a repertoire chord roadmap or SRS card persists across a full browser page reload via `IDBFS`.
- Add concise build & run instructions in `README.md` (Linux/macOS/Windows native + Emscripten WebGUI + Korg LP-380 5-pin DIN cable wiring diagram).

---

## Exit Gate (Verification Criteria)

1. Both `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` AND `emcmake cmake --preset web-release && cmake --build --preset web-release` succeed with zero warnings.
2. Browser verification confirms zero console errors, crisp SMuFL notation, working virtual piano + audio, and surviving `profile.json` data across page reload.
3. `docs/ROADMAP.md` and `docs/STATE.md` updated to mark MVP Complete.

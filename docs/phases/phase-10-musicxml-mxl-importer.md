# Phase 10: `.musicxml` & `.mxl` Importer (`My Books` Track — Desktop & Web)

**Status:** `[ ] Planned` (Depends on Phase 8; independent of Phase 9 content expansion)  
**Goal:** Enable users to import any external piano book study or classical piece in uncompressed **`.musicxml` / `.xml`** or compressed **`.mxl`** format on both Desktop (native OS file picker + drag-and-drop + CLI flag) and Emscripten WebGUI (browser file picker + canvas drop + `IDBFS` `/persist/lessons/` persistence), automatically slicing longer works into selectable 4-to-8 bar phrases inside the `My Books` curriculum track.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `midi-emscripten-interop`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Is Isolated Into Phase 10

Introducing two new third-party `FetchContent` libraries (`pugixml` + `miniz`), handling messy real-world MusicXML files exported from MuseScore/OpenScore/Finale/Sibelius, and wiring cross-platform OS + browser file I/O carries distinct build and edge-case risk. Isolating this into Phase 10 keeps Phases 7–9 fast, zero-dependency, and rock-solid.

---

## 2. Dependencies & Security Guardrails

### Pinned `FetchContent` Additions (`CMakeLists.txt`)
- **`pugixml`** (pinned release tag, e.g. `v1.15`, static library, tests/install OFF): warning-clean C++ XML DOM parser linked privately into `piano_core`.
- **`miniz`** (pinned release tag, e.g. `3.0.2`, static library): pure C ZIP/DEFLATE reader linked privately into `piano_core` to unpack `.mxl` containers with zero OS `zlib` dependency on Desktop or Emscripten.
- Both targets compiled with system include directories so third-party warnings never pollute Selfano's `-Wall -Wextra -Wpedantic` strict warning policy.

### Defensive Input Limits (Desktop & 1 GB Emscripten Heap Safety)
- Max compressed `.mxl` file size: **`16 MiB`**.
- Max decompressed XML payload: **`32 MiB`** (protects against zip-bomb / malformed archives).
- Max measures imported per score: **`512`** (graceful truncation notice or clear `std::unexpected` error).
- Never throw uncaught exceptions across public `piano_core` APIs—all errors returned via `std::expected<LessonScore, std::string>`.

---

## 3. Deliverables & Vertical Slices

### Slice 10.1 — Pure Core `.musicxml` & `.mxl` Parser (`src/core/musicxml_importer.*` via `cpp23-tdd`)
- **Public APIs (`src/core/musicxml_importer.hpp`):**
  ```cpp
  struct MusicXmlImportOptions {
    std::string fallback_title{"Imported Piece"};
    double default_bpm{84.0};
    std::size_t max_measures{512};
  };

  struct MusicXmlImportResult {
    LessonScore score{};
    std::vector<std::string> warnings; // e.g., "Skipped 3 grace notes", "Defaulted tempo to 84 BPM"
  };

  [[nodiscard]] std::expected<MusicXmlImportResult, std::string>
  import_musicxml_string(std::string_view xml_text, MusicXmlImportOptions options = {});

  [[nodiscard]] std::expected<MusicXmlImportResult, std::string>
  import_mxl_bytes(std::span<const std::uint8_t> zip_bytes, MusicXmlImportOptions options = {});

  [[nodiscard]] std::expected<MusicXmlImportResult, std::string>
  import_musicxml_file(const std::filesystem::path& path, MusicXmlImportOptions options = {});
  ```
- **`.mxl` Unpacking Logic:**
  1. Open ZIP archive in memory via `miniz`.
  2. Read `META-INF/container.xml` and locate `<rootfile full-path="..."/>` (fallback: first `.musicxml` or `.xml` entry outside `META-INF/`).
  3. Enforce decompressed byte cap (`<= 32 MiB`) and pass XML text to `import_musicxml_string`.
- **MusicXML `<score-partwise>` Mapping Rules:**
  - **Part Selection:** Prefer the first part declaring `<staves>2</staves>` (or piano instrument name); fallback to first part (splitting notes at Middle C4 `midi >= 60` → Treble, `< 60` → Bass) or merging first 2 single-staff parts if clearly treble+bass.
  - **Metadata:** `<work><work-title>` or `<movement-title>`, `<identification><creator type="composer">`.
  - **Attributes:** `<divisions>` (converted cleanly via rational scaling `tick = round(raw_dur * 480 / divisions)`), `<key><fifths>/<mode>`, `<time><beats>/<beat-type>`, `<clef>`.
  - **Notes & Voices:**
    - Handles `<backup><duration>` and `<forward><duration>` measure cursor resets accurately.
    - `<chord/>` reuses previous note's `start_tick` in the same staff/voice.
    - `<pitch>` (`<step>`, `<alter>`, `<octave>`) mapped to exact `SpelledNote` + `MidiPitch`.
    - `<rest/>` mapped to quantized `RhythmicValue`.
    - `<tie type="start"/>` / `<tie type="stop"/>` preserved for tie curves and MIDI continuation skipping.
    - `<notations><technical><fingering>` parsed into `finger` (`1..5`).
    - `<grace/>` skipped cleanly with a non-fatal warning note.
  - **Harmony:** `<harmony>` (`<root>`, `<kind>`, `<bass>`) converted into `HarmonicAnnotation` strings so imported scores with chord symbols gain **Chord Skeleton** mode automatically.
  - **Tempo:** `<direction><sound tempo="..."/>` or `<metronome>` sets `meta.target_bpm` (clamped `[36, 220]`).
  - **Auto Phrase Slicing:** Respects double barlines / `<rehearsal>` marks when present; otherwise automatically slices into balanced **4-to-8 bar `LessonPhrase`** chunks so a 48-bar book piece immediately becomes drillable 4–8 bar chunks inside one lesson.

### Slice 10.2 — Imported Lesson Storage & `My Books` Track (`src/core/lesson_library.*`, `src/core/profile_store.*`)
- Persist imported normalized `LessonScore` JSON files under:
  - **Desktop:** `<pref_dir>/lessons/<id>.json` (`SDL_GetPrefPath("selfano", "selfano")`, or sibling `lessons/` directory next to custom `--profile <path>`).
  - **Emscripten:** `/persist/lessons/<id>.json` synced to IndexedDB via existing coalescing `FS.syncfs(false)`.
- Support full CRUD for `My Books` imported lessons:
  - **Import** (generates stable unique `imp_<hash>` ID, registers in `profile.imported_lesson_ids`).
  - **Edit Metadata & Harmony** (update title, composer, target BPM, phrase boundaries, or bar chord symbols; re-save `<id>.json`).
  - **Delete** (removes `<id>.json`, removes ID from `imported_lesson_ids` & `lesson_progress`, switches active lesson safely if currently selected).

### Slice 10.3 — Cross-Platform File Picker, Drag-and-Drop & Import Modal (`src/ui/lesson_view.*`, `src/main.cpp`)
- **Desktop (`SDL3`):**
  - **"Import Score (.musicxml / .mxl)"** button inside `LessonView` Curriculum Browser (`My Books` tab) invokes **`SDL_ShowOpenFileDialog`** (async callback marshaled cleanly onto the main thread with zero UI blocking; filter `musicxml;xml;mxl`).
  - Handle **`SDL_EVENT_DROP_FILE`** so dragging a `.musicxml` or `.mxl` file onto the Selfano window imports it immediately.
  - CLI flag `--import-lesson <path>` imports a score on startup for scripted verification.
- **Emscripten WebGUI:**
  - **"Import Score (.musicxml / .mxl)"** button triggers a tiny `EM_JS` offscreen `<input type="file" accept=".musicxml,.xml,.mxl">` + canvas `dragover`/`drop` listener that reads `File.arrayBuffer()`, copies bytes to WASM heap, invokes `EMSCRIPTEN_KEEPALIVE selfano_on_imported_file(filename, ptr, len)`, runs `import_mxl_bytes` / `import_musicxml_string`, writes `/persist/lessons/<id>.json`, and flushes `IDBFS`.
- **Import Preview & Quick-Edit Modal:**
  - Shows detected Title, Composer, Key, Meter, Bar Count, auto-generated Phrases, and any non-fatal parser warnings (e.g., skipped grace notes), with editable Title & Target BPM before saving and opening directly in the Lesson Player.

### Slice 10.4 — Cross-Platform Verification & Browser Testing
- **Catch2 Unit Tests (`tests/musicxml_importer_tests.cpp`):**
  - Multi-staff Grand Staff `.musicxml` fixture with `<backup>`, `<chord/>`, ties, dots, 8th/16th notes, fingerings, `<harmony>`, and tempo.
  - Real compressed `.mxl` ZIP fixture (validating `META-INF/container.xml` lookup and decompression).
  - Long 32-bar fixture verifying automatic 8-bar phrase slicing.
  - Error & security tests: malformed XML, corrupt ZIP bytes, missing rootfile, oversized payload rejection.
- **Live Browser Verification (Emscripten via `chrome-devtools`):**
  - Verify `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles `pugixml`, `miniz`, and `selfano` with zero warnings.
  - Verify uploading a `.musicxml` and `.mxl` file in browser renders cleanly in `My Books`, plays Demo audio, and survives a hard `Page.reload({ ignoreCache: true })` via `IDBFS` `/persist/lessons/`.

---

## 4. Exit Gate (Verification Criteria)

1. Zero compiler warnings on both `native-debug` and `web-release` (including clean compilation of `pugixml` and `miniz`).
2. 100% passing `ctest --preset native-debug --output-on-failure` including full `musicxml_importer_tests`.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Desktop end-to-end test (`--import-lesson` + `SDL_ShowOpenFileDialog` / drag-and-drop) imports, plays, grades, edits, deletes, and persists `.musicxml` and `.mxl` scores cleanly.
5. Emscripten browser verification confirms 0 console errors, working Web file upload, and full `IDBFS` reload persistence of imported scores.

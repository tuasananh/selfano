# Phase 2: Bidirectional MIDI, SPSC Bridge, Audio Fallback & Virtual Piano

**Goal:** Connect real hardware MIDI (`IN` + `OUT` via `libremidi`), lock-free thread bridging, polyphonic chord grouping, an offline `SDL_AudioStream` synth fallback, an interactive virtual piano + QWERTY input, and live Neutral/Green/Red staff feedback.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `midi-emscripten-interop`, `music-notation-imgui`, `verification-and-review`
**MCP Check:** Query `context7` for `libremidi` port observer/in/out API and SDL3 `SDL_OpenAudioDeviceStream` / `SDL_PutAudioStreamData`.

---

## Deliverables & Vertical Slices

### Slice 2.1 — Lock-Free SPSC Queue & Polyphonic Chord Grouper (`src/midi/` via `cpp23-tdd`)
- `spsc_queue.hpp`: Cache-line-aligned lock-free SPSC ring buffer (`SpscQueue<MidiNoteEvent, 1024>`) with acquire/release atomics and zero dynamic allocation.
- `chord_grouper.hpp/.cpp`: Timestamp-injected polyphonic chord window (`80–120 ms`, default `100 ms`) grouping slightly rolled `NoteOn` events into a single `PlayedChord` while maintaining a 128-key live pressed-state bitset.
- `Catch2` tests covering SPSC wrap-around, empty/full bounds, `velocity == 0` normalization, rolled chord grouping within window, and separate chords outside window.

### Slice 2.2 — Bidirectional `libremidi` Wrapper (`src/midi/midi_engine.*`)
- Auto-detect connected `MIDI IN` and `MIDI OUT` ports with live refresh & hot-reconnect support.
- Parse `NoteOn`, `NoteOff`, `NoteOn(vel=0) -> NoteOff`, and Soft Pedal `CC 67` in the OS callback thread and push into `SpscQueue` with zero locks/allocations.
- Outbound `send_note_on`, `send_note_off`, timed note-off scheduler (drained on main thread), and `all_notes_off()` (`CC 123`) safety panic.

### Slice 2.3 — `SDL_AudioStream` Software Synth Fallback (`src/midi/audio_synth.*`)
- Lightweight polyphonic sine+triangle ADSR voice mixer outputting directly to `SDL_AudioStream` when no hardware `MIDI OUT` port is selected (or when user forces software audio).
- Built-in subtle synthesized **metronome click** and **clean-rep confirmation chime** cues for Phases 3 & 4.

### Slice 2.4 — Virtual Piano, QWERTY Mapper & Live Staff Feedback (`src/ui/`)
- Top/docked MIDI Device Bar: `MIDI IN` dropdown, `MIDI OUT` dropdown (with `"Software Synth (SDL3)"` fallback option), Refresh button, and chord-window ms slider (`80–120 ms`).
- Interactive on-screen `ImDrawList` virtual piano (mouse clickable, highlights currently held MIDI/QWERTY keys) + ergonomic QWERTY two-octave keyboard mapping (with octave shift `Z`/`X`).
- Staff feedback states in `StaffRenderer`:
  - **Neutral:** Pending target notes.
  - **Green:** Correctly played target notes.
  - **Red:** Incorrect played notes, displaying target vs. played note names (e.g., `F#4 -> F4`).
- Simple Phase 2 sandbox target prompt (single notes & triads) to test live evaluation end-to-end.

---

## Exit Gate (Verification Criteria)

1. Warning-free build (`cmake --build --preset native-debug`) and 100% passing `ctest` (including SPSC & chord grouper tests).
2. `src/core/` purity check passes with zero UI/SDL/MIDI includes.
3. Clicking virtual piano keys, pressing QWERTY keys, or playing a MIDI keyboard lights up the piano and immediately updates Neutral/Green/Red staff feedback without frame drops.
4. Triggering a test chord plays audible sound through `MIDI OUT` when connected, or seamlessly falls back to `SDL_AudioStream` when no `MIDI OUT` device is present.

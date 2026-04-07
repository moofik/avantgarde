# Avantgarde v0.1 Roadmap

## Goal
Deliver a playable MVP sampler with:
- 2 clip tracks
- simple quantized timing events (none/beat/bar)
- stretch-to-bars playback for clips
- per-track FX stack
- portable low-res UI path (desktop preview -> Raspberry Pi screen backend)

## Milestone 1: Core cleanup and API freeze
- [ ] Align `CONTRACTS.md` with actual code API and C++ standard.
- [ ] Remove direct `#include *.cpp` usage from app/tests via public factory headers.
- [ ] Define stable `TrackId` mapping for exactly two tracks in engine runtime.
- [ ] Add runtime smoke test for dual-track registration and processing.

Exit criteria:
- clean build from scratch
- all tests green

## Milestone 2: Transport and quantized scheduler
- [x] Implement `TransportBridge` double-buffer runtime object.
- [x] Add scheduler to execute clip/transport commands on quantized boundaries.
- [x] Support `QuantizeMode::{None, Beat, Bar}` in RT-safe path.
- [x] Add deterministic tests for command timing at block boundaries.

Exit criteria:
- repeatable quantized start/stop behavior in tests

## Milestone 3: Clip stretch-to-bars
- [x] Implement clip `targetSamples` from `bars + bpm + time signature + sampleRate`.
- [x] Implement varispeed playback increment (`srcFrames / targetFrames`) with interpolation.
- [x] Wire `setSlotLengthInBars()` into real playback behavior.
- [x] Add tests for expected rendered length in samples.

Exit criteria:
- clip aligns to requested bar length under fixed BPM

## Milestone 4: Per-track FX stack
- [x] Enable track-owned ordered `IAudioModule` chain per track.
- [x] Route parameter updates via `ParamBridge` to track/slot targets.
- [x] Add integration test: two tracks with different FX chains.

Exit criteria:
- stable dual-track playback with active FX chains

## Milestone 5: Portable UI layer
- [x] Introduce UI state DTO + renderer interface.
- [x] Introduce desktop ANSI renderer preview.
- [x] Add service-side state composer from runtime telemetry + transport + track state.
- [x] Add tiny input layer for transport/clip triggers.
- [x] Add low-res renderer backend (RPi target adapter).

Exit criteria:
- same `UiState` drives desktop preview and RPi backend renderer

## Milestone 6: Gothic GameBoy-like UI
- [x] Add `--ui=gb` terminal preview renderer with non-scrolling frame redraw.
- [x] Add muted gothic black-pink palette theme (`--theme=gothic`).
- [x] Add dedicated macOS preview window mode (`--ui=gb-window`).
- [x] Add optional Gothic Core font pipeline from `assets/fonts` for macOS preview.
- [ ] Add pixel-surface backend to map same renderer to real 2.8" SPI display.
- [ ] Add direct UI controls for track gain/fx params (beyond speed).

Exit criteria:
- stable real-time control UX in preview and same visual style on device display

## First implementation slice delivered now
- Added `src/contracts/IUi.h`
- Added `src/service/UiStateStore.{h,cpp}`
- Added `src/platform/terminal/AnsiUiRenderer.{h,cpp}`
- Wired live status rendering into `main.cpp`
- Added `src/contracts/IDisplay.h`
- Added `src/platform/lowres/LowResUiRenderer.{h,cpp}`
- Added `src/platform/terminal/TerminalCharDisplay.{h,cpp}` (desktop low-res adapter)

---

## vNext (2026-04-05) — Pattern-first Architecture + UX-first delivery

### Architectural decision (fixed)
Pattern is a standalone system entity, not just an `IRtExtension`.

We keep one live engine runtime (`tracks + transport`) and add a separate pattern layer:
- `PatternBank` owns patterns.
- Each `Pattern` stores music state data:
  - `TransportSnapshot` (bpm/ts/quant/swing, without live sample clock),
  - `TrackSnapshot[]` (clip refs, mute/arm/gain/speed/fx params),
  - `StepEvents[]` (trigs/locks/conditions/probability).
- `PatternScheduler` decides quantized pattern switch.
- `PatternRuntimePlayer` applies events/snapshots to live engine state via existing command path.

Why this model:
- avoids engine/platform coupling and keeps core deterministic;
- avoids duplication of full DSP object graphs per pattern;
- keeps real-time ownership clear: one live clock, one live playback graph.

### Milestone order update
We intentionally run pattern/sequencer architecture first, then editing/record UX layers.

## Milestone 1: Pattern Contracts (foundation)
- Add `IPattern.h` as a dedicated contract layer:
  - `PatternTransportSnapshot`,
  - `PatternTrackSnapshot`,
  - `PatternStepEvent`,
  - `PatternState`,
  - `IPatternBank` / `IPatternScheduler` / `IPatternRuntimePlayer`.
- Fix ownership boundaries in contracts:
  - pattern layer stores state/events,
  - live engine remains single RT playback graph.
- Update `CONTRACTS.md` to make Pattern a first-class system entity (not RT-extension only).

UX review at milestone end:
- clear user mental model: pattern change does not create hidden extra engine instances.

## Milestone 2: Sequencer command protocol (note lane)
- Normalize note command IDs in `ids.h`:
  - `NoteOn`,
  - `NoteOff`,
  - `NoteDetune` (fine offset for note events).
- Document wire payload in `IRtCommandQueue.h` with explicit field mapping (`track/slot/index/value`).
- Add dispatcher API for note commands and tests for deterministic queue payload.

UX review at milestone end:
- note actions stay immediate and predictable for live input / future MIDI mapping.

## Milestone 3: Sequencer Core (MVP)
- Add track step lane with mono-voice + retrigger baseline.
- Add per-step pitch (semitone) and optional fine detune.
- Keep event model deterministic under quantized transport.

UX review at milestone end:
- step edit must stay fast with limited controls (encoder + apply flow).

## Milestone 4: Pattern Runtime Entity (core)
- Add `Pattern`, `PatternBank`, `PatternScheduler`, `PatternRuntimePlayer`.
- Add minimal step triggers per track (without full p-lock matrix yet).
- Add quantized pattern switch (`none/beat/bar`) via scheduler.

UX review at milestone end:
- transparent pattern switching feedback,
- no hidden state transitions.

## Milestone 5: Sample Envelope Core
- Add clip envelope params (MVP: Attack/Release, optional Decay/Sustain in schema).
- Apply envelope in `ClipTrack` playback path without clicks.
- Cover with DSP tests on short/long clips and different playback speeds.

UX review at milestone end:
- keep controls immediate (no apply-confirm for continuous params),
- max 1 screen and minimal actions for envelope editing.

## Milestone 6: Sample Editor (MVP)
- Introduce `SampleEditorWidget` with start/end/loop/reverse/fade.
- Map `UiAction -> UiIntent -> control command` only (no renderer logic in widget).
- Reuse layout-driven flow (`UiPreparedLayout`) for portability.

UX review at milestone end:
- reduce menu-diving,
- make frequent actions one-step accessible.

## Milestone 7: ClipTrack Record
- Add record-to-slot flow with arm + record states.
- Support stop-immediate and quantized stop.
- Optional post-record normalize/trim flags.

UX review at milestone end:
- recording flow must fit into 2-3 explicit user actions.

## Milestone 8: Project Model v2 + Save/Load (Pattern-aware)
- Introduce canonical `ProjectState` including pattern bank.
- Add serialization with `schemaVersion`.
- Add migration hooks for future pattern/step schema expansion.

UX review at milestone end:
- save/load must be explicit, predictable, and recoverable.

## Milestone 9: Pattern v2 (locks + conditions)
- Add parameter locks on steps.
- Add trig conditions/probability/fill behavior.
- Integrate undo/redo as grouped transactions for step edits.

UX review at milestone end:
- step edit flow should remain fast under hardware-limited controls.

## Milestone 10: Performance Layer
- Add scene snapshots and macro apply batches.
- Add one-action performance FX presets.
- Keep real-time safe command batching for live use.

UX review at milestone end:
- prioritize one-click/one-gesture performance actions,
- remove low-frequency actions from primary control surface.

### UX simplification rule (applies to every milestone)
- At milestone close, run explicit UX pass:
  - remove redundant actions from active view,
  - move rarely used actions to context menus,
  - keep high-frequency actions as direct one-action controls,
  - verify navigation consistency across all scenes.

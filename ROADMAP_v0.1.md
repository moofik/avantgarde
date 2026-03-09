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
- [ ] Implement clip `targetSamples` from `bars + bpm + time signature + sampleRate`.
- [ ] Implement varispeed playback increment (`srcFrames / targetFrames`) with interpolation.
- [ ] Wire `setSlotLengthInBars()` into real playback behavior.
- [ ] Add tests for expected rendered length in samples.

Exit criteria:
- clip aligns to requested bar length under fixed BPM

## Milestone 4: Per-track FX stack
- [ ] Enable track-owned ordered `IAudioModule` chain per track.
- [ ] Route parameter updates via `ParamBridge` to track/slot targets.
- [ ] Add integration test: two tracks with different FX chains.

Exit criteria:
- stable dual-track playback with active FX chains

## Milestone 5: Portable UI layer
- [x] Introduce UI state DTO + renderer interface.
- [x] Introduce desktop ANSI renderer preview.
- [x] Add service-side state composer from runtime telemetry + transport + track state.
- [x] Add tiny input layer for transport/clip triggers.
- [ ] Add low-res renderer backend (RPi target adapter).

Exit criteria:
- same `UiState` drives desktop preview and RPi backend renderer

## First implementation slice delivered now
- Added `src/contracts/IUi.h`
- Added `src/service/UiStateStore.{h,cpp}`
- Added `src/platform/terminal/AnsiUiRenderer.{h,cpp}`
- Wired live status rendering into `main.cpp`

# ADR: Process-Isolated libobs Recorder

Status: accepted for implementation

Date: 2026-07-29

## Context

MoonLit needs a Windows-first replay recorder without moving frames through
Tauri IPC, injecting into games, or requiring users to install OBS Studio.
The existing direct WGC/D3D11/NVENC path is a useful benchmark, but it owns
too much codec, audio and container behavior inside the application process.

## Decision

Add a separate `LibobsSidecar` implementation of `ReplayBackend`.

```text
Tauri/Svelte
  -> RecorderRuntime
     -> LibobsSidecarBackend
        -> bounded framed JSON control protocol
           -> moonlit-recorder.exe
              -> moonlit-obs-bridge.dll
                 -> pinned libobs runtime
```

The host exchanges only capability data, control responses, errors and
completed clip metadata. Textures, audio samples and encoded packets stay in
the sidecar.

The first real vertical slice is monitor capture, H.264, MP4, video-only,
with automatic encoder selection and x264 as the required software fallback.
Audio, window capture, MKV and additional configuration become separate
contract increments after this slice is validated.

## Runtime policy

The runtime is loaded from an absolute app-local resource directory. It never
searches `PATH`, an OBS installation, user plugin directories or registry
locations. Modules are opened individually from an allowlist.

The runtime must not contain `win-capture`, `graphics-hook`, Game Capture,
injector helpers, virtual camera, browser, websocket or OBS Studio frontend
components. Process isolation is a crash boundary, not a security sandbox.

OBS 32.2.1 is the current pinned candidate. Its resolved commit, source hash,
dependency hashes and exact staged closure must be approved before release.

## Consequences

Positive:

* A native recorder crash cannot directly crash the Tauri UI.
* libobs owns replay buffering, encoding, audio timing and muxing.
* FakeBackend remains usable without a GPU or runtime assets.
* The control protocol can be reused by a future Linux sidecar.

Costs and constraints:

* The sidecar and runtime require an independently tested build and license
  manifest.
* The bundled closure is larger than the current raw native spike.
* The sidecar must be supervised with deadlines, EOF detection and explicit
  recovery; it must never be transparently restarted while preserving a lost
  replay history.
* Stock OBS capture plugins cannot be copied wholesale because they contain
  prohibited hook/injection capabilities.

## Rejected alternatives

* Bundling the OBS Studio executable: adds Qt, frontend, browser and unrelated
  plugins, and creates an unnecessary application surface.
* `obs-websocket`: adds a network/control service and does not solve the
  process/media ownership boundary.
* Media Foundation in `WindowsNativeBackend`: the interrupted implementation
  lacked a validated A/V/container contract and is superseded by libobs.
* Tauri shell-plugin execution from the frontend: the recorder is launched by
  trusted Rust code with an absolute path and no frontend shell permission.

## Current implementation boundary

The protocol crate, process supervisor, fail-closed recorder executable and
host backend are implemented. The libobs bridge, custom WGC source, curated
runtime build and installer remain explicit follow-up work. The sidecar must
report unavailable until those assets are present and self-tested.
